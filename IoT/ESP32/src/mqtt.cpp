/**
 * @file mqtt.cpp
 * @brief MQTT機能のタスクひな形実装。
 */

#include "mqtt.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
StackType_t* mqttTaskStackBuffer = nullptr;
StaticTask_t mqttTaskControlBlock;
}

bool mqttTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kMqtt, 8);

  if (mqttTaskStackBuffer == nullptr) {
    mqttTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (mqttTaskStackBuffer == nullptr) {
    appLogError("mqttTask creation failed. heap_caps_malloc stack failed.");
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

void mqttTask::taskEntry(void* taskParameter) {
  mqttTask* self = static_cast<mqttTask*>(taskParameter);
  self->runLoop();
}

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

      // TODO: ここで receivedMessage の接続情報を使ってMQTT初期化処理を実装する。
      appTaskMessage doneMessage{};
      doneMessage.sourceTaskId = appTaskId::kMqtt;
      doneMessage.destinationTaskId = appTaskId::kMain;
      doneMessage.messageType = appMessageType::kMqttInitDone;
      doneMessage.intValue = 1;
      strncpy(doneMessage.text, "mqtt init done", sizeof(doneMessage.text) - 1);
      doneMessage.text[sizeof(doneMessage.text) - 1] = '\0';
      bool sendResult = messageService.sendMessage(doneMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("mqttTask: failed to send kMqttInitDone.");
      } else {
        appLogInfo("mqttTask: sent kMqttInitDone.");
      }
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kMqttPublishOnlineRequest) {
      appLogInfo("mqttTask: publish online request received. message=%s", receivedMessage.text);

      // TODO: ここでstatus/onlineをMQTT Brokerへpublishする実処理を実装する。
      appTaskMessage doneMessage{};
      doneMessage.sourceTaskId = appTaskId::kMqtt;
      doneMessage.destinationTaskId = appTaskId::kMain;
      doneMessage.messageType = appMessageType::kMqttPublishOnlineDone;
      doneMessage.intValue = 1;
      strncpy(doneMessage.text, "mqtt online publish done", sizeof(doneMessage.text) - 1);
      doneMessage.text[sizeof(doneMessage.text) - 1] = '\0';
      bool sendResult = messageService.sendMessage(doneMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("mqttTask: failed to send kMqttPublishOnlineDone.");
      } else {
        appLogInfo("mqttTask: sent kMqttPublishOnlineDone.");
      }
    }

    // TODO: MQTT初期化、接続、subscribe/publish処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
