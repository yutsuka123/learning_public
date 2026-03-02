/**
 * @file main.cpp
 * @brief IoT ESP32の起動エントリ。mainTaskから各機能タスクを起動するためのひな形。
 * @details 今回はタスク起動箇所をコメント化し、将来の実装着手点のみ用意する。
 */

#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

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
constexpr uint32_t mainTaskIntervalMs = 1000;
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
/**
 * @brief 指定タスクへ起動要求メッセージを送信する。
 * @param destinationTaskId 宛先タスクID。
 * @param destinationName ログ表示用の宛先名。
 * @return 送信成功時true、失敗時false。
 */
bool sendStartupRequest(appTaskId destinationTaskId, const char* destinationName) {
  appTaskMessage startupRequestMessage{};
  startupRequestMessage.sourceTaskId = appTaskId::kMain;
  startupRequestMessage.destinationTaskId = destinationTaskId;
  startupRequestMessage.messageType = appMessageType::kStartupRequest;
  startupRequestMessage.intValue = 1;
  strncpy(startupRequestMessage.text, "startup request from main", sizeof(startupRequestMessage.text) - 1);
  startupRequestMessage.text[sizeof(startupRequestMessage.text) - 1] = '\0';

  bool sendResult = messageService.sendMessage(startupRequestMessage, pdMS_TO_TICKS(200));
  if (!sendResult) {
    appLogWarn("mainTaskEntry: startup request send failed. destination=%s", destinationName);
    return false;
  }

  appLogInfo("mainTaskEntry: startup request sent. destination=%s", destinationName);
  return true;
}

/**
 * @brief 宛先タスクから指定メッセージを待機する。
 * @param expectedSourceTaskId 期待する送信元タスクID。
 * @param expectedMessageType 期待するメッセージ種別。
 * @param timeoutMs 待機タイムアウト(ms)。
 * @param receivedMessageOut 受信メッセージ出力先（null不可）。
 * @return 期待メッセージ受信時true、タイムアウトまたは不一致継続でfalse。
 */
bool waitForExpectedMessage(appTaskId expectedSourceTaskId,
                            appMessageType expectedMessageType,
                            uint32_t timeoutMs,
                            appTaskMessage* receivedMessageOut) {
  if (receivedMessageOut == nullptr) {
    appLogError("waitForExpectedMessage failed. receivedMessageOut is null.");
    return false;
  }

  TickType_t startTick = xTaskGetTickCount();
  TickType_t timeoutTick = pdMS_TO_TICKS(timeoutMs);

  while ((xTaskGetTickCount() - startTick) < timeoutTick) {
    appTaskMessage currentMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kMain, &currentMessage, pdMS_TO_TICKS(100));
    if (!receiveResult) {
      continue;
    }

    appLogInfo("waitForExpectedMessage: received src=%d type=%d text=%s",
               static_cast<int>(currentMessage.sourceTaskId),
               static_cast<int>(currentMessage.messageType),
               currentMessage.text);

    if (currentMessage.sourceTaskId == expectedSourceTaskId && currentMessage.messageType == appMessageType::kTaskError) {
      appLogError("waitForExpectedMessage task error. source=%d detail=%s",
                  static_cast<int>(currentMessage.sourceTaskId),
                  currentMessage.text);
      return false;
    }

    if (currentMessage.sourceTaskId == expectedSourceTaskId && currentMessage.messageType == expectedMessageType) {
      *receivedMessageOut = currentMessage;
      return true;
    }
  }

  appLogError("waitForExpectedMessage timeout. expectedSource=%d expectedType=%d timeoutMs=%lu",
              static_cast<int>(expectedSourceTaskId),
              static_cast<int>(expectedMessageType),
              static_cast<unsigned long>(timeoutMs));
  return false;
}

/**
 * @brief Wi-Fi初期化要求をwifiTaskへ送信する。
 */
bool sendWifiInitRequest(const String& wifiSsid, const String& wifiPass) {
  appTaskMessage requestMessage{};
  requestMessage.sourceTaskId = appTaskId::kMain;
  requestMessage.destinationTaskId = appTaskId::kWifi;
  requestMessage.messageType = appMessageType::kWifiInitRequest;
  strncpy(requestMessage.text, wifiSsid.c_str(), sizeof(requestMessage.text) - 1);
  requestMessage.text[sizeof(requestMessage.text) - 1] = '\0';
  strncpy(requestMessage.text2, wifiPass.c_str(), sizeof(requestMessage.text2) - 1);
  requestMessage.text2[sizeof(requestMessage.text2) - 1] = '\0';

  appLogInfo("sendWifiInitRequest: request send. ssid=%s pass=%s",
             requestMessage.text,
             maskSecretForLog(String(requestMessage.text2)).c_str());

  bool sendResult = messageService.sendMessage(requestMessage, pdMS_TO_TICKS(300));
  if (!sendResult) {
    appLogError("sendWifiInitRequest failed. ssid=%s", requestMessage.text);
    return false;
  }
  return true;
}

