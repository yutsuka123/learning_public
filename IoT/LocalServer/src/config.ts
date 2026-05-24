/**
 * @file config.ts
 * @description LocalServerの環境変数読み込みと入力検証を担う。
 * @remarks
 * - [重要] 起動失敗時は「不足項目」「無効値」を具体的に例外へ含める。
 * - [厳守] 機密値はログに平文出力しない。
 * - [推奨] `.env` は `env.example.sample.txt` を元に作成し、サンプル値との差分を明確に保つ。
 * - [重要][2026-05-17] `LOCAL_HISTORY_RETENTION_DAYS`: `0` は無期限（パージなし）、`1`〜`99999` は保持日数。非機密。**`settings.json` の初期既定は 30 日**（コード）。本変数は既定 `30`・`loadConfig` の検証用。runtime の正本は `localHistoryRetentionDays`（設定 API）。
 */

import path from "path";
import dotenv from "dotenv";
import { parseLocalHistoryRetentionDaysFromEnv } from "./localHistoryRetention";

dotenv.config();

/**
 * @description LocalServer設定オブジェクト。
 */
export interface appConfig {
  mqttHostName: string;
  mqttHostIp: string;
  httpPort: number;
  wsPath: string;
  sourceId: string;
  mqttHost: string;
  mqttFallbackIp: string;
  mqttPort: number;
  mqttProtocol: "mqtts" | "mqtt";
  mqttUsername: string;
  mqttPassword: string;
  mqttCaPath: string;
  mqttConnectTimeoutMs: number;
  statusOfflineTimeoutSeconds: number;
  statusRequestOnBoot: boolean;
  statusRequestBootDelayMs: number;
  otaPublicHostName: string;
  otaPublicHostIp: string;
  otaPublicHost: string;
  otaHttpsPort: number;
  otaHttpsCertPath: string;
  otaHttpsKeyPath: string;
  otaFirmwarePath: string;
  otaFirmwareVersion: string;
  kUserAppIdentifier: string;
  adminUsername: string;
  adminPassword: string;
  wifiUsbInterfaceName: string;
  apHttpBaseUrl: string;
  apSsidPrefix: string;
  apWifiPassword: string;
  apRoleAdminUsername: string;
  apRoleAdminPassword: string;
  apRoleMfgUsername: string;
  apRoleMfgPassword: string;
  /** @description SQLite 履歴DBの絶対パス。 */
  localHistoryDbPath: string;
  /** @description 履歴保持日数。0 は無期限。 */
  localHistoryRetentionDays: number;
  /** @description 期限切れパージの実行間隔（ミリ秒）。 */
  localHistoryPurgeIntervalMs: number;
  /** @description 履歴平文エクスポートファイルの保存ディレクトリ（絶対パス）。 */
  localHistoryExportDir: string;
  /** @description true のとき SQLite 履歴を定期エクスポートする（LocalServer プロセス内タイマー）。 */
  localHistoryScheduledExportEnabled: boolean;
  /** @description 定期エクスポート間隔（ミリ秒）。有効時は 60000 以上の整数。 */
  localHistoryScheduledExportIntervalMs: number;
  /**
   * @description 定期エクスポートのフィルタ JSON（`localHistoryExportRequestBody` と同一形のオブジェクトを JSON 化した文字列）。空なら全件相当（sources 既定）。
   */
  localHistoryScheduledExportFilterJson: string;
  /**
   * @description true のとき AWS IoT Core subscriber を有効化する。
   * @remarks .env の CLOUD_MQTT_ENABLED で制御。試験後は必ず false に戻すこと（課金防止）。
   */
  cloudMqttEnabled: boolean;
  /** @description クラウドプロバイダー識別子。現在は "aws-iot-core" のみ対応。.env の CLOUD_PROVIDER。 */
  cloudProvider: string;
  /** @description AWS IoT Core カスタムエンドポイント。.env の AWS_IOT_ENDPOINT。 */
  cloudIotEndpoint: string;
  /** @description AWS IoT Core 接続用 clientId。.env の AWS_IOT_CLIENT_ID。 */
  cloudIotClientId: string;
  /** @description LocalServer 用 X.509 クライアント証明書の絶対パス。.env の AWS_IOT_CLIENT_CERT_PATH。 */
  cloudIotClientCertPath: string;
  /** @description LocalServer 用 X.509 秘密鍵の絶対パス。.env の AWS_IOT_PRIVATE_KEY_PATH。 */
  cloudIotPrivateKeyPath: string;
  /** @description Amazon Root CA 証明書の絶対パス。.env の AWS_IOT_CA_CERT_PATH。 */
  cloudIotCaCertPath: string;
  /**
   * @description cloud OTA で firmware を保管する S3 バケット名。.env の OTA_S3_BUCKET。
   * @remarks cloudMqttEnabled=true かつ本フィールドが空でない場合に S3 presigned URL 経由 OTA を使用する。
   */
  otaS3Bucket: string;
  /** @description S3 presigned URL の有効期限（秒）。.env の OTA_PRESIGN_TTL_SECONDS。既定 600 秒。 */
  otaPresignTtlSeconds: number;
  /** @description S3 バケット内のキープレフィックス。.env の OTA_S3_KEY_PREFIX。既定 "firmware/"。 */
  otaS3KeyPrefix: string;
}

