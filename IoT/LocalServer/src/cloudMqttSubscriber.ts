/**
 * @file cloudMqttSubscriber.ts
 * @description AWS IoT Core 向け MQTT クライアントを生成し、mqttGateway へ注入するファクトリ。
 * @remarks
 * - [重要] X.509 相互 TLS 認証（ユーザー名/パスワード不使用）。
 * - [厳守] 証明書ファイルは .env の AWS_IOT_CLIENT_CERT_PATH 等で指定した絶対パスから読む。Git には絶対にコミットしない。
 * - [厳守] rejectUnauthorized: true を維持する（証明書検証スキップ禁止）。
 * - [重要] テスト後は必ず CLOUD_MQTT_ENABLED=false に戻すこと（課金防止）。
 * - [設計] メッセージ処理・ペイロード復号・イベント発火はすべて mqttGateway に委譲する。
 *           本ファイルはクラウド向け接続オプションと MqttClient 生成のみを担当する。
 */

import fs from "fs";
import os from "os";
import mqtt, { IClientOptions } from "mqtt";
import { appConfig } from "./config";
import { DeviceRegistry } from "./deviceRegistry";
import { deviceTransport } from "./deviceTransport";
import { mqttGateway } from "./mqttGateway";
import { keyService } from "./keyService";
import { LocalHistoryStore } from "./localHistoryStore";

/**
 * @description AWS IoT Core に接続する mqttGateway インスタンスを生成する。
 * @param config アプリ設定（cloudMqttEnabled=true かつ証明書パス設定済みであること）。
 * @param registry デバイス状態レジストリ。
 * @param localKeyService ペイロード復号用鍵サービス。
 * @param historyStore SQLite 履歴（任意）。
 * @returns cloudMqttEnabled=true 時は AWS IoT Core 向け deviceTransport、false 時は undefined。
 * @throws config.cloudIotEndpoint / 証明書パスが未設定の場合は起動時エラー。
 */
export function createCloudMqttGateway(
  config: appConfig,
  registry: DeviceRegistry,
  localKeyService: keyService,
  historyStore?: LocalHistoryStore
): deviceTransport | undefined {
  if (!config.cloudMqttEnabled) {
    return undefined;
  }

  const certBuffer = fs.readFileSync(config.cloudIotClientCertPath);
  const keyBuffer = fs.readFileSync(config.cloudIotPrivateKeyPath);
  const caBuffer = fs.readFileSync(config.cloudIotCaCertPath);

  const clientId =
    config.cloudIotClientId.length > 0
      ? config.cloudIotClientId
      : `localserver-${os.hostname()}-${Math.floor(Date.now() / 1000)}`;

  const options: IClientOptions = {
    cert: certBuffer,
    key: keyBuffer,
    ca: caBuffer,
    rejectUnauthorized: true,
    clientId,
    reconnectPeriod: 5000,
    connectTimeout: 30000,
    manualConnect: true
  };

  const brokerUrl = `mqtts://${config.cloudIotEndpoint}:8883`;
  console.info(`cloudMqttSubscriber: creating AWS IoT Core client. endpoint=${config.cloudIotEndpoint} clientId=${clientId}`);

  const client = mqtt.connect(brokerUrl, options);

  return new mqttGateway(
    config,
    registry,
    localKeyService,
    undefined,
    "ts",
    historyStore,
    client
  );
}