/**
 * @brief MQTT初期化要求をmqttTaskへ送信する。
 */
bool sendMqttInitRequest(const String& mqttUrl,
                         const String& mqttUser,
                         const String& mqttPass,
                         int32_t mqttPort,
                         bool mqttTls) {
  appTaskMessage requestMessage{};
  requestMessage.sourceTaskId = appTaskId::kMain;
  requestMessage.destinationTaskId = appTaskId::kMqtt;
  requestMessage.messageType = appMessageType::kMqttInitRequest;
  requestMessage.intValue = mqttPort;
  requestMessage.boolValue = mqttTls;
  strncpy(requestMessage.text, mqttUrl.c_str(), sizeof(requestMessage.text) - 1);
  requestMessage.text[sizeof(requestMessage.text) - 1] = '\0';
  strncpy(requestMessage.text2, mqttUser.c_str(), sizeof(requestMessage.text2) - 1);
  requestMessage.text2[sizeof(requestMessage.text2) - 1] = '\0';
  strncpy(requestMessage.text3, mqttPass.c_str(), sizeof(requestMessage.text3) - 1);
  requestMessage.text3[sizeof(requestMessage.text3) - 1] = '\0';

  appLogInfo("sendMqttInitRequest: request send. url=%s user=%s pass=%s port=%ld tls=%d",
             requestMessage.text,
             requestMessage.text2,
             maskSecretForLog(String(requestMessage.text3)).c_str(),
             static_cast<long>(requestMessage.intValue),
             static_cast<int>(requestMessage.boolValue));

  bool sendResult = messageService.sendMessage(requestMessage, pdMS_TO_TICKS(300));
  if (!sendResult) {
    appLogError("sendMqttInitRequest failed. url=%s", requestMessage.text);
    return false;
  }
  return true;
}

/**
 * @brief MQTT online publish要求をmqttTaskへ送信する。
 */
