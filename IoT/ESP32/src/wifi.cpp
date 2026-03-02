/**
 * @file wifi.cpp
 * @brief Wi-Fi機能のタスクひな形実装。
 */

#include "../header/wifi.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
StackType_t* wifiTaskStackBuffer = nullptr;
StaticTask_t wifiTaskControlBlock;
}

bool wifiTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kWifi, 8);

  if (wifiTaskStackBuffer == nullptr) {
    wifiTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (wifiTaskStackBuffer == nullptr) {
    appLogError("wifiTask creation failed. heap_caps_malloc stack failed.");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "wifiTask",
      taskStackSize,
      this,
      taskPriority,
      wifiTaskStackBuffer,
      &wifiTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("wifiTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("wifiTask created.");
  return true;
}

void wifiTask::taskEntry(void* taskParameter) {
  wifiTask* self = static_cast<wifiTask*>(taskParameter);
  self->runLoop();
}

void wifiTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("wifiTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kWifi, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kWifi;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "wifiTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kWifiInitRequest) {
      appLogInfo("wifiTask: init request received. ssid=%s pass=%s",
                 receivedMessage.text,
                 (strlen(receivedMessage.text2) > 0 ? "******" : "(empty)"));

      // TODO: ここで receivedMessage.text / text2 を使って実際のWi-Fi接続を実装する。
      appTaskMessage doneMessage{};
      doneMessage.sourceTaskId = appTaskId::kWifi;
      doneMessage.destinationTaskId = appTaskId::kMain;
      doneMessage.messageType = appMessageType::kWifiInitDone;
      doneMessage.intValue = 1;
      strncpy(doneMessage.text, "wifi init done", sizeof(doneMessage.text) - 1);
      doneMessage.text[sizeof(doneMessage.text) - 1] = '\0';
      bool sendResult = messageService.sendMessage(doneMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("wifiTask: failed to send kWifiInitDone.");
      } else {
        appLogInfo("wifiTask: sent kWifiInitDone.");
      }
    }

    // TODO: Wi-Fi初期化、AP接続、再接続制御を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
