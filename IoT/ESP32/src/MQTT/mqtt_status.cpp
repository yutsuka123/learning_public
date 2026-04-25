/**
 * @file mqtt_status.cpp
 * @brief MQTT statusメッセージのpayload生成と送信処理。
 * @details
 * - [重要] statusは起動通知用途を想定し、DstID/SrcIDを設定して送信する。
 * - [推奨] payloadは共通キー定義（common.h）経由で組み立てる。
 * - [制限] 詳細項目は将来のIF仕様に合わせて拡張する。
 */

#include "mqttMessages.h"

#include <sys/time.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "common.h"
#include "firmwareInfo.h"
#include "jsonService.h"
#include "log.h"
#include "version.h"

namespace {
constexpr int64_t minimumValidUtcEpochMillis = 1577836800000LL; // 2020-01-01T00:00:00.000Z
/** @brief 直近id採番時のタイムスタンプ（YYYYMMDDHHMMSSmmm）。 */
String lastNoticeIdTimestampText = "";
/** @brief 同一タイムスタンプ内の通し番号。 */
uint16_t noticeIdSequenceNumber = 0;

/**
 * @brief ESP32のeFuse由来Base MACを文字列化する。
 * @param macAddressTextOut 出力先文字列ポインタ。
 * @return 変換成功時true、失敗時false。
 */
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

/**
 * @brief ESP32のeFuse由来Base MACを区切りなし16進文字列へ変換する。
 * @param compactMacTextOut 出力先文字列ポインタ。
 * @return 変換成功時true、失敗時false。
 */
bool createCompactMacAddressText(String* compactMacTextOut) {
  if (compactMacTextOut == nullptr) {
    appLogError("mqtt::createCompactMacAddressText failed. compactMacTextOut is null.");
    return false;
  }
  uint64_t efuseMac = ESP.getEfuseMac();
  char compactMacBuffer[13] = {};
  int writtenLength = snprintf(compactMacBuffer,
                               sizeof(compactMacBuffer),
                               "%02X%02X%02X%02X%02X%02X",
                               static_cast<unsigned>((efuseMac >> 40) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 32) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 24) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 16) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 8) & 0xFF),
                               static_cast<unsigned>(efuseMac & 0xFF));
  if (writtenLength <= 0 || writtenLength >= static_cast<int>(sizeof(compactMacBuffer))) {
    appLogError("mqtt::createCompactMacAddressText failed. snprintf overflow. writtenLength=%d", writtenLength);
    return false;
  }
  *compactMacTextOut = String(compactMacBuffer);
  return true;
}

/**
 * @brief 送信者名（SrcID）を解決する。
 * @param senderNameTextOut 出力先文字列ポインタ。
 * @return 解決成功時true、失敗時false。
 * @details
 * - [重要] デバイス名（WiFiホスト名）が設定済みならその値を優先する。
 * - [重要] 未設定時は `IoT_<BaseMacNoColon>` 形式を使用する。
 */
bool resolveSenderNameText(String* senderNameTextOut) {
  if (senderNameTextOut == nullptr) {
    appLogError("mqtt::resolveSenderNameText failed. senderNameTextOut is null.");
    return false;
  }
  // [重要] SrcIDは運用仕様に合わせて常に IoT_<BaseMacNoColon> 形式を使用する。
  String compactMacText;
  if (!createCompactMacAddressText(&compactMacText)) {
    return false;
  }
  *senderNameTextOut = String("IoT_") + compactMacText;
  return true;
}

/**
 * @brief UTCエポックミリ秒をid用タイムスタンプ文字列へ変換する。
 * @param utcEpochMillis UTCエポックミリ秒。
 * @param compactTimestampTextOut 出力先（YYYYMMDDHHMMSSmmm）。
 * @return 変換成功時true、失敗時false。
 */