/**
 * @description 文字列環境変数を取得し、未設定時は既定値を返す。
 * @param envName 取得対象の環境変数名。
 * @param defaultValue 未設定時に利用する既定値。
 * @returns 文字列値。
 */
function getStringEnv(envName: string, defaultValue: string): string {
  const rawValue = process.env[envName];
  return rawValue === undefined || rawValue.trim().length === 0 ? defaultValue : rawValue.trim();
}

/**
 * @description 数値環境変数を取得し、妥当性を検証する。
 * @param envName 取得対象の環境変数名。
 * @param defaultValue 未設定時の既定値。
 * @returns 数値化後の値。
 */
function getNumberEnv(envName: string, defaultValue: number): number {
  const rawValue = process.env[envName];
  if (rawValue === undefined || rawValue.trim().length === 0) {
    return defaultValue;
  }

  const parsedValue = Number(rawValue);
  if (!Number.isFinite(parsedValue) || Number.isNaN(parsedValue)) {
    throw new Error(`getNumberEnv failed. envName=${envName} rawValue=${rawValue}`);
  }

  return parsedValue;
}

/**
 * @description 真偽値環境変数を取得する。
 * @param envName 取得対象の環境変数名。
 * @param defaultValue 既定値。
 * @returns 真偽値。
 */
function getBooleanEnv(envName: string, defaultValue: boolean): boolean {
  const rawValue = process.env[envName];
  if (rawValue === undefined || rawValue.trim().length === 0) {
    return defaultValue;
  }

  const normalizedValue = rawValue.trim().toLowerCase();
  if (normalizedValue === "true" || normalizedValue === "1" || normalizedValue === "yes") {
    return true;
  }
  if (normalizedValue === "false" || normalizedValue === "0" || normalizedValue === "no") {
    return false;
  }

  throw new Error(`getBooleanEnv failed. envName=${envName} rawValue=${rawValue}`);
}

/**
 * @description 実行ディレクトリ基準でパスを絶対パス化する。
 * @param pathText 相対または絶対パス。
 * @returns 絶対パス文字列。
 */
function toAbsolutePath(pathText: string): string {
  return path.isAbsolute(pathText) ? pathText : path.resolve(process.cwd(), pathText);
}

/**
 * @description LocalServerの設定を読み込み検証する。
 * @returns 検証済み設定。
 */
