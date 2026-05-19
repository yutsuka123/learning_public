/**
 * @file mqtt_get.cpp
 * @brief MQTT `get` kind メッセージの応答処理（ESP32 → LocalServer notice 応答）。
 *
 * @details
 * # 目的
 * - LocalServer からの `get/<sub>/<receiverName>`（状態取得要求）に対する **notice 応答** を
 *   ESP32 → LocalServer 方向で送信する。
 *
 * # 主な対象
 * - `get/trh`：温湿度気圧（BME280）取得 → `notice/trh` で応答
 * - `get/relay`：リレー状態取得 → `notice/relay`
 * - `get/led`：LED 状態取得 → `notice/led`
 * - `get/button`：ボタン状態取得 → `notice/button`
 * - `get/gpio`：GPIO 入出力状態取得 → `notice/gpio`
 * - `get/log`：ログ取得（LittleFS `/logs/`）→ `notice/log`
 *
 * # 関連
 * - 入口集約：`mqtt_parser.cpp`
 * - 詳細仕様：`MQTTコマンド仕様書.md` §3.2.1
 * - 物理 I/O 実装：`externalDevice.cpp`、`i2c.cpp`（BME280）、`led.cpp`、`input.cpp`
 */

#include "mqttMessages.h"

#include "common.h"
#include "jsonService.h"
#include "log.h"

namespace mqtt {

bool sendMqttGet(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument) {
  if (mqttClientOut == nullptr || topicName == nullptr || strlen(topicName) == 0) {
    appLogError("mqtt::sendMqttGet failed. mqttClientOut=%p topicName=%p", mqttClientOut, topicName);
    return false;
  }
  const char* selectedSubName = (subName == nullptr) ? "" : subName;
  const char* selectedReserved = (reservedArgument == nullptr) ? "" : reservedArgument;
  String payloadText = "{}";
  String noticeIdText = String("notice-") + String(static_cast<unsigned long>(millis()));
  jsonService payloadJsonService;
  jsonKeyValueItem itemList[] = {
      {iotCommon::mqtt::jsonKey::get::kVersion, jsonValueType::kString, "1", 0, 0, false},
      {iotCommon::mqtt::jsonKey::get::kDstId, jsonValueType::kString, "all", 0, 0, false},
      {iotCommon::mqtt::jsonKey::get::kSrcId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::get::kKind, jsonValueType::kString, "Notice", 0, 0, false},
      {iotCommon::mqtt::jsonKey::get::kCommand, jsonValueType::kString, iotCommon::mqtt::jsonKey::get::kCommand, 0, 0, false},
      {iotCommon::mqtt::jsonKey::get::kSub, jsonValueType::kString, selectedSubName, 0, 0, false},
      {iotCommon::mqtt::jsonKey::get::kDetail, jsonValueType::kString, selectedReserved, 0, 0, false},
  };
  if (!payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]))) {
    appLogError("mqtt::sendMqttGet failed. setValuesByPath failed.");
    return false;
  }
  return mqttClientOut->publish(topicName, payloadText.c_str(), true);
}

}  // namespace mqtt