bool formatCompactTimestampFromEpochMillis(int64_t utcEpochMillis, String* compactTimestampTextOut) {
  if (compactTimestampTextOut == nullptr) {
    appLogError("mqtt::formatCompactTimestampFromEpochMillis failed. compactTimestampTextOut is null.");
    return false;
  }
  if (utcEpochMillis < 0) {
    utcEpochMillis = 0;
  }

  const time_t epochSeconds = static_cast<time_t>(utcEpochMillis / 1000);
  const int64_t millisecondPart = utcEpochMillis % 1000;
  struct tm utcTimeInfo {};
  if (gmtime_r(&epochSeconds, &utcTimeInfo) == nullptr) {
    appLogError("mqtt::formatCompactTimestampFromEpochMillis failed. gmtime_r returned null. utcEpochMillis=%lld",
                static_cast<long long>(utcEpochMillis));
    return false;
  }

  char dateTimeBuffer[20] = {};
  const size_t writtenLength = strftime(dateTimeBuffer, sizeof(dateTimeBuffer), "%Y%m%d%H%M%S", &utcTimeInfo);
  if (writtenLength == 0) {
    appLogError("mqtt::formatCompactTimestampFromEpochMillis failed. strftime returned 0. utcEpochMillis=%lld",
                static_cast<long long>(utcEpochMillis));
    return false;
  }

  char compactBuffer[24] = {};
  const int printLength = snprintf(compactBuffer,
                                   sizeof(compactBuffer),
                                   "%s%03lld",
                                   dateTimeBuffer,
                                   static_cast<long long>(millisecondPart));
  if (printLength <= 0 || printLength >= static_cast<int>(sizeof(compactBuffer))) {
    appLogError("mqtt::formatCompactTimestampFromEpochMillis failed. snprintf overflow. printLength=%d",
                printLength);
    return false;
  }

  *compactTimestampTextOut = String(compactBuffer);
  return true;
}

/**
 * @brief status通知用のidを生成する。
 * @param utcEpochMillis UTCエポックミリ秒。
 * @param noticeIdTextOut 出力先（YYYYMMDDHHMMSSmmm-001）。
 * @return 生成成功時true、失敗時false。
 */
bool createNoticeIdText(int64_t utcEpochMillis, String* noticeIdTextOut) {
  if (noticeIdTextOut == nullptr) {
    appLogError("mqtt::createNoticeIdText failed. noticeIdTextOut is null.");
    return false;
  }

  String timestampText;
  if (!formatCompactTimestampFromEpochMillis(utcEpochMillis, &timestampText)) {
    return false;
  }

  if (lastNoticeIdTimestampText == timestampText) {
    ++noticeIdSequenceNumber;
    if (noticeIdSequenceNumber > 999) {
      appLogWarn("mqtt::createNoticeIdText sequence overflow. reset to 1. timestamp=%s", timestampText.c_str());
      noticeIdSequenceNumber = 1;
    }
  } else {
    lastNoticeIdTimestampText = timestampText;
    noticeIdSequenceNumber = 1;
  }

  char idBuffer[32] = {};
  const int printLength = snprintf(idBuffer,
                                   sizeof(idBuffer),
                                   "%s-%03u",
                                   timestampText.c_str(),
                                   static_cast<unsigned>(noticeIdSequenceNumber));
  if (printLength <= 0 || printLength >= static_cast<int>(sizeof(idBuffer))) {
    appLogError("mqtt::createNoticeIdText failed. snprintf overflow. printLength=%d", printLength);
    return false;
  }

  *noticeIdTextOut = String(idBuffer);
  return true;
}

/**
 * @brief ネットワークアダプタMACを文字列化する。
 * @param networkMacAddressTextOut 出力先文字列ポインタ。
 * @param fallbackMacAddressText フォールバック時に使う文字列。
 * @return 変換成功時true、失敗時false。
 */
bool createNetworkMacAddressText(String* networkMacAddressTextOut, const String& fallbackMacAddressText) {
  if (networkMacAddressTextOut == nullptr) {
    appLogError("mqtt::createNetworkMacAddressText failed. networkMacAddressTextOut is null.");
    return false;
  }
  String wifiMacAddressText = WiFi.macAddress();
  if (wifiMacAddressText.length() <= 0) {
    *networkMacAddressTextOut = fallbackMacAddressText;
    return true;
  }
  *networkMacAddressTextOut = wifiMacAddressText;
  return true;
}

/**
 * @brief パーティション情報を試験用表記（"0" / "1"）へ変換する。
 * @param partitionOut 参照するパーティション情報。
 * @return 変換後文字列。判定不能時は `"unknown"`。
 * @details
 * - [重要] 7015 / 7025 で必要な A/B 面確認を最短で行えるよう、`ota_0` を `"0"`、`ota_1` を `"1"` へ正規化する。
 * - [将来対応] 試験完了後は本項目自体を status 通知から削除する。
 */
