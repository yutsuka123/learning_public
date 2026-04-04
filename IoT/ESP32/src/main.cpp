/**
 * @file main.cpp
 * @brief IoT ESP32の起動エントリ。mainTaskから各機能タスクを起動・初期化する。
 * @details
 * - [重要] Wi-Fi/MQTT/時刻同期の初期化シーケンスをmainTaskで制御する。
 * - [厳守] 時刻同期はtimeServerTaskへ委譲し、24時間周期の再同期をタスク側で継続する。
 */

#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>
#include <WiFi.h>

#include "certification.h"
#include "common.h"
#include "display.h"
#include "error.h"
#include "externalDevice.h"
#include "filesystem.h"
#include "http.h"
#include "i2c.h"
#include "input.h"
#include "interTaskMessage.h"
#include "led.h"
#include "log.h"
#include "maintenanceMode.h"
#include "maintenanceApServer.h"
#include "mqtt.h"
#include "ota.h"
#include "otaRollback.h"
#include "sensitiveData.h"
#include "sensitiveDataService.h"
#include "secureNvsInit.h"
#include "tcpip.h"
#include "timeServer.h"
#include "util.h"
#include "../header/wifi.h"

namespace {

/**
 * @brief シリアルログ出力のボーレート。
 * @type uint32_t
 * @note [重要] モニタ側の設定と一致しないとログ解読が困難になる。
 */
constexpr uint32_t serialBaudRate = 115200;
/**
 * @brief mainTaskに割り当てるスタックサイズ。
 * @type uint32_t
 * @note [重要] 初期化シーケンスとAPメンテナンスAPI処理が同一タスクで重なるため、12288へ拡張して運用する。
 * @note [変更][2026-03-24] 7041 dry-run precheck で measuredMinStackMarginBytes=3232 (閾値4096未満) が継続したため、
 *       PC-007.stackMarginOk を満たすために mainTask の最小余裕を増やす。
 */
constexpr uint32_t mainTaskStackSize = 12288;
/**
 * @brief mainTaskの優先度。
 * @type UBaseType_t
 */
constexpr UBaseType_t mainTaskPriority = 1;
/**
 * @brief mainTask心拍ログの送信間隔(ms)。
 * @type uint32_t
 */
constexpr uint32_t mainTaskIntervalMs = 500;
/**
 * @brief UTC同期済み判定に使う最小エポック秒（2020-01-01T00:00:00Z）。
 * @type time_t
 */
constexpr time_t minimumValidUtcEpochSeconds = 1577836800;
/**
 * @brief LCD診断モードの有効/無効。
 * @type bool
 * @note [推奨] 通常運用ではfalseを維持する。
 */
constexpr bool i2cLcdDiagnosticMode = false;
/** @brief 再接続リトライ間隔(ms)。@type uint32_t */
constexpr uint32_t reconnectRetryIntervalMs = 5000;
/** @brief NTP同期状態確認周期(ms): 12時間。@type uint32_t */
constexpr uint32_t ntpCheckIntervalMs = 12UL * 60UL * 60UL * 1000UL;
/** @brief NTP未同期時の再試行間隔(ms)。@type uint32_t */
constexpr uint32_t ntpUnsyncedRetryIntervalMs = 30000;
/** @brief 起動時AP遷移判定のボタンGPIO。@type uint8_t */
constexpr uint8_t startupButtonGpio = 4;
/** @brief ボタン押下レベル。@type uint8_t */
constexpr uint8_t startupButtonPressedLevel = LOW;
/** @brief 起動時AP遷移判定の長押し時間(ms)。@type uint32_t */
constexpr uint32_t startupMaintenanceLongPressMs = 3000;
/** @brief 起動時NTP待ちの上限(ms)。超過時は未同期でもMQTT接続へ進む。@type uint32_t */
constexpr uint32_t startupNtpWaitMaxMs = 30000;

/** @brief Wi-Fi制御タスクサービスインスタンス。 */
wifiTask wifiService;
/** @brief MQTT制御タスクサービスインスタンス。 */
mqttTask mqttService;
/** @brief HTTP制御タスクサービスインスタンス。 */
httpTask httpService;
/** @brief TCP/IP制御タスクサービスインスタンス（将来拡張用）。 */
tcpipTask tcpipService;
/** @brief OTA制御タスクサービスインスタンス。 */
otaTask otaService;
/** @brief 外部デバイス制御タスクサービスインスタンス。 */
externalDeviceTask externalDeviceService;
/** @brief 表示制御タスクサービスインスタンス。 */
displayTask displayService;
/** @brief LED制御タスクサービスインスタンス。 */
ledTask ledService;
/** @brief 入力制御タスクサービスインスタンス。 */
inputTask inputService;
/** @brief タイムサーバー同期タスクサービスインスタンス。 */
timeServerTask timeServerService;
/** @brief I2Cサービスインスタンス。LCD表示要求を直列化する。 */
i2cService i2cModule;

/** @brief 認証系サービスインスタンス。 */
certificationService certificationModule;
/** @brief ファイルシステム系サービスインスタンス。 */
filesystemService filesystemModule;
/** @brief 機密情報ロード/保存サービスインスタンス。 */
sensitiveDataService sensitiveDataModule;
/** @brief タスク間メッセージサービス参照。 */
interTaskMessageService& messageService = getInterTaskMessageService();

String maskSecretForLog(const String& rawValue);
String getCurrentTimeString();
iotError::errorCodeType mapTaskErrorCode(appTaskId sourceTaskId);
bool isUtcTimeSynchronized();
void writeErrText(iotError::errorCodeType errorCode, char* errTextOut, size_t errTextOutSize);
bool executeWifiConnectAndConfirm(const String& wifiSsid, const String& wifiPass);
bool executeMqttConnectAndConfirm(const String& mqttUrl,
                                  const String& mqttUser,
                                  const String& mqttPass,
                                  int32_t mqttPort,
                                  bool mqttTls);
bool executeMqttStatusPublishAndConfirm(uint32_t startupCpuMillis, const char* publishSubName, const char* publishReasonText);
bool executeNtpSyncAndConfirm(const String& timeServerUrl, int32_t timeServerPort, bool timeServerTls);
bool createMaintenanceApSsidText(String* apSsidTextOut);
bool startMaintenanceApModeAndHold(i2cService* i2cModuleOut);
bool detectStartupMaintenanceLongPress();
bool detectMaintenanceApModeRebootLongPress();
bool shouldAllowStartupMqttWithoutSyncedTime(uint32_t mainTaskEntryCpuMillis);

/**
 * @brief パスワード等の秘匿値をログ表示用にマスクする。
 * @param rawValue 元の文字列。
 * @return マスク後文字列。
 */
String maskSecretForLog(const String& rawValue) {
  if (rawValue.length() <= 0) {
    return "(empty)";
  }
  return "******";
}

/**
 * @brief 内部UTC時刻をLCD表示向け文字列に変換する。
 * @return "YYMMDD HH:MM:SS" 形式。未同期時は "TIME UNSYNC"。
 */
String getCurrentTimeString() {
  if (!isUtcTimeSynchronized()) {
    return "TIME UNSYNC";
  }
  time_t currentEpochSeconds = time(nullptr);

  struct tm utcTimeInfo {};
  if (gmtime_r(&currentEpochSeconds, &utcTimeInfo) == nullptr) {
    return "TIME ERROR";
  }

  char timeBuffer[20] = {};
  size_t writtenLength = strftime(timeBuffer, sizeof(timeBuffer), "%y%m%d %H:%M:%S", &utcTimeInfo);
  if (writtenLength == 0) {
    return "TIME ERROR";
  }
  return String(timeBuffer);
}

/**
 * @brief 現在時刻が有効なUTCへ同期済みか判定する。
 * @return 同期済みならtrue、未同期ならfalse。
 */
bool isUtcTimeSynchronized() {
  const time_t currentEpochSeconds = time(nullptr);
  return (currentEpochSeconds >= minimumValidUtcEpochSeconds);
}

/**
 * @brief タスク起因エラーを共通エラーコードへ変換する。
 * @param sourceTaskId エラー発生元タスクID。
 * @return 共通エラーコード。
 */
iotError::errorCodeType mapTaskErrorCode(appTaskId sourceTaskId) {
  switch (sourceTaskId) {
    case appTaskId::kWifi:
      return static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kNetworkWifiTaskError);
    case appTaskId::kMqtt:
      return static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kNetworkMqttTaskError);
    case appTaskId::kHttp:
      return static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kNetworkHttpTaskError);
    case appTaskId::kOta:
      return static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kNetworkOtaTaskError);
    case appTaskId::kTimeServer:
      return static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kNetworkTimeServerTaskError);
    case appTaskId::kExternalDevice:
      return static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kHardwareGeneral);
    default:
      return static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kOtherGeneral);
  }
}

