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

/**
 * @brief タスク識別子。
 * @note [重要] タスク間通信の宛先/送信元を固定IDで管理する。
 */
enum class appTaskId : uint8_t {
  kUnknown = 0,
  kMain = 1,
  kWifi = 2,
  kMqtt = 3,
  kHttp = 4,
  kTcpip = 5,
  kOta = 6,
  kExternalDevice = 7,
  kDisplay = 8,
  kLed = 9,
  kInput = 10,
};

/**
 * @brief タスク間メッセージの要求内容（種別）。
 * @note [重要] 送受信の判定は本enumを基準に行う。
 */
enum class appMessageType : uint8_t {
  kUnknown = 0,
  kStartupRequest = 1,
  kStartupAck = 2,
  kHeartbeat = 3,
  kWifiInitRequest = 10,
  kWifiInitDone = 11,
  kMqttInitRequest = 20,
  kMqttInitDone = 21,
  kMqttPublishOnlineRequest = 22,
  kMqttPublishOnlineDone = 23,
  kTaskError = 255,
};

/**
 * @brief タスク横断の共有フラグ名。
 * @note [推奨] フラグ更新は対象タスク自身が行い、他タスクは原則参照のみとする。
 */
enum class appTaskFlagName : uint8_t {
  kUnknown = 0,
  kWifiInitialized = 1,
  kWifiConnected = 2,
  kMqttInitialized = 3,
  kMqttConnected = 4,
  kMqttOnlinePublished = 5,
  kHttpInitialized = 6,
  kOtaInProgress = 7,
  kExternalDeviceReady = 8,
  kDisplayReady = 9,
  kLedReady = 10,
  kInputReady = 11,
  kTaskErrorRaised = 12,
};

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

/**
 * @brief タスクIDの最大値。
 * @note [重要] 配列の静的確保サイズ計算に使用する。
 */
constexpr uint8_t kTaskIdMaxValue = static_cast<uint8_t>(appTaskId::kInput);

/**
 * @brief タスク管理スロット数（0番を含む）。
 */
constexpr uint8_t kTaskSlotCount = kTaskIdMaxValue + 1;

/**
 * @brief フラグ名の最大値。
 */
constexpr uint8_t kTaskFlagNameMaxValue = static_cast<uint8_t>(appTaskFlagName::kTaskErrorRaised);

/**
 * @brief フラグ管理スロット数（0番を含む）。
 */
constexpr uint8_t kTaskFlagSlotCount = kTaskFlagNameMaxValue + 1;

}  // namespace appDefine
