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

#include "certification.h"
#include "display.h"
#include "externalDevice.h"
#include "filesystem.h"
#include "http.h"
#include "i2c.h"
#include "input.h"
#include "interTaskMessage.h"
#include "led.h"
#include "log.h"
#include "mqtt.h"
#include "ota.h"
#include "sensitiveData.h"
#include "sensitiveDataService.h"
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
 * @note [重要] 初期化シーケンスが重いため8192を下限として運用する。
 */
constexpr uint32_t mainTaskStackSize = 8192;
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
 * @brief LCD診断モードの有効/無効。
 * @type bool
 * @note [推奨] 通常運用ではfalseを維持する。
 */
constexpr bool i2cLcdDiagnosticMode = false;

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
  time_t currentEpochSeconds = time(nullptr);
  if (currentEpochSeconds <= 0) {
    return "TIME UNSYNC";
  }

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
 * @brief メインタスク本体。将来ここから各機能タスクを起動する。
 * @param taskParameter タスク引数（未使用）。
 * @return なし（無限ループ）。
 * @details
 * - [重要] START表示 -> Wi-Fi初期化 -> MQTT初期化 -> online publish -> 初回時刻同期 -> DONE表示 の順序を厳守する。
 * - [厳守] 各段階の失敗時は赤LEDアボートパターンを表示し、タスクを終了する。
 */
