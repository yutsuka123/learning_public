/**
 * @file mqtt.cpp
 * @brief MQTT機能のタスク実装。
 * @details
 * - [重要] mainTaskから受け取った設定でブローカー接続し、online状態をpublishする。
 * - [厳守] MQTT接続前にブローカー到達確認（TCPプローブ）を実施する。
 * - [制限] TLS(mqttTls=true)は現時点で未実装。
 */

#include "mqtt.h"

#include <esp_heap_caps.h>
#include <string.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "common.h"
#include "interTaskMessage.h"
#include "led.h"
#include "log.h"

namespace {
/** @brief mqttTask用スタック領域。PSRAM優先で確保し、失敗時は内部RAMへフォールバックする。 */
StackType_t* mqttTaskStackBuffer = nullptr;
/** @brief mqttTask用の静的タスク制御ブロック。 */
StaticTask_t mqttTaskControlBlock;
/** @brief MQTT通信用の下位TCPクライアント。 */
WiFiClient mqttNetworkClient;
/** @brief PubSubClient本体。mqttNetworkClientを介して通信する。 */
PubSubClient mqttClient(mqttNetworkClient);

/** @brief MQTT接続先ホスト名/IP文字列バッファ。 */
char mqttHost[64] = {0};
/** @brief MQTT接続ユーザー名バッファ。 */
char mqttUser[64] = {0};
/** @brief MQTT接続パスワードバッファ。 */
char mqttPass[64] = {0};
/** @brief MQTT接続先ポート番号。 */
int32_t mqttPort = 1883;
/** @brief TLS利用フラグ（未実装）。 */
bool mqttTls = false;
/** @brief MQTT初期化が成功済みかどうか。 */
bool isMqttInitialized = false;

/**
 * @brief MQTTブローカーへの到達確認を実施する。
 * @param brokerHost ブローカーのホスト名またはIP。
 * @return 到達確認成功でtrue、失敗時false。
 */
bool pingBrokerHost(const char* brokerHost) {
  if (brokerHost == nullptr || strlen(brokerHost) == 0) {
    appLogError("pingBrokerHost failed. brokerHost is null or empty.");
    return false;
  }

  IPAddress brokerIpAddress;
  bool resolveResult = WiFi.hostByName(brokerHost, brokerIpAddress);
  if (!resolveResult) {
    appLogError("pingBrokerHost failed. hostByName failed. brokerHost=%s", brokerHost);
    return false;
  }
  appLogInfo("pingBrokerHost start. brokerHost=%s resolvedIp=%s",
             brokerHost,
             brokerIpAddress.toString().c_str());

  // [重要] Arduino-ESP32 2.0.17ではICMP APIの利用が難しいため、TCP到達確認をping代替とする。
  WiFiClient probeClient;
  bool probeResult = probeClient.connect(brokerHost, static_cast<uint16_t>(mqttPort));
  if (!probeResult) {
    appLogError("pingBrokerHost failed (tcp-probe). brokerHost=%s brokerPort=%ld", brokerHost, static_cast<long>(mqttPort));
    return false;
  }

  appLogInfo("pingBrokerHost success (tcp-probe). brokerHost=%s brokerPort=%ld", brokerHost, static_cast<long>(mqttPort));
  probeClient.stop();
  return true;
}

/**
 * @brief MQTT接続情報を内部バッファへ保存する。
 * @param receivedMessage mainTaskから受信したMQTT初期化要求メッセージ。
 */
void storeMqttConfig(const appTaskMessage& receivedMessage) {
  strncpy(mqttHost, receivedMessage.text, sizeof(mqttHost) - 1);
  mqttHost[sizeof(mqttHost) - 1] = '\0';
  strncpy(mqttUser, receivedMessage.text2, sizeof(mqttUser) - 1);
  mqttUser[sizeof(mqttUser) - 1] = '\0';
  strncpy(mqttPass, receivedMessage.text3, sizeof(mqttPass) - 1);
  mqttPass[sizeof(mqttPass) - 1] = '\0';
  mqttPort = receivedMessage.intValue;
  mqttTls = receivedMessage.boolValue;
}

/**
 * @brief MQTTブローカーへ接続する。
 * @return 接続成功時true、失敗時false。
 */
bool connectToMqttBroker() {
  constexpr int32_t maxRetryCount = 10;
  constexpr int32_t retryDelayMs = 200;

  if (strlen(mqttHost) == 0) {
    appLogError("connectToMqttBroker failed. host is empty.");
    return false;
  }
  if (mqttPort <= 0 || mqttPort > 65535) {
    appLogError("connectToMqttBroker failed. invalid port=%ld", static_cast<long>(mqttPort));
    return false;
  }
  if (mqttTls) {
    appLogError("connectToMqttBroker failed. mqttTls=true is not implemented yet.");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    appLogError("connectToMqttBroker failed. wifi is not connected. wifiStatus=%d", static_cast<int>(WiFi.status()));
    ledController::indicateErrorPattern();
    return false;
  }

  ledController::indicateMqttConnecting();
  ledController::indicateCommunicationActivity();
  bool pingResult = pingBrokerHost(mqttHost);
  if (!pingResult) {
    appLogError("connectToMqttBroker failed. ping to brokerHost=%s did not succeed.", mqttHost);
    ledController::indicateErrorPattern();
    return false;
  }

  mqttClient.setServer(mqttHost, static_cast<uint16_t>(mqttPort));
  String clientId = "esp32lab-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  appLogInfo("connectToMqttBroker start. host=%s port=%ld user=%s pass=%s clientId=%s",
             mqttHost,
             static_cast<long>(mqttPort),
             (strlen(mqttUser) > 0 ? mqttUser : "(empty)"),
             (strlen(mqttPass) > 0 ? "******" : "(empty)"),
             clientId.c_str());

