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

/** MQTT Host / IP */
#define SENSITIVE_MQTT_URL "127.0.0.1"
/** MQTT User */
#define SENSITIVE_MQTT_USER "DUMMY_MQTT_USER"
/** MQTT Password */
#define SENSITIVE_MQTT_PASS "DUMMY_MQTT_PASSWORD"
/** MQTT Port */
#define SENSITIVE_MQTT_PORT 1883
/** MQTT TLS: 0=false, 1=true */
#define SENSITIVE_MQTT_TLS 0
