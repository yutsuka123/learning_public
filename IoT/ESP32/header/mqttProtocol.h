/**
 * @file mqttProtocol.h
 * @brief MQTT通信で利用するトピック、コマンド、JSON本文の定義。
 * @details
 * - MQTT通信仕様を1モジュールに集約し、保守性を高める。
 * - 文字列ベースJSONを生成・解析する最小実装を提供する。
 * - 機密値（平文鍵、平文パスワード）をログ・戻り値に含めない。
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "common.h"

namespace mqttProtocol {

/**
 * @brief MQTTコマンド種別。
 */
enum class mqttCommandType : uint16_t {
  kUnknown = 0,
  kDeviceBootNotify = 1001,
  kLedSet = 1002,
  kWifiConfigUpdate = 1101,
  kWifiConfigConfirm = 1102,
};

/**
 * @brief Wi-Fi設定更新要求のペイロードモデル。
 */
struct wifiUpdatePayloadModel {
  String transactionId;
  String encryptedDataBase64;
  String nonceBase64;
  String tagBase64;
};

/**
 * @brief 起動通知トピックを生成する。
 * @param publicId 公開用デバイスID。
 * @return MQTTトピック。
 */
String buildTopicDeviceBoot(const String& publicId);

/**
 * @brief Wi-Fi設定更新要求トピックを生成する。
 * @param publicId 公開用デバイスID。
 * @return MQTTトピック。
 */
String buildTopicWifiUpdate(const String& publicId);

/**
 * @brief Wi-Fi設定更新確認トピックを生成する。
 * @param publicId 公開用デバイスID。
 * @return MQTTトピック。
 */
String buildTopicWifiConfirm(const String& publicId);

/**
 * @brief 起動通知JSON本文を生成する。
 * @param state デバイス状態。
 * @param firmwareVersion ファームウェアバージョン。
 * @param bootCount ブート回数。
 * @return JSON文字列。
 */
String buildBootNotifyPayload(
    iotCommon::deviceRuntimeStateType state, const String& firmwareVersion, uint32_t bootCount);

/**
 * @brief Wi-Fi設定更新結果JSON本文を生成する。
 * @param isSuccess 処理結果。
 * @param reason 理由（エラー/成功補足）。
 * @param transactionId トランザクションID。
 * @return JSON文字列。
 */
String buildWifiUpdateResultPayload(
    bool isSuccess, const String& reason, const String& transactionId);

/**
 * @brief JSON本文からコマンド種別を抽出する。
 * @param payload JSON文字列。
 * @return 抽出したコマンド種別。
 */
mqttCommandType parseCommandFromPayload(const String& payload);

/**
 * @brief Wi-Fi設定更新要求JSON本文を解析する。
 * @param payload JSON文字列。
 * @param outModel 解析結果出力先。
 * @return 解析成功時true。
 */
bool parseWifiUpdatePayload(const String& payload, wifiUpdatePayloadModel* outModel);

/**
 * @brief mqttCommandType を文字列化する。
 * @param command MQTTコマンド。
 * @return コマンド名文字列。
 */
const char* toCommandName(mqttCommandType command);

}  // namespace mqttProtocol