void mainTaskEntry(void* taskParameter) {
  (void)taskParameter;

  // [重要] 起動時は青LEDを一旦消灯後0.5秒待機してから点灯する。
  ledController::initializeByMainOnBoot();
  appLogInfo("mainTask started.");

  bool i2cStartResult = i2cModule.startTask();
  if (!i2cStartResult) {
    ledController::indicateAbortPattern();
    appLogFatal("mainTaskEntry failed. i2cModule.startTask returned false.");
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


  // [重要] 起動時に機密設定を読み込み、将来NVS移行時も同等手順を維持する。
  // [補足] ここで読み込む値は後段のWi-Fi/MQTT初期化メッセージへそのまま搭載される。
  String wifiSsid;
  String wifiPass;
  String mqttUrl;
  String mqttUser;
  String mqttPass;
  int32_t mqttPort = 0;
  bool mqttTls = false;
  String timeServerUrl;
  String timeServerUser;
  String timeServerPass;
  int32_t timeServerPort = 123;
  bool timeServerTls = false;

  bool wifiLoadResult = sensitiveDataModule.loadWifiCredentials(&wifiSsid, &wifiPass);
  if (!wifiLoadResult) {
    appLogWarn("mainTaskEntry: loadWifiCredentials failed. fallback empty values.");
  }

  bool mqttLoadResult = sensitiveDataModule.loadMqttConfig(&mqttUrl, &mqttUser, &mqttPass, &mqttPort, &mqttTls);
  if (!mqttLoadResult) {
    appLogWarn("mainTaskEntry: loadMqttConfig failed. fallback default values.");
    mqttPort = 8883;
    mqttTls = false;
  }

  bool timeServerLoadResult = sensitiveDataModule.loadTimeServerConfig(
      &timeServerUrl,
      &timeServerUser,
      &timeServerPass,
      &timeServerPort,
      &timeServerTls);
  if (!timeServerLoadResult) {
    appLogWarn("mainTaskEntry: loadTimeServerConfig failed. fallback default values.");
    timeServerUrl = "";
    timeServerUser = "";
    timeServerPass = "";
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
  wifiSsid = SENSITIVE_WIFI_SSID;
  wifiPass = SENSITIVE_WIFI_PASS;
  mqttUrl = SENSITIVE_MQTT_URL;
  mqttUser = SENSITIVE_MQTT_USER;
  mqttPass = SENSITIVE_MQTT_PASS;
  mqttPort = static_cast<int32_t>(SENSITIVE_MQTT_PORT);
  mqttTls = (SENSITIVE_MQTT_TLS != 0);
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
  appLogInfo("mainTaskEntry: time server loaded. url=%s, user=%s, pass=%s, port=%ld, tls=%d",
             timeServerUrl.c_str(),
             timeServerUser.c_str(),
             maskSecretForLog(timeServerPass).c_str(),
             static_cast<long>(timeServerPort),
             static_cast<int>(timeServerTls));

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


  startDisplayResult = i2cModule.requestLcdText("WIFI SEARCHING...", "", 0);

  // [重要] wifiTaskにメッセージを投げてWi-Fi初期化を依頼する（接続情報を同梱）。
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
    ledController::indicateAbortPattern();
    startDisplayResult = i2cModule.requestLcdText("WIFI SEARCHING FAILED", "", 0);
    appLogFatal("mainTaskEntry failed. appUtil::sendMessage(kWifiInitRequest) returned false.");
    vTaskDelete(nullptr);
  }

  // [重要] wifiTaskからの初期化完了メッセージを待つ。
  appTaskMessage wifiInitResponseMessage{};
  bool wifiInitWaitResult = appUtil::waitMessage(
      appTaskId::kWifi,
      appTaskId::kMain,
      appMessageType::kWifiInitDone,
      nullptr,
      35000,
      &wifiInitResponseMessage);
  if (!wifiInitWaitResult) {
    ledController::indicateAbortPattern();
    startDisplayResult = i2cModule.requestLcdText("WIFI SEARCHING TIMEOUT", "", 0);
    appLogFatal("mainTaskEntry failed. appUtil::waitMessage(kWifiInitDone) timeout.");
    vTaskDelete(nullptr);
  }
  startDisplayResult = i2cModule.requestLcdText("WIFI CONNECTED", "", 0);

  appLogInfo("mainTaskEntry: wifi initialization completed. detail=%s", wifiInitResponseMessage.text);

  // [重要] mqttTaskにメッセージを投げてMQTT初期化を依頼する（接続情報を同梱）。
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
    ledController::indicateAbortPattern();
    startDisplayResult = i2cModule.requestLcdText("MQTT INIT FAILED", "", 0);
    appLogFatal("mainTaskEntry failed. appUtil::sendMessage(kMqttInitRequest) returned false.");
    vTaskDelete(nullptr);
  }
  startDisplayResult = i2cModule.requestLcdText("MQTT INITIALIZING...", "", 0);

  // [重要] mqttTaskからの初期化完了メッセージを待つ。
  appTaskMessage mqttInitResponseMessage{};
  bool mqttInitWaitResult = appUtil::waitMessage(
      appTaskId::kMqtt,
      appTaskId::kMain,
      appMessageType::kMqttInitDone,
      nullptr,
      20000,
      &mqttInitResponseMessage);
  if (!mqttInitWaitResult) {
    ledController::indicateAbortPattern();
    startDisplayResult = i2cModule.requestLcdText("MQTT INITIALIZATION TIMEOUT", "", 0);
    appLogFatal("mainTaskEntry failed. appUtil::waitMessage(kMqttInitDone) timeout.");
    vTaskDelete(nullptr);
  }
  startDisplayResult = i2cModule.requestLcdText("MQTT INITIALIZED", "", 0);
  appLogInfo("mainTaskEntry: mqtt initialization completed. detail=%s", mqttInitResponseMessage.text);

  // [重要] mqttTaskへ「status online publish」を依頼し、完了を待つ。
  appUtil::appTaskMessageDetail mqttPublishDetail = appUtil::createEmptyMessageDetail();
  mqttPublishDetail.hasBoolValue = true;
  mqttPublishDetail.boolValue = true;
  mqttPublishDetail.text = "status online publish request";
  bool mqttPublishRequestResult = appUtil::sendMessage(
      appTaskId::kMqtt,
      appTaskId::kMain,
      appMessageType::kMqttPublishOnlineRequest,
      &mqttPublishDetail,
      300);
  if (!mqttPublishRequestResult) {
    ledController::indicateAbortPattern();
    startDisplayResult = i2cModule.requestLcdText("MQTT ONLINE PUBLISH FAILED", "", 0);
    appLogFatal("mainTaskEntry failed. appUtil::sendMessage(kMqttPublishOnlineRequest) returned false.");
    vTaskDelete(nullptr);
  }
  startDisplayResult = i2cModule.requestLcdText("MQTT ONLINE PUBLISHING", "", 0);

  appTaskMessage mqttPublishResponseMessage{};
  bool mqttPublishWaitResult = appUtil::waitMessage(
      appTaskId::kMqtt,
      appTaskId::kMain,
      appMessageType::kMqttPublishOnlineDone,
      nullptr,
      20000,
      &mqttPublishResponseMessage);
  if (!mqttPublishWaitResult) {
    ledController::indicateAbortPattern();
    startDisplayResult = i2cModule.requestLcdText("MQTT ONLINE PUBLISHING TIMEOUT", "", 0);
    appLogFatal("mainTaskEntry failed. appUtil::waitMessage(kMqttPublishOnlineDone) timeout.");
    vTaskDelete(nullptr);
  }
  appLogInfo("mainTaskEntry: mqtt online publish completed. detail=%s", mqttPublishResponseMessage.text);

  // [重要] タイムサーバーへ接続して初回時刻同期を行う。同期後は内部時刻をUTCで継続運用する。
  appUtil::appTaskMessageDetail timeServerInitDetail = appUtil::createEmptyMessageDetail();
  timeServerInitDetail.text = timeServerUrl.c_str();
  timeServerInitDetail.text2 = timeServerUser.c_str();
  timeServerInitDetail.text3 = timeServerPass.c_str();
  timeServerInitDetail.hasIntValue = true;
  timeServerInitDetail.intValue = timeServerPort;
  timeServerInitDetail.hasBoolValue = true;
  timeServerInitDetail.boolValue = timeServerTls;
  appLogInfo("mainTaskEntry: time server init request send. url=%s user=%s pass=%s port=%ld tls=%d",
             timeServerUrl.c_str(),
             timeServerUser.c_str(),
             maskSecretForLog(timeServerPass).c_str(),
             static_cast<long>(timeServerPort),
             static_cast<int>(timeServerTls));
  bool timeServerInitSendResult = appUtil::sendMessage(
      appTaskId::kTimeServer,
      appTaskId::kMain,
      appMessageType::kTimeServerInitRequest,
      &timeServerInitDetail,
      300);
  if (!timeServerInitSendResult) {
    startDisplayResult = i2cModule.requestLcdText("TIME SERVER INIT FAILED", "", 0);
    appLogWarn("mainTaskEntry: appUtil::sendMessage(kTimeServerInitRequest) returned false. time sync is skipped.");
  } else {
    appTaskMessage timeServerInitResponseMessage{};
    bool timeServerInitWaitResult = appUtil::waitMessage(
        appTaskId::kTimeServer,
        appTaskId::kMain,
        appMessageType::kTimeServerInitDone,
        nullptr,
        20000,
        &timeServerInitResponseMessage);
    if (!timeServerInitWaitResult) {
      appLogWarn("mainTaskEntry: appUtil::waitMessage(kTimeServerInitDone) timeout. time sync will be retried by timeServerTask.");
      startDisplayResult = i2cModule.requestLcdText("TIME SERVER INITIALIZATION TIMEOUT", "", 0);
    } else if (timeServerInitResponseMessage.intValue == 1) {
      appLogInfo("mainTaskEntry: time server initialization completed. detail=%s utcNow=%s",
                 timeServerInitResponseMessage.text,
                 timeServerInitResponseMessage.text2);
      startDisplayResult = i2cModule.requestLcdText("TIME SERVER INITIALIZED", "", 0);
    } else {
      appLogWarn("mainTaskEntry: time server initialization failed. detail=%s utcNow=%s",
                 timeServerInitResponseMessage.text,
                 timeServerInitResponseMessage.text2);
      startDisplayResult = i2cModule.requestLcdText("TIME SERVER INITIALIZATION FAILED", "", 0);
    }
    startDisplayResult = i2cModule.requestLcdText("TIME SERVER INITIALIZED", "", 0);
  }

  for (;;) {
    static uint32_t heartbeatCount = 0;
    static uint32_t errorCount = 0;

    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kMain, &receivedMessage, pdMS_TO_TICKS(100));
    if (receiveResult) {
      appLogInfo("mainTaskEntry: message received. src=%d dst=%d type=%d text=%s",
                 static_cast<int>(receivedMessage.sourceTaskId),
                 static_cast<int>(receivedMessage.destinationTaskId),
                 static_cast<int>(receivedMessage.messageType),
                 receivedMessage.text);
      if (receivedMessage.messageType == appMessageType::kTaskError) {
        ++errorCount;
      }
    }

    appLogDebug("mainTask heartbeat.");
    //1行目時刻表示　2行目ハートビートカウント表示（エラー時はエラー番号表示）
    String timeString = getCurrentTimeString();
    String heartbeatCountString = String(heartbeatCount);
    String errorString = String(errorCount);
    String secondLine = String("HB ") + heartbeatCountString + String(" ERR ") + errorString;
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
  sensitiveDataModule.initialize();
  messageService.initialize();
  messageService.registerTaskQueue(appTaskId::kMain, 16);

  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      mainTaskEntry,
      "mainTask",
      mainTaskStackSize,
      nullptr,
      mainTaskPriority,
      nullptr,
      ARDUINO_RUNNING_CORE);

  if (createTaskResult != pdPASS) {
    appLogFatal("setup failed. xTaskCreatePinnedToCore(mainTask) failed.");
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
