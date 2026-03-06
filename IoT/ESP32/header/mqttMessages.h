/**
 * @file mqttMessages.h
 * @brief MQTTメッセージ送信関数の宣言。
 * @details
 * - [重要] メッセージ種別ごとに送信関数を分離し、mqtt.cppからは1関数呼び出しで送信できるようにする。
 * - [推奨] topicは呼び出し元で決定し、payloadは各関数内で組み立てる。
 * - [制限] 現時点ではTLSや複雑なargs構造には依存せず、フラットpayloadを基本とする。
 */

#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

namespace mqtt {

/**
 * @brief 受信メッセージ種別。
 */
enum class mqttIncomingType : uint8_t {
  kUnknown = 0,
  kStatus = 1,
  kCall = 2,
  kGet = 3,
  kSet = 4,
  kNetwork = 5,
};

/**
 * @brief MQTT受信メッセージの解析結果。
 */
struct mqttIncomingMessage {
  mqttIncomingType messageType;
  String commandName;
  String subName;
  String dstId;
  String srcId;
  String rawPayload;
};

/**
 * @brief status payloadを生成する。
 * @param subName サブ種別（例: boot）。
 * @param reservedArgument 予備引数（statusではonlineStateとして使用）。
 * @param startupCpuMillis mainTaskEntry開始時のCPU時刻(ms)。
 * @param payloadTextOut 出力先（null不可）。
 * @return 成功時true、失敗時false。
 */
bool buildMqttStatusPayload(const char* subName,
                            const char* reservedArgument,
                            uint32_t startupCpuMillis,
                            String* payloadTextOut);

/**
 * @brief statusメッセージを送信する。
 * @param mqttClientOut 送信先クライアント（null不可）。
 * @param topicName 送信topic（null不可）。
 * @param subName サブ種別。
 * @param reservedArgument 予備引数（statusではonlineStateとして使用）。
 * @param startupCpuMillis mainTaskEntry開始時のCPU時刻(ms)。
 * @return 成功時true、失敗時false。
 */
bool sendMqttStatus(PubSubClient* mqttClientOut,
                    const char* topicName,
                    const char* subName,
                    const char* reservedArgument,
                    uint32_t startupCpuMillis);

/**
 * @brief callメッセージを送信する。
 * @param mqttClientOut 送信先クライアント（null不可）。
 * @param topicName 送信topic（null不可）。
 * @param subName サブ種別。
 * @param reservedArgument 予備引数。
 * @return 成功時true、失敗時false。
 */
bool sendMqttCall(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument);

/**
 * @brief getメッセージを送信する。
 * @param mqttClientOut 送信先クライアント（null不可）。
 * @param topicName 送信topic（null不可）。
 * @param subName サブ種別。
 * @param reservedArgument 予備引数。
 * @return 成功時true、失敗時false。
 */
bool sendMqttGet(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument);

/**
 * @brief setメッセージを送信する。
 * @param mqttClientOut 送信先クライアント（null不可）。
 * @param topicName 送信topic（null不可）。
 * @param subName サブ種別。
 * @param reservedArgument 予備引数。
 * @return 成功時true、失敗時false。
 */
bool sendMqttSet(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument);

/**
 * @brief networkメッセージを送信する。
 * @param mqttClientOut 送信先クライアント（null不可）。
 * @param topicName 送信topic（null不可）。
 * @param subName サブ種別。
 * @param reservedArgument 予備引数。
 * @return 成功時true、失敗時false。
 */
bool sendMqttNetwork(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument);

/**
 * @brief MQTT受信payloadを解析して種別/共通項目を抽出する。
 * @param topicName 受信トピック名（null可）。
 * @param payloadText 受信payload文字列（null不可）。
 * @param parsedMessageOut 解析結果出力先（null不可）。
 * @return 成功時true、失敗時false。
 */
bool parseMqttIncomingMessage(const char* topicName, const char* payloadText, mqttIncomingMessage* parsedMessageOut);

}  // namespace mqtt