String resolvePartitionIndexText(const esp_partition_t* partitionOut) {
  if (partitionOut == nullptr) {
    return "unknown";
  }
  if (partitionOut->type != ESP_PARTITION_TYPE_APP) {
    return "unknown";
  }
  if (partitionOut->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
      partitionOut->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
    const int32_t partitionIndex = static_cast<int32_t>(partitionOut->subtype) -
                                   static_cast<int32_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0);
    return String(partitionIndex);
  }
  if (partitionOut->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    return "factory";
  }
  return "unknown";
}

/**
 * @brief status通知理由をdetail文字列へ変換する。
 * @param subName statusのsub値。
 * @param reservedArgument statusの予備引数。
 * @return detailに設定する理由文字列。
 * @details
 * - [重要] detailは運用検索用に固定語彙へ正規化する。
 * - [推奨] 既定値は起動通知を示す `StartUp` とする。
 */
String resolveStatusDetailText(const char* subName, const char* reservedArgument) {
  String reasonSourceText;
  if (subName != nullptr && strlen(subName) > 0) {
    reasonSourceText = String(subName);
  } else if (reservedArgument != nullptr && strlen(reservedArgument) > 0) {
    reasonSourceText = String(reservedArgument);
  } else {
    reasonSourceText = "";
  }

  String reasonSourceLowerText = reasonSourceText;
  reasonSourceLowerText.toLowerCase();
  if (reasonSourceLowerText.length() == 0 ||
      reasonSourceLowerText == "startup" ||
      reasonSourceLowerText == "start-up" ||
      reasonSourceLowerText == "boot") {
    return "StartUp";
  }
  if (reasonSourceLowerText.indexOf("reconnect") >= 0) {
    return "ReConnect";
  }
  if (reasonSourceLowerText == "will") {
    return "Disconnect";
  }
  if ((reasonSourceLowerText.indexOf("restart") >= 0 || reasonSourceLowerText.indexOf("reboot") >= 0) &&
      (reasonSourceLowerText.indexOf("button") >= 0 || reasonSourceLowerText.indexOf("botton") >= 0)) {
    return "Restart(Button)";
  }
  if ((reasonSourceLowerText.indexOf("restart") >= 0 || reasonSourceLowerText.indexOf("reboot") >= 0) &&
      reasonSourceLowerText.indexOf("abort") >= 0) {
    return "Restart(abort)";
  }
  if ((reasonSourceLowerText.indexOf("restart") >= 0 || reasonSourceLowerText.indexOf("reboot") >= 0) &&
      reasonSourceLowerText.indexOf("call") >= 0) {
    return "Restart(Call)";
  }
  if (reasonSourceLowerText.indexOf("reply") >= 0) {
    return "Reply";
  }
  if (reasonSourceLowerText.indexOf("button") >= 0 || reasonSourceLowerText.indexOf("botton") >= 0 || reasonSourceLowerText.indexOf("bottun") >= 0) {
    return "button";
  }
  return "Reply";
}

/**
 * @brief UTCミリ秒エポックをISO8601(ミリ秒付き)へ変換する。
 * @param utcEpochMillis UTCエポックミリ秒。
 * @param iso8601TextOut 変換後文字列の出力先。
 * @return 変換成功時true、失敗時false。
 */
bool formatUtcIso8601FromEpochMillis(int64_t utcEpochMillis, String* iso8601TextOut) {
  if (iso8601TextOut == nullptr) {
    appLogError("mqtt::formatUtcIso8601FromEpochMillis failed. iso8601TextOut is null.");
    return false;
  }
  if (utcEpochMillis < 0) {
    utcEpochMillis = 0;
  }

  const time_t epochSeconds = static_cast<time_t>(utcEpochMillis / 1000);
  const int64_t millisecondPart = utcEpochMillis % 1000;
  struct tm utcTimeInfo {};
  if (gmtime_r(&epochSeconds, &utcTimeInfo) == nullptr) {
    appLogError("mqtt::formatUtcIso8601FromEpochMillis failed. gmtime_r returned null. utcEpochMillis=%lld",
                static_cast<long long>(utcEpochMillis));
    return false;
  }

  char dateTimeBuffer[32] = {};
  const size_t writtenLength = strftime(dateTimeBuffer, sizeof(dateTimeBuffer), "%Y-%m-%dT%H:%M:%S", &utcTimeInfo);
  if (writtenLength == 0) {
    appLogError("mqtt::formatUtcIso8601FromEpochMillis failed. strftime returned 0. utcEpochMillis=%lld",
                static_cast<long long>(utcEpochMillis));
    return false;
  }

  char iso8601Buffer[40] = {};
  const int printLength = snprintf(iso8601Buffer,
                                   sizeof(iso8601Buffer),
                                   "%s.%03lldZ",
                                   dateTimeBuffer,
                                   static_cast<long long>(millisecondPart));
  if (printLength <= 0 || printLength >= static_cast<int>(sizeof(iso8601Buffer))) {
    appLogError("mqtt::formatUtcIso8601FromEpochMillis failed. snprintf overflow. printLength=%d",
                printLength);
    return false;
  }

  *iso8601TextOut = String(iso8601Buffer);
  return true;
}