export function loadConfig(): appConfig {
  const httpPort = getNumberEnv("LOCAL_SERVER_HTTP_PORT", 3100);
  const mqttPort = getNumberEnv("MQTT_PORT", 8883);
  const otaHttpsPort = getNumberEnv("OTA_HTTPS_PORT", 443);
  const mqttConnectTimeoutMs = getNumberEnv("MQTT_CONNECT_TIMEOUT_MS", 15000);
  const statusOfflineTimeoutSeconds = getNumberEnv("STATUS_OFFLINE_TIMEOUT_SECONDS", 90);
  const statusRequestBootDelayMs = getNumberEnv("STATUS_REQUEST_BOOT_DELAY_MS", 3000);
  const mqttProtocolRaw = getStringEnv("MQTT_PROTOCOL", "mqtts");
  if (mqttProtocolRaw !== "mqtt" && mqttProtocolRaw !== "mqtts") {
    throw new Error(`loadConfig failed. MQTT_PROTOCOL must be mqtt or mqtts. value=${mqttProtocolRaw}`);
  }
  const mqttHostName = getStringEnv("MQTT_HOST_NAME", getStringEnv("MQTT_HOST", "mqtt.esplab.home.arpa"));
  const mqttHostIp = getStringEnv("MQTT_HOST_IP", getStringEnv("MQTT_FALLBACK_IP", ""));
  const otaPublicHostName = getStringEnv("OTA_PUBLIC_HOST_NAME", getStringEnv("OTA_PUBLIC_HOST", "ota.esplab.home.arpa"));
  const otaPublicHostIp = getStringEnv("OTA_PUBLIC_HOST_IP", "");
  const localHistoryRetentionDays = parseLocalHistoryRetentionDaysFromEnv(
    getNumberEnv("LOCAL_HISTORY_RETENTION_DAYS", 30)
  );
  const localHistoryPurgeIntervalMs = getNumberEnv("LOCAL_HISTORY_PURGE_INTERVAL_MS", 86400000);
  if (!Number.isInteger(localHistoryPurgeIntervalMs) || localHistoryPurgeIntervalMs < 60000) {
    throw new Error(
      `loadConfig failed. LOCAL_HISTORY_PURGE_INTERVAL_MS must be an integer >= 60000. value=${localHistoryPurgeIntervalMs}`
    );
  }

  const localHistoryScheduledExportEnabled = getBooleanEnv("LOCAL_HISTORY_SCHEDULED_EXPORT_ENABLED", false);
  const localHistoryScheduledExportIntervalMs = getNumberEnv(
    "LOCAL_HISTORY_SCHEDULED_EXPORT_INTERVAL_MS",
    86400000
  );
  if (localHistoryScheduledExportEnabled) {
    if (!Number.isInteger(localHistoryScheduledExportIntervalMs) || localHistoryScheduledExportIntervalMs < 60000) {
      throw new Error(
        `loadConfig failed. LOCAL_HISTORY_SCHEDULED_EXPORT_INTERVAL_MS must be an integer >= 60000 when scheduled export is enabled. value=${localHistoryScheduledExportIntervalMs}`
      );
    }
  }

  const cloudMqttEnabled = getBooleanEnv("CLOUD_MQTT_ENABLED", false);
  const cloudIotEndpoint = getStringEnv("AWS_IOT_ENDPOINT", "");
  const cloudIotClientId = getStringEnv("AWS_IOT_CLIENT_ID", "");
  // [重要] 証明書パスは cloudMqttEnabled に依存せず常にロードする。動的モード切替時に使用するため。
  const cloudIotClientCertPath = toAbsolutePath(getStringEnv("AWS_IOT_CLIENT_CERT_PATH", ""));
  const cloudIotPrivateKeyPath = toAbsolutePath(getStringEnv("AWS_IOT_PRIVATE_KEY_PATH", ""));
  const cloudIotCaCertPath = toAbsolutePath(getStringEnv("AWS_IOT_CA_CERT_PATH", ""));

  const nextConfig: appConfig = {
    mqttHostName,
    mqttHostIp,
    httpPort,
    wsPath: getStringEnv("LOCAL_SERVER_WS_PATH", "/ws"),
    sourceId: getStringEnv("LOCAL_SERVER_SOURCE_ID", "local-server-001"),
    mqttHost: mqttHostName,
    mqttFallbackIp: mqttHostIp,
    mqttPort,
    mqttProtocol: mqttProtocolRaw,
    mqttUsername: getStringEnv("MQTT_USERNAME", "esp32lab_mqtt"),
    mqttPassword: getStringEnv("MQTT_PASSWORD", "esp32lab_mqtt_pass32"),
    mqttCaPath: toAbsolutePath(getStringEnv("MQTT_TLS_CA_PATH", "../ESP32/src/MQTT/ca.crt")),
    mqttConnectTimeoutMs,
    statusOfflineTimeoutSeconds,
    statusRequestOnBoot: getBooleanEnv("STATUS_REQUEST_ON_BOOT", true),
    statusRequestBootDelayMs,
    otaPublicHostName,
    otaPublicHostIp,
    otaPublicHost: otaPublicHostName,
    otaHttpsPort,
    otaHttpsCertPath: toAbsolutePath(getStringEnv("OTA_HTTPS_CERT_PATH", "./certs/server.crt")),
    otaHttpsKeyPath: toAbsolutePath(getStringEnv("OTA_HTTPS_KEY_PATH", "./certs/server.key")),
    otaFirmwarePath: toAbsolutePath(getStringEnv("OTA_FIRMWARE_PATH", "./ota/firmware.bin")),
    otaFirmwareVersion: getStringEnv("OTA_FIRMWARE_VERSION", "0.1.0"),
    kUserAppIdentifier: getStringEnv("K_USER_APP_IDENTIFIER", "esp32lab-default"),
    adminUsername: getStringEnv("LOCAL_ADMIN_USERNAME", "admin"),
    adminPassword: getStringEnv("LOCAL_ADMIN_PASSWORD", "change-this-password"),
    wifiUsbInterfaceName: getStringEnv("LOCAL_WIFI_USB_INTERFACE_NAME", ""),
    apHttpBaseUrl: getStringEnv("AP_HTTP_BASE_URL", "http://192.168.4.1"),
    apSsidPrefix: getStringEnv("AP_SSID_PREFIX", "AP-esp32lab-"),
    apWifiPassword: getStringEnv("AP_WIFI_PASSWORD", "<AP_WIFI_PASSWORD>"),
    apRoleAdminUsername: getStringEnv("AP_ROLE_ADMIN_USERNAME", "admin"),
    apRoleAdminPassword: getStringEnv("AP_ROLE_ADMIN_PASSWORD", "change-me"),
    apRoleMfgUsername: getStringEnv("AP_ROLE_MFG_USERNAME", "mfg"),
    apRoleMfgPassword: getStringEnv("AP_ROLE_MFG_PASSWORD", "change-me"),
    localHistoryDbPath: toAbsolutePath(getStringEnv("LOCAL_HISTORY_DB_PATH", "./data/localHistory.db")),
    localHistoryRetentionDays,
    localHistoryPurgeIntervalMs,
    localHistoryExportDir: toAbsolutePath(getStringEnv("LOCAL_HISTORY_EXPORT_DIR", "./data/history-exports")),
    localHistoryScheduledExportEnabled,
    localHistoryScheduledExportIntervalMs,
    localHistoryScheduledExportFilterJson: getStringEnv("LOCAL_HISTORY_SCHEDULED_EXPORT_FILTER_JSON", ""),
    cloudMqttEnabled,
    cloudProvider: getStringEnv("CLOUD_PROVIDER", "aws-iot-core"),
    cloudIotEndpoint,
    cloudIotClientId,
    cloudIotClientCertPath,
    cloudIotPrivateKeyPath,
    cloudIotCaCertPath,
    otaS3Bucket: getStringEnv("OTA_S3_BUCKET", ""),
    otaPresignTtlSeconds: getNumberEnv("OTA_PRESIGN_TTL_SECONDS", 600),
    otaS3KeyPrefix: getStringEnv("OTA_S3_KEY_PREFIX", "firmware/")
  };

  if (nextConfig.mqttHostName.length === 0) {
    throw new Error("loadConfig failed. MQTT_HOST_NAME is empty.");
  }
  if (nextConfig.otaPublicHostName.length === 0) {
    throw new Error("loadConfig failed. OTA_PUBLIC_HOST_NAME is empty.");
  }
  if (nextConfig.sourceId.length === 0) {
    throw new Error("loadConfig failed. LOCAL_SERVER_SOURCE_ID is empty.");
  }
  // [重要] クラウドモード有効時はローカル MQTT 認証情報を省略可能（AWS IoT Core X.509 認証を使用）。
  if (!nextConfig.cloudMqttEnabled && (nextConfig.mqttUsername.length === 0 || nextConfig.mqttPassword.length === 0)) {
    throw new Error("loadConfig failed. MQTT_USERNAME or MQTT_PASSWORD is empty.");
  }
  if (nextConfig.kUserAppIdentifier.length === 0) {
    throw new Error("loadConfig failed. K_USER_APP_IDENTIFIER is empty.");
  }
  if (nextConfig.adminUsername.length === 0 || nextConfig.adminPassword.length === 0) {
    throw new Error("loadConfig failed. LOCAL_ADMIN_USERNAME or LOCAL_ADMIN_PASSWORD is empty.");
  }
  if (nextConfig.cloudMqttEnabled) {
    if (nextConfig.cloudIotEndpoint.length === 0) {
      throw new Error("loadConfig failed. AWS_IOT_ENDPOINT is required when CLOUD_MQTT_ENABLED=true.");
    }
    if (nextConfig.cloudIotClientCertPath.length === 0) {
      throw new Error("loadConfig failed. AWS_IOT_CLIENT_CERT_PATH is required when CLOUD_MQTT_ENABLED=true.");
    }
    if (nextConfig.cloudIotPrivateKeyPath.length === 0) {
      throw new Error("loadConfig failed. AWS_IOT_PRIVATE_KEY_PATH is required when CLOUD_MQTT_ENABLED=true.");
    }
    if (nextConfig.cloudIotCaCertPath.length === 0) {
      throw new Error("loadConfig failed. AWS_IOT_CA_CERT_PATH is required when CLOUD_MQTT_ENABLED=true.");
    }
  }

  return nextConfig;
}