/**
 * @brief エラーコードを `ERRxxx` 形式へ変換する。
 * @param errorCode 共通エラーコード。
 * @param errTextOut 出力先バッファ。
 * @param errTextOutSize 出力先バッファサイズ。
 */
void writeErrText(iotError::errorCodeType errorCode, char* errTextOut, size_t errTextOutSize) {
  bool result = iotError::formatErrCodeText(errorCode, errTextOut, errTextOutSize);
  if (!result && errTextOut != nullptr && errTextOutSize > 0) {
    snprintf(errTextOut, errTextOutSize, "ERR999");
  }
}

/**
 * @brief Wi-Fi接続要求を送信し、接続完了応答まで待機する。
 * @param wifiSsid SSID。
 * @param wifiPass パスワード。
 * @return 成功時true、失敗時false。
 */
bool executeWifiConnectAndConfirm(const String& wifiSsid, const String& wifiPass) {
  appUtil::appTaskMessageDetail wifiInitDetail = appUtil::createEmptyMessageDetail();
  wifiInitDetail.text = wifiSsid.c_str();
  wifiInitDetail.text2 = wifiPass.c_str();
  appLogInfo("mainTaskEntry: wifi init request send. ssid=%s pass=%s",
             wifiSsid.c_str(),
             maskSecretForLog(wifiPass).c_str());
  bool wifiInitSendResult = appUtil::sendMessage(
      appTaskId::kWifi,
      appTaskId::kMain,
      appMessageType::kWifiInitRequest,
      &wifiInitDetail,
      300);
  if (!wifiInitSendResult) {
    char errText[8] = {};
    writeErrText(static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kEspStartupWifiInitRequestFailed), errText, sizeof(errText));
    appLogWarn("%s mainTaskEntry: appUtil::sendMessage(kWifiInitRequest) returned false.", errText);
    return false;
  }

  appTaskMessage wifiInitResponseMessage{};
  bool wifiInitWaitResult = appUtil::waitMessage(
      appTaskId::kWifi,
      appTaskId::kMain,
      appMessageType::kWifiInitDone,
      nullptr,
      35000,
      &wifiInitResponseMessage);
  if (!wifiInitWaitResult) {
    char errText[8] = {};
    writeErrText(static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kEspStartupWifiInitTimeout), errText, sizeof(errText));
    appLogWarn("%s mainTaskEntry: appUtil::waitMessage(kWifiInitDone) timeout.", errText);
    return false;
  }
  appLogInfo("mainTaskEntry: wifi initialization completed. detail=%s", wifiInitResponseMessage.text);
  return true;
}

/**
 * @brief MQTT接続要求を送信し、接続完了応答まで待機する。
 * @param mqttUrl ブローカーURL。
 * @param mqttUser ユーザー名。
 * @param mqttPass パスワード。
 * @param mqttPort ポート。
 * @param mqttTls TLSフラグ。
 * @return 成功時true、失敗時false。
 */