  for (int32_t retryIndex = 0; retryIndex < maxRetryCount; ++retryIndex) {
    ledController::indicateMqttConnecting();
    ledController::indicateCommunicationActivity();
    bool connectResult = false;
    if (strlen(mqttUser) > 0 || strlen(mqttPass) > 0) {
      connectResult = mqttClient.connect(clientId.c_str(), mqttUser, mqttPass);
    } else {
      connectResult = mqttClient.connect(clientId.c_str());
    }

    if (connectResult) {
      ledController::indicateMqttConnected();
      appLogInfo("connectToMqttBroker success. state=%d", mqttClient.state());
      return true;
    }

    appLogWarn("connectToMqttBroker retry. retry=%ld state=%d", static_cast<long>(retryIndex + 1), mqttClient.state());
    vTaskDelay(pdMS_TO_TICKS(retryDelayMs));
  }

  appLogError("connectToMqttBroker failed. state=%d", mqttClient.state());
  ledController::indicateErrorPattern();
  return false;
}

/**
 * @brief MQTTへonlineステータスを初回publishする。
 * @return publish成功時true、失敗時false。
 */
bool publishOnlineStatus() {
  if (!mqttClient.connected()) {
    appLogError("publishOnlineStatus failed. mqtt is not connected.");
    return false;
  }

  char topicBuffer[96];
  snprintf(topicBuffer, sizeof(topicBuffer), "%s%s", iotCommon::mqtt::kTopicPrefixNotice, "status");
  const char* payload = "{\"status\":\"online\"}";
  bool publishResult = mqttClient.publish(topicBuffer, payload, true);
  ledController::indicateCommunicationActivity();
  if (!publishResult) {
    appLogError("publishOnlineStatus failed. topic=%s", topicBuffer);
    return false;
  }

  mqttClient.loop();
  appLogInfo("publishOnlineStatus success. topic=%s payload=%s", topicBuffer, payload);
  return true;
}
}

