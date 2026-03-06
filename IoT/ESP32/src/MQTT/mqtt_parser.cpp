/**
 * @file mqtt_parser.cpp
 * @brief MQTT受信メッセージ解析処理。
 * @details
 * - [重要] サーバー要求/通知（call/get/set/network/status）の入口を1か所へ集約する。
 * - [推奨] 解析対象は共通キー（DstID/SrcID/sub）を優先する。
 */

#include "mqttMessages.h"

#include "common.h"
#include "jsonService.h"
#include "log.h"

namespace {

mqtt::mqttIncomingType determineMessageType(const String& payloadText, const char* topicName) {
  if (payloadText.indexOf(String("\"") + iotCommon::mqtt::jsonKey::status::kCommand + "\"") >= 0) {
    return mqtt::mqttIncomingType::kStatus;
  }
  if (payloadText.indexOf(String("\"") + iotCommon::mqtt::jsonKey::call::kCommand + "\"") >= 0) {
    return mqtt::mqttIncomingType::kCall;
  }
  if (payloadText.indexOf(String("\"") + iotCommon::mqtt::jsonKey::get::kCommand + "\"") >= 0) {
    return mqtt::mqttIncomingType::kGet;
  }
  if (payloadText.indexOf(String("\"") + iotCommon::mqtt::jsonKey::set::kCommand + "\"") >= 0) {
    return mqtt::mqttIncomingType::kSet;
  }
  if (topicName != nullptr && strstr(topicName, "network") != nullptr) {
    return mqtt::mqttIncomingType::kNetwork;
  }
  return mqtt::mqttIncomingType::kUnknown;
}

}  // namespace

namespace mqtt {

bool parseMqttIncomingMessage(const char* topicName, const char* payloadText, mqttIncomingMessage* parsedMessageOut) {
  if (payloadText == nullptr || parsedMessageOut == nullptr) {
    appLogError("mqtt::parseMqttIncomingMessage failed. payloadText=%p parsedMessageOut=%p",
                payloadText,
                parsedMessageOut);
    return false;
  }

  String payloadString = String(payloadText);
  jsonService payloadJsonService;
  mqttIncomingMessage parsedMessage{};
  parsedMessage.rawPayload = payloadString;
  parsedMessage.messageType = determineMessageType(payloadString, topicName);

  String subName;
  if (payloadJsonService.getValueByPath(payloadString, iotCommon::mqtt::jsonKey::status::kSub, &subName)) {
    parsedMessage.subName = subName;
  } else {
    parsedMessage.subName = "";
  }

  String dstId;
  if (payloadJsonService.getValueByPath(payloadString, iotCommon::mqtt::jsonKey::status::kDstId, &dstId)) {
    parsedMessage.dstId = dstId;
  } else {
    parsedMessage.dstId = "";
  }

  String srcId;
  if (payloadJsonService.getValueByPath(payloadString, iotCommon::mqtt::jsonKey::status::kSrcId, &srcId)) {
    parsedMessage.srcId = srcId;
  } else {
    parsedMessage.srcId = "";
  }

  switch (parsedMessage.messageType) {
    case mqttIncomingType::kStatus:
      parsedMessage.commandName = iotCommon::mqtt::jsonKey::status::kCommand;
      break;
    case mqttIncomingType::kCall:
      parsedMessage.commandName = iotCommon::mqtt::jsonKey::call::kCommand;
      break;
    case mqttIncomingType::kGet:
      parsedMessage.commandName = iotCommon::mqtt::jsonKey::get::kCommand;
      break;
    case mqttIncomingType::kSet:
      parsedMessage.commandName = iotCommon::mqtt::jsonKey::set::kCommand;
      break;
    case mqttIncomingType::kNetwork:
      parsedMessage.commandName = "network";
      break;
    default:
      parsedMessage.commandName = "unknown";
      break;
  }

  *parsedMessageOut = parsedMessage;
  return true;
}

}  // namespace mqtt
