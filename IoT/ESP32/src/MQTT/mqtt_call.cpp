/**
 * @file mqtt_call.cpp
 * @brief MQTT `call` kind メッセージの応答処理（ESP32 → LocalServer notice 応答）。
 *
 * @details
 * # 目的
 * - LocalServer からの `call/<sub>/<receiverName>`（命令実行要求）に対する **notice 応答** を
 *   ESP32 → LocalServer 方向で送信する。
 *
 * # 主な対象
 * - `call/restart`：再起動命令 → 応答後 `esp_restart()`
 * - `call/maintenance`：メンテナンスモード遷移命令 → `requestMaintenanceModeOnNextBoot()` → 再起動
 * - `call/fileSyncPlan`：差分ファイル同期計画通知 → ESP32 側で受信し plan に従って `notice/fileSyncStatus` で進捗報告
 *
 * # 関連
 * - 入口集約：`mqtt_parser.cpp`
 * - 詳細仕様：`MQTTコマンド仕様書.md` §3.2.1
 * - メンテナンスモード：`maintenanceMode.cpp`（フラグ管理）
 * - fileSync：`filesystem.cpp`（LittleFS 差分更新）
 */

#include "mqttMessages.h"

#include "common.h"
#include "jsonService.h"
#include "log.h"

namespace mqtt {

bool sendMqttCall(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument) {
  if (mqttClientOut == nullptr || topicName == nullptr || strlen(topicName) == 0) {
    appLogError("mqtt::sendMqttCall failed. mqttClientOut=%p topicName=%p", mqttClientOut, topicName);
    return false;
  }
  const char* selectedSubName = (subName == nullptr) ? "" : subName;
  const char* selectedReserved = (reservedArgument == nullptr) ? "" : reservedArgument;
  String payloadText = "{}";
  String noticeIdText = String("notice-") + String(static_cast<unsigned long>(millis()));
  jsonService payloadJsonService;
  jsonKeyValueItem itemList[] = {
      {iotCommon::mqtt::jsonKey::call::kVersion, jsonValueType::kString, "1", 0, 0, false},
      {iotCommon::mqtt::jsonKey::call::kDstId, jsonValueType::kString, "all", 0, 0, false},
      {iotCommon::mqtt::jsonKey::call::kSrcId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::call::kKind, jsonValueType::kString, "Notice", 0, 0, false},
      {iotCommon::mqtt::jsonKey::call::kCommand, jsonValueType::kString, iotCommon::mqtt::jsonKey::call::kCommand, 0, 0, false},
      {iotCommon::mqtt::jsonKey::call::kSub, jsonValueType::kString, selectedSubName, 0, 0, false},
      {iotCommon::mqtt::jsonKey::call::kDetail, jsonValueType::kString, selectedReserved, 0, 0, false},
  };
  if (!payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]))) {
    appLogError("mqtt::sendMqttCall failed. setValuesByPath failed.");
    return false;
  }
  return mqttClientOut->publish(topicName, payloadText.c_str(), true);
}

}  // namespace mqtt