bool sendMqttPublishOnlineRequest() {
  appTaskMessage requestMessage{};
  requestMessage.sourceTaskId = appTaskId::kMain;
  requestMessage.destinationTaskId = appTaskId::kMqtt;
  requestMessage.messageType = appMessageType::kMqttPublishOnlineRequest;
  requestMessage.boolValue = true;
  strncpy(requestMessage.text, "status online publish request", sizeof(requestMessage.text) - 1);
  requestMessage.text[sizeof(requestMessage.text) - 1] = '\0';

  appLogInfo("sendMqttPublishOnlineRequest: request send.");
  bool sendResult = messageService.sendMessage(requestMessage, pdMS_TO_TICKS(300));
  if (!sendResult) {
    appLogError("sendMqttPublishOnlineRequest failed.");
    return false;
  }
  return true;
}


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
 * @brief メインタスク本体。将来ここから各機能タスクを起動する。
 * @param taskParameter タスク引数（未使用）。
 * @return なし（無限ループ）。
 * @details
 * - [重要] START表示 -> Wi-Fi初期化 -> MQTT初期化 -> online publish -> DONE表示 の順序を厳守する。
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
  bool startDisplayResult = i2cModule.requestLcdText("START", "", 0);
  if (!startDisplayResult) {
    appLogWarn("mainTaskEntry: requestLcdText(START) failed.");
  }
  if (i2cLcdDiagnosticMode) {
    appLogWarn("mainTaskEntry: I2C LCD diagnostic mode enabled. normal startup is skipped.");
    uint32_t displayCounter = 0;
    for (;;) {
      char secondLineBuffer[17];
      snprintf(secondLineBuffer,
               sizeof(secondLineBuffer),
               "Counter:%lu",
               static_cast<unsigned long>(displayCounter));
      bool diagnosticDisplayResult = i2cModule.requestLcdText("hello, world!", secondLineBuffer, 0);
      if (!diagnosticDisplayResult) {
        appLogWarn("mainTaskEntry: diagnostic requestLcdText failed. counter=%lu",
                   static_cast<unsigned long>(displayCounter));
      }
      ++displayCounter;
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
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

  sendStartupRequest(appTaskId::kWifi, "wifiTask");
  sendStartupRequest(appTaskId::kMqtt, "mqttTask");
  sendStartupRequest(appTaskId::kHttp, "httpTask");
  sendStartupRequest(appTaskId::kOta, "otaTask");
  sendStartupRequest(appTaskId::kExternalDevice, "externalDeviceTask");
  sendStartupRequest(appTaskId::kDisplay, "displayTask");
  sendStartupRequest(appTaskId::kLed, "ledTask");
  sendStartupRequest(appTaskId::kInput, "inputTask");

  // [重要] wifiTaskにメッセージを投げてWi-Fi初期化を依頼する（接続情報を同梱）。
  bool wifiInitSendResult = sendWifiInitRequest(wifiSsid, wifiPass);
  if (!wifiInitSendResult) {
    ledController::indicateAbortPattern();
    appLogFatal("mainTaskEntry failed. sendWifiInitRequest returned false.");
    vTaskDelete(nullptr);
  }

  // [重要] wifiTaskからの初期化完了メッセージを待つ。
  appTaskMessage wifiInitResponseMessage{};
  bool wifiInitWaitResult = waitForExpectedMessage(
      appTaskId::kWifi, appMessageType::kWifiInitDone, 35000, &wifiInitResponseMessage);
  if (!wifiInitWaitResult) {
    ledController::indicateAbortPattern();
    appLogFatal("mainTaskEntry failed. waitForExpectedMessage(kWifiInitDone) timeout.");
    vTaskDelete(nullptr);
  }
  appLogInfo("mainTaskEntry: wifi initialization completed. detail=%s", wifiInitResponseMessage.text);

  // [重要] mqttTaskにメッセージを投げてMQTT初期化を依頼する（接続情報を同梱）。
  bool mqttInitSendResult = sendMqttInitRequest(mqttUrl, mqttUser, mqttPass, mqttPort, mqttTls);
  if (!mqttInitSendResult) {
    ledController::indicateAbortPattern();
    appLogFatal("mainTaskEntry failed. sendMqttInitRequest returned false.");
    vTaskDelete(nullptr);
  }

  // [重要] mqttTaskからの初期化完了メッセージを待つ。
  appTaskMessage mqttInitResponseMessage{};
  bool mqttInitWaitResult = waitForExpectedMessage(
      appTaskId::kMqtt, appMessageType::kMqttInitDone, 20000, &mqttInitResponseMessage);
  if (!mqttInitWaitResult) {
    ledController::indicateAbortPattern();
    appLogFatal("mainTaskEntry failed. waitForExpectedMessage(kMqttInitDone) timeout.");
    vTaskDelete(nullptr);
  }
  appLogInfo("mainTaskEntry: mqtt initialization completed. detail=%s", mqttInitResponseMessage.text);

  // [重要] mqttTaskへ「status online publish」を依頼し、完了を待つ。
  bool mqttPublishRequestResult = sendMqttPublishOnlineRequest();
  if (!mqttPublishRequestResult) {
    ledController::indicateAbortPattern();
    appLogFatal("mainTaskEntry failed. sendMqttPublishOnlineRequest returned false.");
    vTaskDelete(nullptr);
  }

  appTaskMessage mqttPublishResponseMessage{};
  bool mqttPublishWaitResult = waitForExpectedMessage(
      appTaskId::kMqtt, appMessageType::kMqttPublishOnlineDone, 20000, &mqttPublishResponseMessage);
  if (!mqttPublishWaitResult) {
    ledController::indicateAbortPattern();
    appLogFatal("mainTaskEntry failed. waitForExpectedMessage(kMqttPublishOnlineDone) timeout.");
    vTaskDelete(nullptr);
  }
  appLogInfo("mainTaskEntry: mqtt online publish completed. detail=%s", mqttPublishResponseMessage.text);
  bool doneDisplayResult = i2cModule.requestLcdText("DONE", "", 0);
  if (!doneDisplayResult) {
    appLogWarn("mainTaskEntry: requestLcdText(DONE) failed.");
  }



  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kMain, &receivedMessage, pdMS_TO_TICKS(100));
    if (receiveResult) {
      appLogInfo("mainTaskEntry: message received. src=%d dst=%d type=%d text=%s",
                 static_cast<int>(receivedMessage.sourceTaskId),
                 static_cast<int>(receivedMessage.destinationTaskId),
                 static_cast<int>(receivedMessage.messageType),
                 receivedMessage.text);
    }

    appLogDebug("mainTask heartbeat.");
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