bool executeMqttConnectAndConfirm(const String& mqttUrl,
                                  const String& mqttUser,
                                  const String& mqttPass,
                                  int32_t mqttPort,
                                  bool mqttTls) {
  appUtil::appTaskMessageDetail mqttInitDetail = appUtil::createEmptyMessageDetail();
  mqttInitDetail.hasIntValue = true;
  mqttInitDetail.intValue = mqttPort;
  mqttInitDetail.hasBoolValue = true;
  mqttInitDetail.boolValue = mqttTls;
  mqttInitDetail.text = mqttUrl.c_str();
  mqttInitDetail.text2 = mqttUser.c_str();
  mqttInitDetail.text3 = mqttPass.c_str();
  appLogInfo("mainTaskEntry: mqtt init request send. url=%s user=%s pass=%s port=%ld tls=%d",
             mqttUrl.c_str(),
             mqttUser.c_str(),
             maskSecretForLog(mqttPass).c_str(),
             static_cast<long>(mqttPort),
             static_cast<int>(mqttTls));
  bool mqttInitSendResult = appUtil::sendMessage(
      appTaskId::kMqtt,
      appTaskId::kMain,
      appMessageType::kMqttInitRequest,
      &mqttInitDetail,
      300);
  if (!mqttInitSendResult) {
    char errText[8] = {};
    writeErrText(static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kEspStartupMqttInitRequestFailed), errText, sizeof(errText));
    appLogWarn("%s mainTaskEntry: appUtil::sendMessage(kMqttInitRequest) returned false.", errText);
    return false;
  }

  appTaskMessage mqttInitResponseMessage{};
  bool mqttInitWaitResult = appUtil::waitMessage(
      appTaskId::kMqtt,
      appTaskId::kMain,
      appMessageType::kMqttInitDone,
      nullptr,
      20000,
      &mqttInitResponseMessage);
  if (!mqttInitWaitResult) {
    char errText[8] = {};
    writeErrText(static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kEspStartupMqttInitTimeout), errText, sizeof(errText));
    appLogWarn("%s mainTaskEntry: appUtil::waitMessage(kMqttInitDone) timeout.", errText);
    return false;
  }
  appLogInfo("mainTaskEntry: mqtt initialization completed. detail=%s", mqttInitResponseMessage.text);
  return true;
}

/**
 * @brief MQTT status publish要求を送信し、完了応答まで待機する。
 * @param startupCpuMillis 起動CPU時刻(ms)。
 * @param publishSubName sub値。
 * @param publishReasonText ログ用理由文字列。
 * @return 成功時true、失敗時false。
 */
bool executeMqttStatusPublishAndConfirm(uint32_t startupCpuMillis, const char* publishSubName, const char* publishReasonText) {
  appUtil::appTaskMessageDetail mqttPublishDetail = appUtil::createEmptyMessageDetail();
  mqttPublishDetail.hasBoolValue = true;
  mqttPublishDetail.boolValue = true;
  mqttPublishDetail.hasIntValue = true;
  mqttPublishDetail.intValue = static_cast<int32_t>(startupCpuMillis);
  mqttPublishDetail.text = "status online publish request";
  mqttPublishDetail.text2 = publishSubName;
  mqttPublishDetail.text3 = "Online";
  bool mqttPublishRequestResult = appUtil::sendMessage(
      appTaskId::kMqtt,
      appTaskId::kMain,
      appMessageType::kMqttPublishOnlineRequest,
      &mqttPublishDetail,
      300);
  if (!mqttPublishRequestResult) {
    appLogWarn("mainTaskEntry: appUtil::sendMessage(kMqttPublishOnlineRequest) failed. reason=%s",
               (publishReasonText == nullptr ? "(null)" : publishReasonText));
    return false;
  }

  appTaskMessage mqttPublishResponseMessage{};
  bool mqttPublishWaitResult = appUtil::waitMessage(
      appTaskId::kMqtt,
      appTaskId::kMain,
      appMessageType::kMqttPublishOnlineDone,
      nullptr,
      20000,
      &mqttPublishResponseMessage);
  if (!mqttPublishWaitResult) {
    appLogWarn("mainTaskEntry: appUtil::waitMessage(kMqttPublishOnlineDone) timeout. reason=%s",
               (publishReasonText == nullptr ? "(null)" : publishReasonText));
    return false;
  }
  appLogInfo("mainTaskEntry: mqtt status publish completed. reason=%s detail=%s",
             (publishReasonText == nullptr ? "(null)" : publishReasonText),
             mqttPublishResponseMessage.text);
  return true;
}

/**
 * @brief NTP同期要求を送信し、同期完了応答を待機する。
 * @param timeServerUrl タイムサーバーURL。
 * @param timeServerPort タイムサーバーポート。
 * @param timeServerTls TLSフラグ。
 * @return 同期成功時true、失敗時false。
 */
