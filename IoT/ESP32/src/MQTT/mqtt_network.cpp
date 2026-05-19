/**
 * @file mqtt_network.cpp
 * @brief MQTT `network` kind メッセージの送信処理（ESP32 → LocalServer）。
 *
 * @details
 * # 目的
 * - ESP32 → LocalServer 方向の **network 通知**（Wi-Fi/MQTT/HTTPS/NTP の接続状態や設定変更結果）を
 *   `esp32lab/network/<sub>/<destination>` トピックで送信する。
 * - 受信側は LocalServer（SecretCore 経由で復号・正規化）。
 *
 * # トピック構造
 * - 出力例：`esp32lab/network/Notice/all`、`esp32lab/network/Reply/<receiverName>`
 * - 詳細は `MQTTコマンド仕様書.md` §2.1 / §3.3
 *
 * # 関連
 * - 入口集約：`mqtt_parser.cpp`（kind=`network` の受信側パース）
 * - 設計：`セキュア全般ノウハウ_設計から実装まで.md` §9（MQTT 認証）／`設計書実装マッピング表.md` §4
 *
 * # 注意
 * - 高リスク command（OTA 開始・重要設定変更）は本ファイルではなく、HMAC 署名付きで送信する。
 */

#include "mqttMessages.h"

#include "common.h"
#include "jsonService.h"
#include "log.h"

namespace mqtt {

bool sendMqttNetwork(PubSubClient* mqttClientOut, const char* topicName, const char* subName, const char* reservedArgument) {
  if (mqttClientOut == nullptr || topicName == nullptr || strlen(topicName) == 0) {
    appLogError("mqtt::sendMqttNetwork failed. mqttClientOut=%p topicName=%p", mqttClientOut, topicName);
    return false;
  }
  const char* selectedSubName = (subName == nullptr) ? "" : subName;
  const char* selectedReserved = (reservedArgument == nullptr) ? "" : reservedArgument;
  String payloadText = "{}";
  String noticeIdText = String("notice-") + String(static_cast<unsigned long>(millis()));
  jsonService payloadJsonService;
  jsonKeyValueItem itemList[] = {
      {iotCommon::mqtt::jsonKey::network::kVersion, jsonValueType::kString, "1", 0, 0, false},
      {iotCommon::mqtt::jsonKey::network::kDstId, jsonValueType::kString, "all", 0, 0, false},
      {iotCommon::mqtt::jsonKey::network::kSrcId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::network::kKind, jsonValueType::kString, "Notice", 0, 0, false},
      {iotCommon::mqtt::jsonKey::network::kWifiSsid, jsonValueType::kString, selectedSubName, 0, 0, false},
      {iotCommon::mqtt::jsonKey::kDetail, jsonValueType::kString, selectedReserved, 0, 0, false},
  };
  if (!payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]))) {
    appLogError("mqtt::sendMqttNetwork failed. setValuesByPath failed.");
    return false;
  }
  return mqttClientOut->publish(topicName, payloadText.c_str(), true);
}

}  // namespace mqtt
