/**
 * @file led.cpp
 * @brief LED表示タスクのひな形実装。
 */

#include "led.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
StackType_t* ledTaskStackBuffer = nullptr;
StaticTask_t ledTaskControlBlock;
}

bool ledTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kLed, 8);

  if (ledTaskStackBuffer == nullptr) {
    ledTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (ledTaskStackBuffer == nullptr) {
    appLogWarn("ledTask: PSRAM stack allocation failed. fallback to internal RAM.");
    ledTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (ledTaskStackBuffer == nullptr) {
    appLogError("ledTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "ledTask",
      taskStackSize,
      this,
      taskPriority,
      ledTaskStackBuffer,
      &ledTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("ledTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("ledTask created.");
  return true;
}

void ledTask::taskEntry(void* taskParameter) {
  ledTask* self = static_cast<ledTask*>(taskParameter);
  self->runLoop();
}

void ledTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("ledTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kLed, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kLed;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "ledTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }

    // TODO: LED表示パターン制御を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
