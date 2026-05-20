/**
 * k-device を LittleFS 用 sensitiveData.json に書き出す回復スクリプト。
 * [厳守] k-device の raw 値はログに出力しない。fingerprint のみ表示。
 * [使い方] node scripts/createSensitiveDataJsonForRecovery.cjs [targetDeviceName] [cloud]
 *   cloud: 省略時は credentials のみ（ローカルモード回復）。
 *          "cloud" を指定すると mqtt.brokerMode=cloud + cloudEndpoint も付加する。
 *          cloudEndpoint は IoT/LocalServer/.env の AWS_IOT_ENDPOINT から読む。
 * [例]     node scripts/createSensitiveDataJsonForRecovery.cjs IoT_04CEF94EB580
 *          node scripts/createSensitiveDataJsonForRecovery.cjs IoT_04CEF94EB580 cloud
 *
 * 実行後の手順:
 *   1. pio run -e esp32s3_secure --target uploadfs --upload-port COM4
 *   2. ESP32 が LittleFS から NVS へ k-device を移行（シリアルで "recovered keyDevice" を確認）
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

  const healthy = await ipcClient.checkHealth(true);
  if (!healthy) {
    manager.stop();
    throw new Error('SecretCore health check failed.');
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
  const targetBrokerMode = (process.argv[3] ?? '').trim().toLowerCase();
  /** @type {{ credentials: { keyDevice: string }; mqtt?: { brokerMode: string; cloudEndpoint: string } }} */
  const jsonRoot = { credentials: { keyDevice: keyDeviceBase64 } };

  if (targetBrokerMode === 'cloud') {
    require('dotenv').config({ path: path.resolve(__dirname, '../.env') });
    const cloudEndpoint = (process.env.AWS_IOT_ENDPOINT ?? '').trim();
    if (!cloudEndpoint) {
      manager.stop();
      throw new Error('AWS_IOT_ENDPOINT is not set in IoT/LocalServer/.env. Cannot write cloud broker mode to recovery JSON.');
    }
    jsonRoot.mqtt = { brokerMode: 'cloud', cloudEndpoint };
    console.log(`[recover] Including brokerMode=cloud, endpoint=${cloudEndpoint}`);
  } else {
    console.log('[recover] brokerMode not included (local mode). Pass "cloud" as 3rd arg to include cloud config.');
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
