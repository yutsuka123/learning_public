/**
 * @file mqtt_set.cpp
 * @brief MQTT `set` kind メッセージの送信処理（ESP32 → LocalServer 応答）。
 *
 * @details
 * # 目的
 * - LocalServer から受信した `set/<sub>/<receiverName>`（リレー切替・LED 制御・GPIO 制御・
 *   keyDeviceSet / fileLogSet 等）の **実行結果通知** を ESP32 → LocalServer 方向で送信する。
 *
 * # 高リスク command（HMAC 署名検証）
 * - `set/keyDeviceSet`：k-device の置き換え
 * - `set/fileLogSet`：ファイルログ設定変更
 * - これらは ESP32 側受信時に k-device で HMAC-SHA256 検証を行い、不一致なら拒否する。
 * - 詳細は `MQTTコマンド仕様書.md` §3.1.1 セキュリティ拡張ヘッダ
 *
 * # 通常 command
 * - `set/relay` / `set/led_ON` / `set/led_OFF` / `set/led_Blink` / `set/gpio_H` / `set/gpio_L`
 * - notice 応答は同 sub 名で送り返す（要求と応答の対応関係）。
 *
 * # 関連
 * - 入口集約：`mqtt_parser.cpp`
 * - 設計：`セキュア全般ノウハウ_設計から実装まで.md` §11（HMAC 署名）／`設計書実装マッピング表.md` §2.2
 * - 試験：`7052`（重要設定変更 HMAC、OK 2026-05-10）
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
