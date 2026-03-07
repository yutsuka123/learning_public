/**
 * @file config.ts
 * @description LocalServerの環境変数読み込みと入力検証を担う。
 * @remarks
 * - [重要] 起動失敗時は「不足項目」「無効値」を具体的に例外へ含める。
 * - [厳守] 機密値はログに平文出力しない。
 * - [推奨] `.env` と `.env.example` を同期維持する。
 */

import path from "path";
import dotenv from "dotenv";

dotenv.config();

/**
 * @description LocalServer設定オブジェクト。
 */
export interface appConfig {
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
  otaPublicHost: string;
  otaHttpsPort: number;
  otaHttpsCertPath: string;
  otaHttpsKeyPath: string;
  otaFirmwarePath: string;
  otaFirmwareVersion: string;
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

  const nextConfig: appConfig = {
    httpPort,
    wsPath: getStringEnv("LOCAL_SERVER_WS_PATH", "/ws"),
    sourceId: getStringEnv("LOCAL_SERVER_SOURCE_ID", "local-server-001"),
    mqttHost: getStringEnv("MQTT_HOST", "mqtt.esplab.home.arpa"),
    mqttFallbackIp: getStringEnv("MQTT_FALLBACK_IP", ""),
    mqttPort,
    mqttProtocol: mqttProtocolRaw,
    mqttUsername: getStringEnv("MQTT_USERNAME", "esp32lab_mqtt"),
    mqttPassword: getStringEnv("MQTT_PASSWORD", "esp32lab_mqtt_pass32"),
    mqttCaPath: toAbsolutePath(getStringEnv("MQTT_TLS_CA_PATH", "../ESP32/src/MQTT/ca.crt")),
    mqttConnectTimeoutMs,
    statusOfflineTimeoutSeconds,
    statusRequestOnBoot: getBooleanEnv("STATUS_REQUEST_ON_BOOT", true),
    statusRequestBootDelayMs,
    otaPublicHost: getStringEnv("OTA_PUBLIC_HOST", "ota.esplab.home.arpa"),
    otaHttpsPort,
    otaHttpsCertPath: toAbsolutePath(getStringEnv("OTA_HTTPS_CERT_PATH", "./certs/server.crt")),
    otaHttpsKeyPath: toAbsolutePath(getStringEnv("OTA_HTTPS_KEY_PATH", "./certs/server.key")),
    otaFirmwarePath: toAbsolutePath(getStringEnv("OTA_FIRMWARE_PATH", "./ota/firmware.bin")),
    otaFirmwareVersion: getStringEnv("OTA_FIRMWARE_VERSION", "0.1.0")
  };

  if (nextConfig.mqttHost.length === 0) {
    throw new Error("loadConfig failed. MQTT_HOST is empty.");
  }
  if (nextConfig.sourceId.length === 0) {
    throw new Error("loadConfig failed. LOCAL_SERVER_SOURCE_ID is empty.");
  }
  if (nextConfig.mqttUsername.length === 0 || nextConfig.mqttPassword.length === 0) {
    throw new Error("loadConfig failed. MQTT_USERNAME or MQTT_PASSWORD is empty.");
  }

  return nextConfig;
}
