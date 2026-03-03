/**
 * @file mqtt_get.cpp
 * @brief MQTT getメッセージ送信処理。
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
      {iotCommon::mqtt::jsonKey::get::kRequestId, jsonValueType::kString, "", 0, 0, false},
      {iotCommon::mqtt::jsonKey::get::kReplyId, jsonValueType::kString, "", 0, 0, false},
      {iotCommon::mqtt::jsonKey::get::kNoticeId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
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
