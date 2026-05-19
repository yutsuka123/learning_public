/**
 * @file mqttProtocol.cpp
 * @brief MQTT 通信プロトコル定義の実装。
 *
 * @details
 * # 目的
 * - ESP32 と LocalServer の間で交換する MQTT メッセージの **トピック構築・payload 生成・
 *   payload パース** を一元化する。
 * - JSON エスケープと共通フォーマット処理もここで提供する。
 *
 * # 提供する関数
 * - `buildTopicDeviceBoot(publicId)`：起動通知トピック `device/<publicId>/boot`
 * - `buildTopicWifiUpdate(publicId)`：Wi-Fi 更新トピック（**現行未使用・参考実装**）
 * - `buildTopicWifiConfirm(publicId)`：Wi-Fi 確定トピック（**現行未使用・参考実装**）
 * - `buildBootNotifyPayload`：起動通知 JSON 生成
 * - `buildWifiUpdateResultPayload`：Wi-Fi 更新結果 JSON 生成
 * - `parseCommandFromPayload`：受信 JSON からコマンド種別判別
 * - `parseWifiUpdatePayload`：Wi-Fi 更新 payload パース
 *
 * # トピック設計の正本
 * - 実際の active path は `esp32lab/<kind>/<sub>/<name>`（kind = `notice` / `set` / `get` / `call` / `network` / `status`）。
 *   詳細は `MQTTコマンド仕様書.md` §2.1 と `mqtt.cpp:3290-3311 subscribe 設定`。
 * - `device/<publicId>/wifi/update` 系の helper は `009-0014` で**不採用クローズ**となったが、
 *   参考実装として残置（誤利用しないよう `mqttProtocol.h` の Doxygen にコメント付き）。
 *
 * # JSON エスケープ
 * - `escapeJsonString`：`\\`、`"`、`\n`、`\r`、`\t` を JSON 規約に従ってエスケープ。
 *
 * # 関連
 * - 入口集約：`MQTT/mqtt_parser.cpp`
 * - MQTT 認証・トピック設計：`MQTTコマンド仕様書.md` / `Mosquitto_TLS_IDパスワード設定手順書.md`
 * - 統合ガイド：`セキュア全般ノウハウ_設計から実装まで.md` §9
 * - マッピング表：`設計書実装マッピング表.md` §4.4
 */

#include "mqttProtocol.h"

namespace {

String escapeJsonString(const String& value) {
  String escapedValue;
  escapedValue.reserve(value.length() + 8);
  for (uint32_t index = 0; index < value.length(); ++index) {
    const char currentChar = value[index];
    if (currentChar == '\\' || currentChar == '"') {
      escapedValue += '\\';
      escapedValue += currentChar;
      continue;
    }
    if (currentChar == '\n') {
      escapedValue += "\\n";
      continue;
    }
    if (currentChar == '\r') {
      escapedValue += "\\r";
      continue;
    }
    if (currentChar == '\t') {
      escapedValue += "\\t";
      continue;
    }
    escapedValue += currentChar;
  }
  return escapedValue;
}

bool extractJsonStringValue(const String& payload, const String& key, String* outValue) {
  if (outValue == nullptr) {
    return false;
  }

  const String keyPattern = "\"" + key + "\":\"";
  const int32_t keyIndex = payload.indexOf(keyPattern);
  if (keyIndex < 0) {
    return false;
  }

  const int32_t valueStartIndex = keyIndex + static_cast<int32_t>(keyPattern.length());
  const int32_t valueEndIndex = payload.indexOf("\"", valueStartIndex);
  if (valueEndIndex < 0 || valueEndIndex < valueStartIndex) {
    return false;
  }

  *outValue = payload.substring(valueStartIndex, valueEndIndex);
  return true;
}

}  // namespace

namespace mqttProtocol {

String buildTopicDeviceBoot(const String& publicId) {
  return "device/" + publicId + "/boot";
}

String buildTopicWifiUpdate(const String& publicId) {
  return "device/" + publicId + "/wifi/update";
}

String buildTopicWifiConfirm(const String& publicId) {
  return "device/" + publicId + "/wifi/confirm";
}

String buildBootNotifyPayload(
    iotCommon::deviceRuntimeStateType state, const String& firmwareVersion, uint32_t bootCount) {
  String payload;
  payload.reserve(256);
  payload += "{";
  payload += "\"protocolVersion\":\"";
  payload += iotCommon::kProtocolVersion;
  payload += "\",";
  payload += "\"command\":\"";
  payload += iotCommon::toCommandName(iotCommon::commandType::kDeviceBootNotify);
  payload += "\",";
  payload += "\"state\":\"";
  payload += iotCommon::toDeviceRuntimeStateName(state);
  payload += "\",";
  payload += "\"firmwareVersion\":\"";
  payload += escapeJsonString(firmwareVersion);
  payload += "\",";
  payload += "\"bootCount\":";
  payload += String(bootCount);
  payload += "}";
  return payload;
}

String buildWifiUpdateResultPayload(
    bool isSuccess, const String& reason, const String& transactionId) {
  String payload;
  payload.reserve(256);
  payload += "{";
  payload += "\"protocolVersion\":\"";
  payload += iotCommon::kProtocolVersion;
  payload += "\",";
  payload += "\"command\":\"";
  payload += iotCommon::toCommandName(iotCommon::commandType::kWifiConfigConfirm);
  payload += "\",";
  payload += "\"transactionId\":\"";
  payload += escapeJsonString(transactionId);
  payload += "\",";
  payload += "\"result\":\"";
  payload += isSuccess ? "success" : "error";
  payload += "\",";
  payload += "\"reason\":\"";
  payload += escapeJsonString(reason);
  payload += "\"";
  payload += "}";
  return payload;
}

mqttCommandType parseCommandFromPayload(const String& payload) {
  String commandName;
  if (!extractJsonStringValue(payload, "command", &commandName)) {
    return mqttCommandType::kUnknown;
  }
  if (commandName == "deviceBootNotify") {
    return mqttCommandType::kDeviceBootNotify;
  }
  if (commandName == "ledSet") {
    return mqttCommandType::kLedSet;
  }
  if (commandName == "wifiConfigUpdate") {
    return mqttCommandType::kWifiConfigUpdate;
  }
  if (commandName == "wifiConfigConfirm") {
    return mqttCommandType::kWifiConfigConfirm;
  }
  return mqttCommandType::kUnknown;
}

bool parseWifiUpdatePayload(const String& payload, wifiUpdatePayloadModel* outModel) {
  if (outModel == nullptr) {
    return false;
  }

  wifiUpdatePayloadModel parsedModel;
  if (!extractJsonStringValue(payload, "transactionId", &parsedModel.transactionId)) {
    return false;
  }
  if (!extractJsonStringValue(payload, "encryptedDataBase64", &parsedModel.encryptedDataBase64)) {
    return false;
  }
  if (!extractJsonStringValue(payload, "nonceBase64", &parsedModel.nonceBase64)) {
    return false;
  }
  if (!extractJsonStringValue(payload, "tagBase64", &parsedModel.tagBase64)) {
    return false;
  }

  *outModel = parsedModel;
  return true;
}

const char* toCommandName(mqttCommandType command) {
  switch (command) {
    case mqttCommandType::kDeviceBootNotify:
      return "deviceBootNotify";
    case mqttCommandType::kLedSet:
      return "ledSet";
    case mqttCommandType::kWifiConfigUpdate:
      return "wifiConfigUpdate";
    case mqttCommandType::kWifiConfigConfirm:
      return "wifiConfigConfirm";
    default:
      return "unknown";
  }
}

}  // namespace mqttProtocol
