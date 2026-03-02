/**
 * @file input.cpp
 * @brief 入力処理タスクのひな形実装。
 */

#include "input.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
StackType_t* inputTaskStackBuffer = nullptr;
StaticTask_t inputTaskControlBlock;
}

bool inputTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kInput, 8);

  if (inputTaskStackBuffer == nullptr) {
    inputTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (inputTaskStackBuffer == nullptr) {
    appLogWarn("inputTask: PSRAM stack allocation failed. fallback to internal RAM.");
    inputTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (inputTaskStackBuffer == nullptr) {
    appLogError("inputTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "inputTask",
      taskStackSize,
      this,
      taskPriority,
      inputTaskStackBuffer,
      &inputTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("inputTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("inputTask created.");
  return true;
}

void inputTask::taskEntry(void* taskParameter) {
  inputTask* self = static_cast<inputTask*>(taskParameter);
  self->runLoop();
}

void inputTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("inputTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kInput, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kInput;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "inputTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }

    // TODO: ボタン/外部入力の読み取り処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
