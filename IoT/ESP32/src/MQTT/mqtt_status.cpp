/**
 * @file mqtt_status.cpp
 * @brief MQTT statusメッセージのpayload生成と送信処理。
 * @details
 * - [重要] statusは起動通知用途を想定し、requestId/replyIdは空、noticeIdを採番する。
 * - [推奨] payloadは共通キー定義（common.h）経由で組み立てる。
 * - [制限] 詳細項目は将来のIF仕様に合わせて拡張する。
 */

#include "mqttMessages.h"

#include <WiFi.h>

#include "common.h"
#include "jsonService.h"
#include "log.h"

namespace {

bool createMacAddressText(String* macAddressTextOut) {
  if (macAddressTextOut == nullptr) {
    appLogError("mqtt::createMacAddressText failed. macAddressTextOut is null.");
    return false;
  }
  uint64_t efuseMac = ESP.getEfuseMac();
  char macAddressBuffer[18];
  snprintf(macAddressBuffer,
           sizeof(macAddressBuffer),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           static_cast<unsigned>((efuseMac >> 40) & 0xFF),
           static_cast<unsigned>((efuseMac >> 32) & 0xFF),
           static_cast<unsigned>((efuseMac >> 24) & 0xFF),
           static_cast<unsigned>((efuseMac >> 16) & 0xFF),
           static_cast<unsigned>((efuseMac >> 8) & 0xFF),
           static_cast<unsigned>(efuseMac & 0xFF));
  *macAddressTextOut = String(macAddressBuffer);
  return true;
}

}  // namespace

namespace mqtt {

bool buildMqttStatusPayload(const char* subName, const char* reservedArgument, String* payloadTextOut) {
  if (payloadTextOut == nullptr) {
    appLogError("mqtt::buildMqttStatusPayload failed. payloadTextOut is null.");
    return false;
  }
  if (reservedArgument == nullptr || strlen(reservedArgument) == 0) {
    appLogError("mqtt::buildMqttStatusPayload failed. reservedArgument is null or empty.");
    return false;
  }

  String macAddressText;
  if (!createMacAddressText(&macAddressText)) {
    return false;
  }

  String noticeIdText = String("notice-") + String(static_cast<unsigned long>(millis()));
  String wifiSsidText = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("(dummy-ssid)");
  String ipAddressText = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("0.0.0.0");
  long wifiSignalLevelValue = static_cast<long>(WiFi.RSSI());
  const char* selectedSubName = (subName == nullptr || strlen(subName) == 0) ? "" : subName;
  const char* dummyDeviceTime = "unknown";
  const char* dummyFirmwareVersion = "0.0.0-dev";
  const char* dummyDetail = "status publish from mqtt module";
  String payloadText = "{}";
  jsonService payloadJsonService;

  jsonKeyValueItem itemList[] = {
      {iotCommon::mqtt::jsonKey::status::kVersion, jsonValueType::kString, "1", 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kRequestId, jsonValueType::kString, "", 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kReplyId, jsonValueType::kString, "", 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kNoticeId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kDeviceId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kKind, jsonValueType::kString, "Notice", 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kMacAddr, jsonValueType::kString, macAddressText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kTimestamp, jsonValueType::kLong, nullptr, 0, static_cast<long>(millis()), false},
      {iotCommon::mqtt::jsonKey::status::kCommand, jsonValueType::kString, iotCommon::mqtt::jsonKey::status::kCommand, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kSub, jsonValueType::kString, selectedSubName, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kOnlineState, jsonValueType::kString, reservedArgument, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kStartUpTime, jsonValueType::kLong, nullptr, 0, static_cast<long>(millis()), false},
      {iotCommon::mqtt::jsonKey::status::kDeviceTime, jsonValueType::kString, dummyDeviceTime, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kFirmwareVersion, jsonValueType::kString, dummyFirmwareVersion, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kWifiSignalLevel, jsonValueType::kLong, nullptr, 0, wifiSignalLevelValue, false},
      {iotCommon::mqtt::jsonKey::status::kIpAddress, jsonValueType::kString, ipAddressText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kWifiSsid, jsonValueType::kString, wifiSsidText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::kDetail, jsonValueType::kString, dummyDetail, 0, 0, false},
  };

  bool setResult = payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]));
  if (!setResult) {
    appLogError("mqtt::buildMqttStatusPayload failed. setValuesByPath failed.");
    return false;
  }

  *payloadTextOut = payloadText;
  return true;
}

bool sendMqttStatus(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument) {
  if (mqttClientOut == nullptr || topicName == nullptr || strlen(topicName) == 0) {
    appLogError("mqtt::sendMqttStatus failed. mqttClientOut=%p topicName=%p", mqttClientOut, topicName);
    return false;
  }
  String payloadText;
  if (!buildMqttStatusPayload(subName, reservedArgument, &payloadText)) {
    return false;
  }
  bool publishResult = mqttClientOut->publish(topicName, payloadText.c_str(), true);
  if (!publishResult) {
    appLogError("mqtt::sendMqttStatus failed. topic=%s payloadLength=%ld",
                topicName,
                static_cast<long>(payloadText.length()));
    return false;
  }
  return true;
}

}  // namespace mqtt
