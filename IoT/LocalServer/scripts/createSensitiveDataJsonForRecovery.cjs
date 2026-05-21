/**
 * k-device を LittleFS 用 sensitiveData.json に書き出す回復スクリプト。
 * [厳守] k-device の raw 値はログに出力しない。fingerprint のみ表示。
 * [使い方] node scripts/createSensitiveDataJsonForRecovery.cjs [targetDeviceName] [cloud]
 *   cloud: 省略時は credentials のみ（ローカルモード回復）。
 *          "cloud" を指定すると以下を付加：
 *            - wifi.wifiSSID / wifi.wifiPass: .env の CLOUD_WIFI_SSID / CLOUD_WIFI_PASSWORD から読む
 *              （ESP32 がインターネット接続するため WAN 接続のある AP に切替え。
 *               AP-IoTESP32Test は閉域 AP のため cloud で使用不可）
 *            - mqtt.brokerMode = "cloud"
 *            - mqtt.cloudEndpoint: .env の AWS_IOT_ENDPOINT から読む
 * [例]     node scripts/createSensitiveDataJsonForRecovery.cjs IoT_04CEF94EB580
 *          node scripts/createSensitiveDataJsonForRecovery.cjs IoT_04CEF94EB580 cloud
 *
 * 実行後の手順:
 *   1. pio run -e esp32s3_secure --target uploadfs --upload-port COM4
 *   2. ESP32 が LittleFS から NVS へ k-device / wifi / mqtt を移行（シリアルで "recovered keyDevice" を確認）
 *   3. sensitiveData.json を data/ から削除し、再度 uploadfs して LittleFS を最終化
 */
const path = require('path');
const fs = require('fs');
const crypto = require('crypto');

const { SecretCoreManager } = require('../dist/secretCoreManager.js');
const { SecretCoreIpcClient } = require('../dist/secretCoreIpcClient.js');
const { SecretCoreFacade } = require('../dist/secretCoreFacade.js');

const ESP32_DATA_DIR = path.resolve(__dirname, '../../ESP32/data');
const OUTPUT_PATH = path.join(ESP32_DATA_DIR, 'sensitiveData.json');

