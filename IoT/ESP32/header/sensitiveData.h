/**
 * @file sensitiveData.h
 * @brief 開発初期向け機密設定マクロ（ローカル専用）。
 * @details
 * - [重要] 本ファイルは開発初期にデバイスへ即反映するためのマクロ定義。
 * - [厳守] 本ファイルはGit管理禁止（実値が入るため）。
 * - [禁止] 本ファイルを公開リポジトリへアップロードしない。
 * - [将来対応] 本運用ではJSON/NVS/Secrets管理へ移行する。
 */

#pragma once

/** [推奨] 1: ヘッダー値優先（開発初期） / 0: ファイル読込優先 */
#define SENSITIVE_DATA_USE_HEADER_VALUES 1

/** Wi-Fi SSID */
#define SENSITIVE_WIFI_SSID "AP-IoTESP32Test"
/** Wi-Fi Password */
#define SENSITIVE_WIFI_PASS "IoTTest32Pass"

/** MQTT Host / IP */
#define SENSITIVE_MQTT_URL "172.16.1.59"
/** MQTT User */
#define SENSITIVE_MQTT_USER ""
/** MQTT Password */
#define SENSITIVE_MQTT_PASS ""
/** MQTT Port */
#define SENSITIVE_MQTT_PORT 1883
/** MQTT TLS: 0=false, 1=true */
#define SENSITIVE_MQTT_TLS 0
