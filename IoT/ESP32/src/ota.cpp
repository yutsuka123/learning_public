/**
 * @file ota.cpp
 * @brief OTA更新機能のタスクひな形実装。
 */

#include "ota.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
StackType_t* otaTaskStackBuffer = nullptr;
StaticTask_t otaTaskControlBlock;
}

bool otaTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kOta, 8);

  if (otaTaskStackBuffer == nullptr) {
    otaTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (otaTaskStackBuffer == nullptr) {
    appLogWarn("otaTask: PSRAM stack allocation failed. fallback to internal RAM.");
    otaTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (otaTaskStackBuffer == nullptr) {
    appLogError("otaTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "otaTask",
      taskStackSize,
      this,
      taskPriority,
      otaTaskStackBuffer,
      &otaTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("otaTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("otaTask created.");
  return true;
}

void otaTask::taskEntry(void* taskParameter) {
  otaTask* self = static_cast<otaTask*>(taskParameter);
  self->runLoop();
}

void otaTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("otaTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kOta, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kOta;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "otaTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }

    // TODO: OTA判定、ダウンロード、検証、適用処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
