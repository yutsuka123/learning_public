/**
 * @file mqtt_set.cpp
 * @brief MQTT setメッセージ送信処理。
 */

#include "mqttMessages.h"

#include "common.h"
#include "jsonService.h"
#include "log.h"

namespace mqtt {

bool sendMqttSet(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument) {
  if (mqttClientOut == nullptr || topicName == nullptr || strlen(topicName) == 0) {
    appLogError("mqtt::sendMqttSet failed. mqttClientOut=%p topicName=%p", mqttClientOut, topicName);
    return false;
  }
  const char* selectedSubName = (subName == nullptr) ? "" : subName;
  const char* selectedReserved = (reservedArgument == nullptr) ? "" : reservedArgument;
  String payloadText = "{}";
  String noticeIdText = String("notice-") + String(static_cast<unsigned long>(millis()));
  jsonService payloadJsonService;
  jsonKeyValueItem itemList[] = {
      {iotCommon::mqtt::jsonKey::set::kVersion, jsonValueType::kString, "1", 0, 0, false},
      {iotCommon::mqtt::jsonKey::set::kDstId, jsonValueType::kString, "all", 0, 0, false},
      {iotCommon::mqtt::jsonKey::set::kSrcId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::set::kKind, jsonValueType::kString, "Notice", 0, 0, false},
      {iotCommon::mqtt::jsonKey::set::kCommand, jsonValueType::kString, iotCommon::mqtt::jsonKey::set::kCommand, 0, 0, false},
      {iotCommon::mqtt::jsonKey::set::kSub, jsonValueType::kString, selectedSubName, 0, 0, false},
      {iotCommon::mqtt::jsonKey::set::kDetail, jsonValueType::kString, selectedReserved, 0, 0, false},
  };
  if (!payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]))) {
    appLogError("mqtt::sendMqttSet failed. setValuesByPath failed.");
    return false;
  }
  return mqttClientOut->publish(topicName, payloadText.c_str(), true);
}

}  // namespace mqtt