/**
 * @brief 現在のUTC時刻をISO8601(ミリ秒付き)へ変換する。
 * @param nowUtcIso8601Out 現在時刻文字列の出力先。
 * @param nowUtcEpochMillisOut 現在UTCエポックミリ秒の出力先。
 * @return 変換成功時true、失敗時false。
 */
bool getCurrentUtcIso8601(String* nowUtcIso8601Out, int64_t* nowUtcEpochMillisOut) {
  if (nowUtcIso8601Out == nullptr || nowUtcEpochMillisOut == nullptr) {
    appLogError("mqtt::getCurrentUtcIso8601 failed. output parameter is null. nowUtcIso8601Out=%p nowUtcEpochMillisOut=%p",
                nowUtcIso8601Out,
                nowUtcEpochMillisOut);
    return false;
  }

  struct timeval currentTimeValue {};
  int getTimeResult = gettimeofday(&currentTimeValue, nullptr);
  if (getTimeResult != 0) {
    appLogError("mqtt::getCurrentUtcIso8601 failed. gettimeofday returned=%d", getTimeResult);
    return false;
  }

  const int64_t epochMillis = static_cast<int64_t>(currentTimeValue.tv_sec) * 1000LL +
                              static_cast<int64_t>(currentTimeValue.tv_usec) / 1000LL;
  if (epochMillis < minimumValidUtcEpochMillis) {
    appLogWarn("mqtt::getCurrentUtcIso8601 skipped. utc time is not synchronized yet. epochMillis=%lld",
               static_cast<long long>(epochMillis));
    return false;
  }
  String iso8601Text;
  bool formatResult = formatUtcIso8601FromEpochMillis(epochMillis, &iso8601Text);
  if (!formatResult) {
    return false;
  }

  *nowUtcEpochMillisOut = epochMillis;
  *nowUtcIso8601Out = iso8601Text;
  return true;
}

}  // namespace