async function main() {
  const targetDeviceName = (process.argv[2] ?? 'IoT_04CEF94EB580').trim();
  console.log(`[recover] targetDeviceName=${targetDeviceName}`);
  console.log(`[recover] outputPath=${OUTPUT_PATH}`);

  if (!fs.existsSync(ESP32_DATA_DIR)) {
    throw new Error(`ESP32 data dir not found: ${ESP32_DATA_DIR}`);
  }

  const manager = new SecretCoreManager();
  manager.start();
  await manager.waitForBootstrapAck();

  const ipcClient = new SecretCoreIpcClient(
    () => manager.getPipeName(),
    () => manager.getIpcSessionKeyBase64()
  );

  // [重要] ACK 受信後も SecretCore 内で KeyManager 初期化 → pipe 作成完了までに数百ミリ秒の遅延がある。
  //         server.ts ではリトライで吸収するが、本スクリプトでも同様に最大 10 回（合計約 5 秒）リトライする。
  let healthy = false;
  for (let attempt = 1; attempt <= 10; attempt += 1) {
    healthy = await ipcClient.checkHealth(false);
    if (healthy) {
      if (attempt > 1) {
        console.log(`[recover] SecretCore health check succeeded on attempt ${attempt}.`);
      }
      break;
    }
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  if (!healthy) {
    // 最終リトライでエラー詳細をログへ出すため verbose=true で 1 回叩き直す
    await ipcClient.checkHealth(true);
    manager.stop();
    throw new Error('SecretCore health check failed after 10 retries (5s total).');
  }

  const facade = new SecretCoreFacade(ipcClient);
  const result = await facade.getKDevice(targetDeviceName);
  const keyDeviceBase64 = (result && result.keyDeviceBase64) ? result.keyDeviceBase64 : '';
  if (keyDeviceBase64.length === 0) {
    manager.stop();
    throw new Error(`getKDevice returned empty keyDeviceBase64 for ${targetDeviceName}`);
  }

  const fingerprint = crypto
    .createHash('sha256')
    .update(Buffer.from(keyDeviceBase64, 'base64'))
    .digest('hex')
    .slice(0, 16);
  console.log(`[recover] k-device fingerprint=${fingerprint} (raw value NOT shown)`);

  // credentials.keyDevice は必須。ESP32 起動時に ensureDefaultFileExists() が NVS へ移行する。
  // [重要] brokerMode は SENSITIVE_DATA_USE_HEADER_VALUES に影響されないため、
  //         クラウドモード回復時は必ず mqtt.brokerMode + mqtt.cloudEndpoint を含める。
  // [重要] クラウドモードは ESP32 がインターネット接続するため WAN 接続のある AP に切替える。
  //         CLOUD_WIFI_SSID / CLOUD_WIFI_PASSWORD を .env から読んで wifi セクションへ書き出す。
  const targetBrokerMode = (process.argv[3] ?? '').trim().toLowerCase();
  /** @type {{
   *   credentials: { keyDevice: string };
   *   wifi?: { wifiSSID: string; wifiPass: string };
   *   mqtt?: { brokerMode: string; cloudEndpoint: string };
   * }} */
  const jsonRoot = { credentials: { keyDevice: keyDeviceBase64 } };

  if (targetBrokerMode === 'cloud') {
    require('dotenv').config({ path: path.resolve(__dirname, '../.env') });
    const cloudEndpoint = (process.env.AWS_IOT_ENDPOINT ?? '').trim();
    if (!cloudEndpoint) {
      manager.stop();
      throw new Error('AWS_IOT_ENDPOINT is not set in IoT/LocalServer/.env. Cannot write cloud broker mode to recovery JSON.');
    }
    const cloudWifiSsid = (process.env.CLOUD_WIFI_SSID ?? '').trim();
    const cloudWifiPassword = (process.env.CLOUD_WIFI_PASSWORD ?? '').trim();
    if (!cloudWifiSsid || !cloudWifiPassword) {
      manager.stop();
      throw new Error(
        'CLOUD_WIFI_SSID / CLOUD_WIFI_PASSWORD is not set in IoT/LocalServer/.env. ' +
          'Cloud mode requires a Wi-Fi AP with internet access (AP-IoTESP32Test has no WAN). ' +
          'Set both values in .env and retry.'
      );
    }
    jsonRoot.wifi = { wifiSSID: cloudWifiSsid, wifiPass: cloudWifiPassword };
    jsonRoot.mqtt = { brokerMode: 'cloud', cloudEndpoint };
    console.log(`[recover] Including wifi.ssid=${cloudWifiSsid} (pass=*** length=${cloudWifiPassword.length})`);
    console.log(`[recover] Including brokerMode=cloud, endpoint=${cloudEndpoint}`);
  } else if (targetBrokerMode === 'local') {
    // [重要] ローカルモードへの切替（7208 フォールバック試験用）。
    //         wifi は ESP32 ヘッダー(SENSITIVE_WIFI_SSID="AP-IoTESP32Test") を使うため JSON には含めない。
    //         main.cpp の cloudModeActive=false 分岐がヘッダー値を NVS へ上書きする設計。
    //         cloudEndpoint も含めない（local モードでは未使用）。
    jsonRoot.mqtt = { brokerMode: 'local', cloudEndpoint: '' };
    console.log('[recover] Including brokerMode=local (cloud fallback test).');
    console.log('[recover] wifi not included (ESP32 will use SENSITIVE_WIFI_SSID="AP-IoTESP32Test" from header).');
  } else {
    console.log('[recover] brokerMode not included. Pass "cloud" or "local" as 3rd arg to include broker mode config.');
    console.log('[recover] wifi not included (preserves existing NVS wifi credentials).');
  }

  const jsonContent = JSON.stringify(jsonRoot);
  fs.writeFileSync(OUTPUT_PATH, jsonContent, { encoding: 'utf8' });
  console.log(`[recover] Written: ${OUTPUT_PATH}`);
  console.log('[recover] DONE. Next: uploadfs → verify serial → delete file → uploadfs again.');

  manager.stop();
  process.exit(0);
}

main().catch((err) => {
  console.error('[recover] FAILED:', err.message);
  process.exit(1);
});