bool executeNtpSyncAndConfirm(const String& timeServerUrl, int32_t timeServerPort, bool timeServerTls) {
  appUtil::appTaskMessageDetail timeServerInitDetail = appUtil::createEmptyMessageDetail();
  timeServerInitDetail.text = timeServerUrl.c_str();
  timeServerInitDetail.text2 = "";
  timeServerInitDetail.text3 = "";
  timeServerInitDetail.hasIntValue = true;
  timeServerInitDetail.intValue = timeServerPort;
  timeServerInitDetail.hasBoolValue = true;
  timeServerInitDetail.boolValue = timeServerTls;
  appLogInfo("mainTaskEntry: time server init request send. url=%s port=%ld tls=%d",
             timeServerUrl.c_str(),
             static_cast<long>(timeServerPort),
             static_cast<int>(timeServerTls));
  bool timeServerInitSendResult = appUtil::sendMessage(
      appTaskId::kTimeServer,
      appTaskId::kMain,
      appMessageType::kTimeServerInitRequest,
      &timeServerInitDetail,
      300);
  if (!timeServerInitSendResult) {
    char errText[8] = {};
    writeErrText(static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kEspStartupTimeInitRequestFailed), errText, sizeof(errText));
    appLogWarn("%s mainTaskEntry: appUtil::sendMessage(kTimeServerInitRequest) returned false.", errText);
    return false;
  }

  appTaskMessage timeServerInitResponseMessage{};
  bool timeServerInitWaitResult = appUtil::waitMessage(
      appTaskId::kTimeServer,
      appTaskId::kMain,
      appMessageType::kTimeServerInitDone,
      nullptr,
      20000,
      &timeServerInitResponseMessage);
  if (!timeServerInitWaitResult) {
    char errText[8] = {};
    writeErrText(static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kEspStartupTimeInitTimeout), errText, sizeof(errText));
    appLogWarn("%s mainTaskEntry: appUtil::waitMessage(kTimeServerInitDone) timeout.", errText);
    return false;
  }
  if (timeServerInitResponseMessage.intValue != 1) {
    appLogWarn("mainTaskEntry: time server initialization failed. detail=%s utcNow=%s",
               timeServerInitResponseMessage.text,
               timeServerInitResponseMessage.text2);
    return false;
  }
  appLogInfo("mainTaskEntry: time server initialization completed. detail=%s utcNow=%s",
             timeServerInitResponseMessage.text,
             timeServerInitResponseMessage.text2);
  return true;
}

/**
 * @brief メンテナンスAP名を生成する。
 * @param apSsidTextOut 出力先。
 * @return 生成成功時true、失敗時false。
 */
bool createMaintenanceApSsidText(String* apSsidTextOut) {
  if (apSsidTextOut == nullptr) {
    appLogError("createMaintenanceApSsidText failed. apSsidTextOut is null.");
    return false;
  }
  const uint64_t efuseMac = ESP.getEfuseMac();
  char compactMacBuffer[13] = {};
  const int writtenLength = snprintf(compactMacBuffer,
                                     sizeof(compactMacBuffer),
                                     "%02X%02X%02X%02X%02X%02X",
                                     static_cast<unsigned>((efuseMac >> 40) & 0xFF),
                                     static_cast<unsigned>((efuseMac >> 32) & 0xFF),
                                     static_cast<unsigned>((efuseMac >> 24) & 0xFF),
                                     static_cast<unsigned>((efuseMac >> 16) & 0xFF),
                                     static_cast<unsigned>((efuseMac >> 8) & 0xFF),
                                     static_cast<unsigned>(efuseMac & 0xFF));
  if (writtenLength <= 0 || writtenLength >= static_cast<int>(sizeof(compactMacBuffer))) {
    appLogError("createMaintenanceApSsidText failed. snprintf overflow. writtenLength=%d", writtenLength);
    return false;
  }
  *apSsidTextOut = String(iotCommon::apConfig::kMaintApPrefix) + String(compactMacBuffer);
  return true;
}

/**
 * @brief メンテナンスAPを起動し、そのまま待機ループへ入る。
 * @param i2cModuleOut 表示モジュール。
 * @return AP起動成功時true、失敗時false。
 */
bool startMaintenanceApModeAndHold(i2cService* i2cModuleOut) {
  String maintenanceApSsid;
  if (!createMaintenanceApSsidText(&maintenanceApSsid)) {
    return false;
  }
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP);
  const bool apStartResult = WiFi.softAP(maintenanceApSsid.c_str(), iotCommon::apConfig::kMaintApPass);
  if (!apStartResult) {
    appLogError("startMaintenanceApModeAndHold failed. WiFi.softAP returned false. ssid=%s", maintenanceApSsid.c_str());
    return false;
  }
  const IPAddress apIpAddress = WiFi.softAPIP();
  appLogWarn("startMaintenanceApModeAndHold success. ssid=%s pass=%s apIp=%s",
             maintenanceApSsid.c_str(),
             iotCommon::apConfig::kMaintApPass,
             apIpAddress.toString().c_str());

  if (i2cModuleOut != nullptr) {
    i2cModuleOut->requestLcdText("MaintenanceMode", maintenanceApSsid.substring(0, 16).c_str(), 0);
  }
  const bool maintenanceServerStartResult = maintenanceApServer::start(maintenanceApSsid, &sensitiveDataModule);
  if (!maintenanceServerStartResult) {
    appLogError("startMaintenanceApModeAndHold failed. maintenanceApServer::start returned false.");
    return false;
  }
  pinMode(startupButtonGpio, INPUT_PULLUP);

  // [重要] APモード専用処理。通常のWi-Fi/MQTT初期化には進ませない。
  for (;;) {
    maintenanceApServer::loopOnce();
    if (detectMaintenanceApModeRebootLongPress()) {
      appLogWarn("startMaintenanceApModeAndHold: reboot requested by button long press in AP mode.");
      ledController::indicateButtonLongPressRebootPattern();
      esp_restart();
    }
    ledController::indicateMaintenanceModeBluePatternCycle();
  }
}

/**
 * @brief 起動時3秒長押しでメンテナンスモード要求を判定する。
 * @return 長押し成立時true。
 */