namespace mqtt {

bool buildMqttStatusPayload(const char* subName,
                            const char* reservedArgument,
                            uint32_t startupCpuMillis,
                            String* payloadTextOut) {
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
  String senderNameText;
  if (!resolveSenderNameText(&senderNameText)) {
    return false;
  }
  // [重要] 初期 public_id は送信元名と同じ IoT_<BaseMacNoColon> 形式を使う。
  // [理由] AP 初回接続時に server 側へも同じ初期識別子を渡し、未ペアリング状態でも空欄にしないため。
  String publicIdText = senderNameText;
  String networkMacAddressText;
  if (!createNetworkMacAddressText(&networkMacAddressText, macAddressText)) {
    return false;
  }

  String wifiSsidText = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("(dummy-ssid)");
  String ipAddressText = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("0.0.0.0");
  long wifiSignalLevelValue = static_cast<long>(WiFi.RSSI());
  const char* selectedSubName = (subName == nullptr || strlen(subName) == 0) ? "" : subName;
  const char* currentFirmwareVersion = appVersion::kFirmwareVersion;
  const String resolvedFirmwareWrittenAt =
      firmwareInfo::resolveFirmwareWrittenAtForStatus(currentFirmwareVersion, appVersion::kFirmwareWrittenAt);
  String statusDetailText = resolveStatusDetailText(subName, reservedArgument);
  if (statusDetailText == "Reply" && selectedSubName[0] == '\0' &&
      reservedArgument != nullptr && String(reservedArgument) == "Online") {
    // [重要] 起動時の自己通知（online publish）は StartUp として扱う。
    statusDetailText = "StartUp";
  }
  String currentTsText;
  int64_t currentUtcEpochMillis = 0;
  bool currentTimeResult = getCurrentUtcIso8601(&currentTsText, &currentUtcEpochMillis);
  if (!currentTimeResult) {
    appLogWarn("mqtt::buildMqttStatusPayload skipped. current UTC is not synchronized.");
    return false;
  }
  String noticeIdText;
  if (!createNoticeIdText(currentUtcEpochMillis, &noticeIdText)) {
    appLogWarn("mqtt::buildMqttStatusPayload skipped. notice id generation failed.");
    return false;
  }
  const uint32_t currentCpuMillis = millis();
  const uint32_t elapsedCpuMillis = (currentCpuMillis >= startupCpuMillis) ? (currentCpuMillis - startupCpuMillis) : 0;
  int64_t startupUtcEpochMillis = currentUtcEpochMillis - static_cast<int64_t>(elapsedCpuMillis);
  if (startupUtcEpochMillis < 0) {
    startupUtcEpochMillis = 0;
  }
  String startupTsText;
  bool startupTimeResult = formatUtcIso8601FromEpochMillis(startupUtcEpochMillis, &startupTsText);
  if (!startupTimeResult) {
    appLogWarn("mqtt::buildMqttStatusPayload skipped. startup UTC could not be calculated.");
    return false;
  }
  String payloadText = "{}";
  jsonService payloadJsonService;
  const String runningPartitionText = resolvePartitionIndexText(esp_ota_get_running_partition());
  const String bootPartitionText = resolvePartitionIndexText(esp_ota_get_boot_partition());
  const String nextUpdatePartitionText = resolvePartitionIndexText(esp_ota_get_next_update_partition(nullptr));

  jsonKeyValueItem itemList[] = {
      {iotCommon::mqtt::jsonKey::status::kVersion, jsonValueType::kString, "1", 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kDstId, jsonValueType::kString, "all", 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kSrcId, jsonValueType::kString, senderNameText.c_str(), 0, 0, false},
      {"publicId", jsonValueType::kString, publicIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kKind, jsonValueType::kString, "Notice", 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kMacAddr, jsonValueType::kString, macAddressText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kMacAddrNetwork, jsonValueType::kString, networkMacAddressText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kId, jsonValueType::kString, noticeIdText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kTimestamp, jsonValueType::kString, currentTsText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kCommand, jsonValueType::kString, iotCommon::mqtt::jsonKey::status::kCommand, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kSub, jsonValueType::kString, selectedSubName, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kOnlineState, jsonValueType::kString, reservedArgument, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kStartUpTime, jsonValueType::kString, startupTsText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kFWVersion, jsonValueType::kString, currentFirmwareVersion, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kFWWrittenAt, jsonValueType::kString, resolvedFirmwareWrittenAt.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kFirmwareVersion, jsonValueType::kString, currentFirmwareVersion, 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kFirmwareWrittenAt, jsonValueType::kString, resolvedFirmwareWrittenAt.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kWifiSignalLevel, jsonValueType::kLong, nullptr, 0, wifiSignalLevelValue, false},
      {iotCommon::mqtt::jsonKey::status::kIpAddress, jsonValueType::kString, ipAddressText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kWifiSsid, jsonValueType::kString, wifiSsidText.c_str(), 0, 0, false},
      // [重要] 7015 / 7025 試験用の一時項目。A/B 切替確認で使用する。
      {iotCommon::mqtt::jsonKey::status::kRunningPartition, jsonValueType::kString, runningPartitionText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kBootPartition, jsonValueType::kString, bootPartitionText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::status::kNextUpdatePartition, jsonValueType::kString, nextUpdatePartitionText.c_str(), 0, 0, false},
      {iotCommon::mqtt::jsonKey::kDetail, jsonValueType::kString, statusDetailText.c_str(), 0, 0, false},
  };

  bool setResult = payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]));
  if (!setResult) {
    appLogError("mqtt::buildMqttStatusPayload failed. setValuesByPath failed.");
    return false;
  }

  *payloadTextOut = payloadText;
  return true;
}

bool sendMqttStatus(PubSubClient* mqttClientOut,
                    const char* topicName,
                    const char* subName,
                    const char* reservedArgument,
                    uint32_t startupCpuMillis) {
  if (mqttClientOut == nullptr || topicName == nullptr || strlen(topicName) == 0) {
    appLogError("mqtt::sendMqttStatus failed. mqttClientOut=%p topicName=%p", mqttClientOut, topicName);
    return false;
  }
  String payloadText;
  if (!buildMqttStatusPayload(subName, reservedArgument, startupCpuMillis, &payloadText)) {
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