/**
 * @brief MQTTタスクを生成し、受信用キューを登録する。
 * @return 生成成功時true、失敗時false。
 */
bool mqttTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kMqtt, 8);

  if (mqttTaskStackBuffer == nullptr) {
    mqttTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (mqttTaskStackBuffer == nullptr) {
    appLogWarn("mqttTask: PSRAM stack allocation failed. fallback to internal RAM.");
    mqttTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (mqttTaskStackBuffer == nullptr) {
    appLogError("mqttTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "mqttTask",
      taskStackSize,
      this,
      taskPriority,
      mqttTaskStackBuffer,
      &mqttTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("mqttTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("mqttTask created.");
  return true;
}

/**
 * @brief FreeRTOSタスクエントリ。
 * @param taskParameter thisポインタ。
 */
void mqttTask::taskEntry(void* taskParameter) {
  mqttTask* self = static_cast<mqttTask*>(taskParameter);
  self->runLoop();
}

/**
 * @brief MQTTタスクの常駐ループ。
 * @details
 * - 起動要求受信時: startup ackを返信。
 * - MQTT初期化要求受信時: 接続結果を返信。
 * - online publish要求受信時: publish結果を返信。
 */
void mqttTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("mqttTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kMqtt, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kMqtt;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "mqttTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kMqttInitRequest) {
      appLogInfo("mqttTask: init request received. url=%s user=%s pass=%s port=%ld tls=%d",
                 receivedMessage.text,
                 receivedMessage.text2,
                 (strlen(receivedMessage.text3) > 0 ? "******" : "(empty)"),
                 static_cast<long>(receivedMessage.intValue),
                 static_cast<int>(receivedMessage.boolValue));

      storeMqttConfig(receivedMessage);
      bool initResult = connectToMqttBroker();
      isMqttInitialized = initResult;

      appTaskMessage doneMessage{};
      doneMessage.sourceTaskId = appTaskId::kMqtt;
      doneMessage.destinationTaskId = appTaskId::kMain;
      doneMessage.messageType = initResult ? appMessageType::kMqttInitDone : appMessageType::kTaskError;
      doneMessage.intValue = initResult ? 1 : 0;
      strncpy(doneMessage.text, initResult ? "mqtt init done" : "mqtt init failed", sizeof(doneMessage.text) - 1);
      doneMessage.text[sizeof(doneMessage.text) - 1] = '\0';
      bool sendResult = messageService.sendMessage(doneMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("mqttTask: failed to send mqtt init response.");
      } else {
        appLogInfo("mqttTask: mqtt init response sent. type=%d detail=%s",
                   static_cast<int>(doneMessage.messageType),
                   doneMessage.text);
      }
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kMqttPublishOnlineRequest) {
      appLogInfo("mqttTask: publish online request received. message=%s", receivedMessage.text);

      bool publishResult = isMqttInitialized && publishOnlineStatus();
      appTaskMessage doneMessage{};
      doneMessage.sourceTaskId = appTaskId::kMqtt;
      doneMessage.destinationTaskId = appTaskId::kMain;
      doneMessage.messageType = publishResult ? appMessageType::kMqttPublishOnlineDone : appMessageType::kTaskError;
      doneMessage.intValue = publishResult ? 1 : 0;
      strncpy(doneMessage.text, publishResult ? "mqtt online publish done" : "mqtt online publish failed", sizeof(doneMessage.text) - 1);
      doneMessage.text[sizeof(doneMessage.text) - 1] = '\0';
      bool sendResult = messageService.sendMessage(doneMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("mqttTask: failed to send mqtt publish response.");
      } else {
        appLogInfo("mqttTask: mqtt publish response sent. type=%d detail=%s",
                   static_cast<int>(doneMessage.messageType),
                   doneMessage.text);
      }
    }

    // TODO: MQTT初期化、接続、subscribe/publish処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
