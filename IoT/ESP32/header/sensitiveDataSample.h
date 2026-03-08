/**
 * @file sensitiveDataSample.h
 * @brief 公開可能な機密設定サンプル（ダミー値）。
 * @details
 * - [重要] 正ファイルは `sensitiveData.h`。
 * - [厳守] `sensitiveData.h` は実値を含むためGit管理禁止。
 * - [推奨] 新規環境では本サンプルをコピーして `sensitiveData.h` を作成する。
 */

#pragma once

/** [推奨] 1: ヘッダー値優先（開発初期） / 0: ファイル読込優先 */
#define SENSITIVE_DATA_USE_HEADER_VALUES 1

/** Wi-Fi SSID */
#define SENSITIVE_WIFI_SSID "DUMMY_WIFI_SSID"
/** Wi-Fi Password */
#define SENSITIVE_WIFI_PASS "DUMMY_WIFI_PASSWORD"

/** MQTT HostName（正規名） */
#define SENSITIVE_MQTT_HOST_NAME "mqtt.example.local"
/** MQTT HostIp（診断/代替用） */
#define SENSITIVE_MQTT_HOST_IP "127.0.0.1"
/** MQTT 現在使用値 */
#define SENSITIVE_MQTT_URL SENSITIVE_MQTT_HOST_NAME
/** MQTT User */
#define SENSITIVE_MQTT_USER "DUMMY_USER"
/** MQTT Password */
#define SENSITIVE_MQTT_PASS "DUMMY_PASSWORD"
/** MQTT Port */
#define SENSITIVE_MQTT_PORT 8883
/** MQTT TLS: 0=false, 1=true */
#define SENSITIVE_MQTT_TLS 1
/** MQTT DNS失敗時の暫定フォールバックIP */
#define SENSITIVE_MQTT_FALLBACK_IP SENSITIVE_MQTT_HOST_IP
/**
 * MQTT TLS CA証明書(PEM)サンプル。
 * [厳守] 実運用ではブローカー証明書チェーンを検証可能なCA証明書を設定する。
 */
#define SENSITIVE_MQTT_TLS_CA_CERT \
    "-----BEGIN CERTIFICATE-----\n" \
    "DUMMY_CA_CERTIFICATE_CONTENT\n" \
    "-----END CERTIFICATE-----\n"

/** Server HostName（正規名） */
#define SENSITIVE_SERVER_HOST_NAME "api.example.local"
/** Server HostIp（診断/代替用） */
#define SENSITIVE_SERVER_HOST_IP "127.0.0.1"
/** Server 現在使用値 */
#define SENSITIVE_SERVER_URL SENSITIVE_SERVER_HOST_NAME
/** Server 診断/代替用IP */
#define SENSITIVE_SERVER_FALLBACK_IP SENSITIVE_SERVER_HOST_IP
/** Server User */
#define SENSITIVE_SERVER_USER "DUMMY_USER"
/** Server Password */
#define SENSITIVE_SERVER_PASS "DUMMY_PASSWORD"
/** Server Port */
#define SENSITIVE_SERVER_PORT 443
/** Server TLS: 0=false, 1=true */
#define SENSITIVE_SERVER_TLS 1

/** OTA HostName（正規名） */
#define SENSITIVE_OTA_HOST_NAME "ota.example.local"
/** OTA HostIp（診断/代替用） */
#define SENSITIVE_OTA_HOST_IP "127.0.0.1"
/** OTA 現在使用値 */
#define SENSITIVE_OTA_URL SENSITIVE_OTA_HOST_NAME
/** OTA 診断/代替用IP */
#define SENSITIVE_OTA_FALLBACK_IP SENSITIVE_OTA_HOST_IP
/** OTA User */
#define SENSITIVE_OTA_USER "DUMMY_USER"
/** OTA Password */
#define SENSITIVE_OTA_PASS "DUMMY_PASSWORD"
/** OTA Port */
#define SENSITIVE_OTA_PORT 443
/** OTA TLS: 0=false, 1=true */
#define SENSITIVE_OTA_TLS 1

/** TimeServer HostName（正規名） */
#define SENSITIVE_TIME_SERVER_HOST_NAME "ntp.example.local"
/** TimeServer HostIp（診断/代替用） */
#define SENSITIVE_TIME_SERVER_HOST_IP "127.0.0.1"
/** TimeServer 現在使用値 */
#define SENSITIVE_TIME_SERVER_URL SENSITIVE_TIME_SERVER_HOST_NAME
/** TimeServer 診断/代替用IP */
#define SENSITIVE_TIME_SERVER_FALLBACK_IP SENSITIVE_TIME_SERVER_HOST_IP
/** TimeServer Port */
#define SENSITIVE_TIME_SERVER_PORT 123
/** TimeServer TLS: 0=false, 1=true */
#define SENSITIVE_TIME_SERVER_TLS 0
