/**
 * @file define.h
 * @brief ESP32向けのコンパイル時定義および固定値。
 * @details
 * - 本ファイルはESP実装専用の定数を集約する。
 * - 機密情報（鍵、パスワード、証明書）は定義しない。
 * - 値変更時は通信相手（Local/Cloud）との整合を確認する。
 */

#pragma once

#include <stdint.h>

namespace appDefine {

/**
 * @brief FreeRTOSタスク共通待機周期（ms）。
 */
constexpr uint32_t kTaskLoopIntervalMs = 1000;

/**
 * @brief シリアル通信ボーレート。
 */
constexpr uint32_t kSerialBaudRate = 115200;

/**
 * @brief MQTT既定ポート（TLS）。
 */
constexpr uint16_t kMqttTlsPort = 8883;

/**
 * @brief HTTPS既定ポート。
 */
constexpr uint16_t kHttpsPort = 443;

/**
 * @brief MQTTトピック1階層の最大長。
 */
constexpr uint32_t kTopicLevelMaxLength = 64;

/**
 * @brief MQTTペイロード最大長（バイト）。
 */
constexpr uint32_t kMqttPayloadMaxLength = 1024;

/**
 * @brief HTTPS APIパス最大長。
 */
constexpr uint32_t kHttpPathMaxLength = 128;

}  // namespace appDefine