bool detectStartupMaintenanceLongPress() {
  pinMode(startupButtonGpio, INPUT_PULLUP);
  if (digitalRead(startupButtonGpio) != startupButtonPressedLevel) {
    return false;
  }
  const uint32_t pressedStartMs = millis();
  appLogInfo("detectStartupMaintenanceLongPress: button pressed on boot. wait for %lu ms.",
             static_cast<unsigned long>(startupMaintenanceLongPressMs));
  while (millis() - pressedStartMs < startupMaintenanceLongPressMs) {
    if (digitalRead(startupButtonGpio) != startupButtonPressedLevel) {
      appLogInfo("detectStartupMaintenanceLongPress: released before threshold.");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  appLogWarn("detectStartupMaintenanceLongPress: 3s long press detected.");
  return true;
}

/**
 * @brief APモード中の3秒長押し再起動要求を判定する。
 * @return 長押し再起動成立時true。
 */
bool detectMaintenanceApModeRebootLongPress() {
  static bool isPressing = false;
  static uint32_t pressedStartMs = 0;
  const bool isPressed = (digitalRead(startupButtonGpio) == startupButtonPressedLevel);
  if (!isPressed) {
    isPressing = false;
    pressedStartMs = 0;
    return false;
  }
  if (!isPressing) {
    isPressing = true;
    pressedStartMs = millis();
    appLogInfo("detectMaintenanceApModeRebootLongPress: button press started in AP mode.");
    return false;
  }
  if (millis() - pressedStartMs < startupMaintenanceLongPressMs) {
    return false;
  }
  isPressing = false;
  pressedStartMs = 0;
  appLogWarn("detectMaintenanceApModeRebootLongPress: 3s long press detected in AP mode.");
  return true;
}

/**
 * @brief 起動時NTP待ちが長過ぎる場合にMQTT接続を許可する判定。
 * @param mainTaskEntryCpuMillis mainTask開始時刻(ms)。
 * @return 許可する場合true。
 */
bool shouldAllowStartupMqttWithoutSyncedTime(uint32_t mainTaskEntryCpuMillis) {
  static bool hasLoggedFallback = false;
  const uint32_t elapsedMs = millis() - mainTaskEntryCpuMillis;
  if (elapsedMs < startupNtpWaitMaxMs) {
    return false;
  }
  if (!hasLoggedFallback) {
    hasLoggedFallback = true;
    appLogWarn("mainTaskEntry: NTP wait timeout reached. continue MQTT connect without UTC sync. elapsedMs=%lu",
               static_cast<unsigned long>(elapsedMs));
  }
  return true;
}

/**
 * @brief メインタスク本体。将来ここから各機能タスクを起動する。
 * @param taskParameter タスク引数（未使用）。
 * @return なし（無限ループ）。
 * @details
 * - [重要] START表示 -> Wi-Fi初期化 -> MQTT初期化 -> online publish -> 初回時刻同期 -> DONE表示 の順序を厳守する。
 * - [厳守] 各段階の失敗時は赤LEDアボートパターンを表示し、タスクを終了する。
 */
void mainTaskEntry(void* taskParameter) {
  (void)taskParameter;
  const uint32_t mainTaskEntryCpuMillis = millis();

  // [重要] 起動時は青LEDを一旦消灯後0.5秒待機してから点灯する。
  ledController::initializeByMainOnBoot();
  appLogInfo("mainTask started.");

  bool i2cStartResult = i2cModule.startTask();
  if (!i2cStartResult) {
    char errText[8] = {};
    writeErrText(static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kEspStartupI2cTaskStartFailed), errText, sizeof(errText));
    ledController::indicateAbortPattern();
    appLogFatal("%s mainTaskEntry failed. i2cModule.startTask returned false.", errText);
    vTaskDelete(nullptr);
  }
  bool startDisplayResult = i2cModule.requestLcdText("BOOT DONE", "", 0);
  if (!startDisplayResult) {
    appLogWarn("mainTaskEntry: requestLcdText(START) failed.");
  }
  if (i2cLcdDiagnosticMode) {
    appLogWarn("mainTaskEntry: I2C LCD diagnostic mode enabled. run finite diagnostics and resume normal startup.");
    constexpr uint32_t diagnosticLoopCount = 5;
    for (uint32_t displayCounter = 0; displayCounter < diagnosticLoopCount; ++displayCounter) {
      char secondLineBuffer[17];
      snprintf(secondLineBuffer,
               sizeof(secondLineBuffer),
               "Counter:%lu",
               static_cast<unsigned long>(displayCounter));
      bool diagnosticDisplayResult = i2cModule.requestLcdText("I2C:0x27 OK", secondLineBuffer, 0);
      if (!diagnosticDisplayResult) {
        appLogWarn("mainTaskEntry: diagnostic requestLcdText failed. counter=%lu",
                   static_cast<unsigned long>(displayCounter));
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    appLogInfo("mainTaskEntry: I2C LCD diagnostic completed. resume normal startup.");
  }

  bool shouldEnterMaintenanceAp = maintenanceMode::consumeMaintenanceModeRequest();
  if (!shouldEnterMaintenanceAp && detectStartupMaintenanceLongPress()) {
    shouldEnterMaintenanceAp = true;
    appLogWarn("mainTaskEntry: startup long press requested maintenance mode.");
  }
  if (shouldEnterMaintenanceAp) {
    appLogWarn("mainTaskEntry: maintenance mode request consumed. start AP maintenance mode.");
    const bool apModeResult = startMaintenanceApModeAndHold(&i2cModule);
    if (!apModeResult) {
      appLogError("mainTaskEntry: failed to start maintenance AP mode. fallback to normal startup.");
    }
  }


  // [重要] 起動時に機密設定を読み込み、将来NVS移行時も同等手順を維持する。
  // [補足] ここで読み込む値は後段のWi-Fi/MQTT初期化メッセージへそのまま搭載される。
  String wifiSsid;
  String wifiPass;
  String mqttUrl;
  String mqttUrlName;
  String mqttUser;
  String mqttPass;
  int32_t mqttPort = 0;
  bool mqttTls = false;
  String timeServerUrl;
  int32_t timeServerPort = 123;
  bool timeServerTls = false;

  bool wifiLoadResult = sensitiveDataModule.loadWifiCredentials(&wifiSsid, &wifiPass);
  if (!wifiLoadResult) {
    appLogWarn("mainTaskEntry: loadWifiCredentials failed. fallback empty values.");
  }

  bool mqttLoadResult = sensitiveDataModule.loadMqttConfig(&mqttUrl, &mqttUrlName, &mqttUser, &mqttPass, &mqttPort, &mqttTls);
  if (!mqttLoadResult) {
    appLogWarn("mainTaskEntry: loadMqttConfig failed. fallback default values.");
    mqttPort = 8883;
    mqttTls = true;
  }

  bool timeServerLoadResult = sensitiveDataModule.loadTimeServerConfig(
      &timeServerUrl,
      &timeServerPort,
      &timeServerTls);
  if (!timeServerLoadResult) {
    appLogWarn("mainTaskEntry: loadTimeServerConfig failed. fallback default values.");
    timeServerUrl = "";
    timeServerPort = 123;
    timeServerTls = false;
  }
  if (timeServerUrl.length() <= 0 && mqttUrl.length() > 0) {
    // [重要] 旧設定ファイル互換: timeServerUrl未設定時は同一ファイル内のmqttUrlを引き継ぐ。
    timeServerUrl = mqttUrl;
    appLogWarn("mainTaskEntry: timeServerUrl is empty. fallback to mqttUrl=%s", mqttUrl.c_str());
  }

#if defined(SENSITIVE_DATA_USE_HEADER_VALUES) && (SENSITIVE_DATA_USE_HEADER_VALUES == 1)
  // [重要] 開発初期はヘッダー機密値を優先して即時反映する。
  // [将来対応] 本ブロックはNVS移行完了後に廃止し、NVS読込値を唯一の正とする。
  wifiSsid = SENSITIVE_WIFI_SSID;
  wifiPass = SENSITIVE_WIFI_PASS;
  mqttUrl = SENSITIVE_MQTT_URL;
  mqttUser = SENSITIVE_MQTT_USER;
  mqttPass = SENSITIVE_MQTT_PASS;
  mqttPort = static_cast<int32_t>(SENSITIVE_MQTT_PORT);
  mqttTls = (SENSITIVE_MQTT_TLS != 0);
  timeServerUrl = SENSITIVE_TIME_SERVER_URL;
  timeServerPort = static_cast<int32_t>(SENSITIVE_TIME_SERVER_PORT);
  timeServerTls = (SENSITIVE_TIME_SERVER_TLS != 0);
  appLogWarn("mainTaskEntry: using sensitiveData.h macro values. file-based values are overridden.");
#endif

  appLogInfo("mainTaskEntry: wifi loaded. ssid=%s, pass=%s",
             wifiSsid.c_str(),
             maskSecretForLog(wifiPass).c_str());
  appLogInfo("mainTaskEntry: mqtt loaded. url=%s, user=%s, pass=%s, port=%ld, tls=%d",
             mqttUrl.c_str(),
             mqttUser.c_str(),
             maskSecretForLog(mqttPass).c_str(),
             static_cast<long>(mqttPort),
             static_cast<int>(mqttTls));
  appLogInfo("mainTaskEntry: time server loaded. url=%s, port=%ld, tls=%d",
             timeServerUrl.c_str(),
             static_cast<long>(timeServerPort),
             static_cast<int>(timeServerTls));

  const bool missingWifiConfig = (wifiSsid.length() == 0);
  const bool missingMqttConfig = (mqttUrl.length() == 0 || mqttUser.length() == 0 || mqttPass.length() == 0);
  if (missingWifiConfig || missingMqttConfig) {
    appLogWarn("mainTaskEntry: required wifi/mqtt config is missing. enter maintenance AP mode. missingWifi=%d missingMqtt=%d",
               missingWifiConfig ? 1 : 0,
               missingMqttConfig ? 1 : 0);
    const bool apModeResult = startMaintenanceApModeAndHold(&i2cModule);
    if (!apModeResult) {
      appLogError("mainTaskEntry: failed to start maintenance AP mode for missing config. continue normal startup.");
    }
  }

  // [重要] FreeRTOS Queueによる一般的なメッセージ連携を開始する。
  // [補足] mainTaskは指令側として各機能タスクを起動し、応答のみを待機する。
  wifiService.startTask();
  mqttService.startTask();
  httpService.startTask();
  //tcpipService.startTask();  // 必要時のみ有効化
  otaService.startTask();
  externalDeviceService.startTask();
  displayService.startTask();
  ledService.startTask();
  inputService.startTask();
  timeServerService.startTask();

  appUtil::appTaskMessageDetail startupDetail = appUtil::createEmptyMessageDetail();
  startupDetail.hasIntValue = true;
  startupDetail.intValue = 1;
  startupDetail.text = "startup request from main";
  appUtil::sendMessage(appTaskId::kWifi, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);
  appUtil::sendMessage(appTaskId::kMqtt, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);
  appUtil::sendMessage(appTaskId::kHttp, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);
  appUtil::sendMessage(appTaskId::kOta, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);
  appUtil::sendMessage(appTaskId::kExternalDevice, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);
  appUtil::sendMessage(appTaskId::kDisplay, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);
  appUtil::sendMessage(appTaskId::kLed, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);
  appUtil::sendMessage(appTaskId::kInput, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);
  appUtil::sendMessage(appTaskId::kTimeServer, appTaskId::kMain, appMessageType::kStartupRequest, &startupDetail, 200);


  // [重要] 起動時から同一ロジックで接続/確認/再試行を行うため、状態フラグを明示的に管理する。
  bool isWifiReady = false;
  bool isMqttReady = false;
  bool isStartupStatusPublished = false;
  bool isCurrentAppConfirmed = false;
  bool shouldPublishReconnectStatus = false;
  bool shouldRunNtpReconnectAfterPublish = false;
  uint32_t lastWifiRetryAtMs = 0;
  uint32_t lastMqttRetryAtMs = 0;
  uint32_t lastPublishRetryAtMs = 0;
  uint32_t lastNtpCheckAtMs = 0;
  uint32_t lastNtpRetryAtMs = 0;

  startDisplayResult = i2cModule.requestLcdText("WIFI SEARCHING...", "", 0);


  for (;;) {
    static uint32_t heartbeatCount = 0;
    static iotError::errorCodeType currentErrorCode = iotError::kNoError;
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kMain, &receivedMessage, pdMS_TO_TICKS(100));
    if (receiveResult) {
      appLogInfo("mainTaskEntry: message received. src=%d dst=%d type=%d text=%s",
                 static_cast<int>(receivedMessage.sourceTaskId),
                 static_cast<int>(receivedMessage.destinationTaskId),
                 static_cast<int>(receivedMessage.messageType),
                 receivedMessage.text);
      if (receivedMessage.messageType == appMessageType::kTaskError) {
        currentErrorCode = mapTaskErrorCode(receivedMessage.sourceTaskId);
        char errText[8] = {};
        writeErrText(currentErrorCode, errText, sizeof(errText));
        appLogError("%s mainTaskEntry: task error received. src=%d text=%s",
                    errText,
                    static_cast<int>(receivedMessage.sourceTaskId),
                    receivedMessage.text);
        if (receivedMessage.sourceTaskId == appTaskId::kWifi) {
          // [重要] Wi-Fi系エラーはチェーン再接続の起点に戻す。
          isWifiReady = false;
          isMqttReady = false;
          shouldPublishReconnectStatus = true;
          shouldRunNtpReconnectAfterPublish = true;
          lastWifiRetryAtMs = 0;
          lastMqttRetryAtMs = 0;
          lastPublishRetryAtMs = 0;
        } else if (receivedMessage.sourceTaskId == appTaskId::kMqtt) {
          // [重要] MQTT系エラー時はMQTT再接続→ReConnect publishを即再試行する。
          isMqttReady = false;
          shouldPublishReconnectStatus = true;
          shouldRunNtpReconnectAfterPublish = true;
          lastMqttRetryAtMs = 0;
          lastPublishRetryAtMs = 0;
        } else if (receivedMessage.sourceTaskId == appTaskId::kTimeServer) {
          // [重要] NTP系エラー時はNTP再同期タイマをリセットして短周期再試行へ戻す。
          lastNtpRetryAtMs = 0;
        }
      }
    }

    uint32_t nowMs = millis();
    bool isWifiCurrentlyConnected = (WiFi.status() == WL_CONNECTED);
    if (!isWifiCurrentlyConnected) {
      if (isWifiReady || isMqttReady) {
        appLogWarn("mainTaskEntry: wifi disconnected detected. run reconnect chain.");
      }
      isWifiReady = false;
      isMqttReady = false;
      shouldPublishReconnectStatus = true;
      if (lastWifiRetryAtMs == 0 || (nowMs - lastWifiRetryAtMs >= reconnectRetryIntervalMs)) {
        lastWifiRetryAtMs = nowMs;
        startDisplayResult = i2cModule.requestLcdText("WIFI RECONNECT", "", 0);
        if (executeWifiConnectAndConfirm(wifiSsid, wifiPass)) {
          isWifiReady = true;
          startDisplayResult = i2cModule.requestLcdText("WIFI CONNECTED", "", 0);
          shouldRunNtpReconnectAfterPublish = true;
        } else {
          startDisplayResult = i2cModule.requestLcdText("WIFI RECON FAIL", "", 0);
        }
      }
    } else {
      isWifiReady = true;
    }

    const bool isStartupPhase = !isStartupStatusPublished;
    const bool canRegisterWillWithSynchronizedTime = isUtcTimeSynchronized();
    const bool startupNtpWaitTimedOut = shouldAllowStartupMqttWithoutSyncedTime(mainTaskEntryCpuMillis);
    const bool canStartMqttConnect = !isStartupPhase || canRegisterWillWithSynchronizedTime || startupNtpWaitTimedOut;
    if (isWifiReady && !isMqttReady && !canStartMqttConnect) {
      // [重要] 起動フェーズのWill登録はUTC同期後に行う。
      // [理由] Willのts/startUpTimeへ時刻を入れ、(unsynchronized)フォールバックを常用しないため。
      startDisplayResult = i2cModule.requestLcdText("WAIT NTP FOR WILL", "", 0);
      if (lastNtpRetryAtMs == 0 || (nowMs - lastNtpRetryAtMs >= ntpUnsyncedRetryIntervalMs)) {
        appLogInfo("mainTaskEntry: skip mqtt connect until UTC sync for startup will registration.");
      }
    }
    if (isWifiReady && !isMqttReady && canStartMqttConnect &&
        (lastMqttRetryAtMs == 0 || (nowMs - lastMqttRetryAtMs >= reconnectRetryIntervalMs))) {
      lastMqttRetryAtMs = nowMs;
      startDisplayResult = i2cModule.requestLcdText("MQTT RECONNECT", "", 0);
      if (executeMqttConnectAndConfirm(mqttUrl, mqttUser, mqttPass, mqttPort, mqttTls)) {
        isMqttReady = true;
        shouldPublishReconnectStatus = true;
        shouldRunNtpReconnectAfterPublish = true;
        startDisplayResult = i2cModule.requestLcdText("MQTT CONNECTED", "", 0);
      } else {
        startDisplayResult = i2cModule.requestLcdText("MQTT RECON FAIL", "", 0);
      }
    }

    if (isWifiReady && isMqttReady && (lastPublishRetryAtMs == 0 || (nowMs - lastPublishRetryAtMs >= reconnectRetryIntervalMs))) {
      if (!isStartupStatusPublished || shouldPublishReconnectStatus) {
        const bool isReconnectPublish = isStartupStatusPublished && shouldPublishReconnectStatus;
        const char* publishSubName = isReconnectPublish
                                         ? iotCommon::mqtt::subCommand::status::kReConnect
                                         : iotCommon::mqtt::subCommand::status::kStartUp;
        const char* publishReasonText = isReconnectPublish ? "ReConnect" : "StartUp";
        lastPublishRetryAtMs = nowMs;
        startDisplayResult = i2cModule.requestLcdText("MQTT STATUS PUBL", "", 0);
        bool publishResult = executeMqttStatusPublishAndConfirm(mainTaskEntryCpuMillis, publishSubName, publishReasonText);
        if (publishResult) {
          isStartupStatusPublished = true;
          shouldPublishReconnectStatus = false;
          startDisplayResult = i2cModule.requestLcdText("MQTT PUBLISH OK", "", 0);
          if (!isCurrentAppConfirmed) {
            // [重要] rollback 有効環境では起動安定後に現在アプリを確定する。
            const bool confirmResult = otaRollback::confirmCurrentAppIfNeeded();
            if (!confirmResult) {
              appLogError("mainTaskEntry: otaRollback::confirmCurrentAppIfNeeded failed.");
            } else {
              isCurrentAppConfirmed = true;
            }
          }
        } else {
          startDisplayResult = i2cModule.requestLcdText("MQTT PUBLISH NG", "", 0);
        }
      }
    }

    bool shouldCheckNtp = (!isUtcTimeSynchronized()) || (nowMs - lastNtpCheckAtMs >= ntpCheckIntervalMs);
    if (shouldRunNtpReconnectAfterPublish) {
      shouldCheckNtp = true;
    }
    if (shouldCheckNtp && (lastNtpRetryAtMs == 0 || (nowMs - lastNtpRetryAtMs >= ntpUnsyncedRetryIntervalMs))) {
      lastNtpRetryAtMs = nowMs;
      startDisplayResult = i2cModule.requestLcdText("NTP SYNC...", "", 0);
      bool ntpSyncResult = executeNtpSyncAndConfirm(timeServerUrl, timeServerPort, timeServerTls);
      if (ntpSyncResult) {
        lastNtpCheckAtMs = nowMs;
        shouldRunNtpReconnectAfterPublish = false;
        startDisplayResult = i2cModule.requestLcdText("NTP SYNC OK", "", 0);
      } else {
        startDisplayResult = i2cModule.requestLcdText("NTP SYNC FAIL", "", 0);
      }
    }

    appLogDebug("mainTask heartbeat.");
    //1行目時刻表示　2行目ハートビートカウント表示（エラー時はエラー番号表示）
    String timeString = getCurrentTimeString();
    char errText[8] = {};
    writeErrText(currentErrorCode, errText, sizeof(errText));
    char secondLineBuffer[17] = {};
    snprintf(secondLineBuffer,
             sizeof(secondLineBuffer),
             "HB%03lu %s",
             static_cast<unsigned long>(heartbeatCount % 1000),
             errText);
    String secondLine = String(secondLineBuffer);
    startDisplayResult = i2cModule.requestLcdText(timeString.c_str(), secondLine.c_str(), 0);
    ++heartbeatCount;
    vTaskDelay(pdMS_TO_TICKS(mainTaskIntervalMs));
  }
}

}  // namespace

/**
 * @brief Arduino初期化。mainTaskのみ起動する。
 * @return なし。
 * @details
 * - [重要] ここでは依存サービス初期化とmainTask起動のみを行い、業務処理はmainTaskへ集約する。
 */
void setup() {
  Serial.begin(serialBaudRate);
  delay(200);
  initializeLogLevel();

  // [重要] 再起動時のみ赤LEDの再起動パターンを表示する。
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason != ESP_RST_POWERON) {
    ledController::indicateRebootPattern();
  }

  bool isPsramFound = psramFound();
  appLogInfo("setup: psramFound=%d totalPsram=%u freePsram=%u freeHeap=%u",
             static_cast<int>(isPsramFound),
             static_cast<unsigned>(ESP.getPsramSize()),
             static_cast<unsigned>(ESP.getFreePsram()),
             static_cast<unsigned>(ESP.getFreeHeap()));

  certificationModule.initialize();
  filesystemModule.initialize();

  // [重要][2026-03-26] NVS Encryption (HMAC方式) の暗号化初期化。
  // sdkconfig で CONFIG_NVS_ENCRYPTION=y + CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=2 を有効にした場合、
  // Preferences::begin() より前に nvs_flash_secure_init() を呼ばなければ NVS が開けない。
  // eFuse BLOCK_KEY2 に HMAC_UP 鍵が投入済みであることが前提。
  // 工程C（本番セキュア化出荷準備試験計画書.md 5.6c）の実装。
  // [重要][2026-04-04] 最終セキュアビルドでは secure 初期化失敗時に false が返るため、
  // 以降の初期化を続行しない。開発ビルドは secureNvsInit 側で plain fallback 可否を判定する。
  const bool secureNvsInitializeResult = secureNvsInit::initializeSecureNvs();
  if (!secureNvsInitializeResult) {
    appLogError("setup: secureNvsInit::initializeSecureNvs failed. aborting startup for this build.");
    return;
  }

  sensitiveDataModule.initialize();
  messageService.initialize();
  messageService.registerTaskQueue(appTaskId::kMain, 16);

  // [重要] rollback 試験モード有効時は、起動直後に意図的再起動して未確定失敗を再現する。
  const bool rollbackInitializeResult = otaRollback::initializeOnBoot();
  if (!rollbackInitializeResult) {
    appLogError("setup: otaRollback::initializeOnBoot failed.");
  }

  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      mainTaskEntry,
      "mainTask",
      mainTaskStackSize,
      nullptr,
      mainTaskPriority,
      nullptr,
      ARDUINO_RUNNING_CORE);

  if (createTaskResult != pdPASS) {
    char errText[8] = {};
    writeErrText(static_cast<iotError::errorCodeType>(iotError::errorCodeEnum::kFatalMainTaskCreateFailed), errText, sizeof(errText));
    appLogFatal("%s setup failed. xTaskCreatePinnedToCore(mainTask) failed.", errText);
    return;
  }

  appLogInfo("setup completed. mainTask launched.");
}

/**
 * @brief Arduinoループ。タスク駆動のため待機のみ行う。
 * @return なし。
 * @note [推奨] 業務処理はmainTask/各機能タスクで実施し、loop()は軽量維持する。
 */
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
