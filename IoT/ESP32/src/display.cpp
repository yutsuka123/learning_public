/**
 * @file display.cpp
 * @brief ディスプレー表示タスクのひな形実装。
 */

#include "display.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
StackType_t* displayTaskStackBuffer = nullptr;
StaticTask_t displayTaskControlBlock;
}

bool displayTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kDisplay, 8);

  if (displayTaskStackBuffer == nullptr) {
    displayTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (displayTaskStackBuffer == nullptr) {
    appLogWarn("displayTask: PSRAM stack allocation failed. fallback to internal RAM.");
    displayTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (displayTaskStackBuffer == nullptr) {
    appLogError("displayTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "displayTask",
      taskStackSize,
      this,
      taskPriority,
      displayTaskStackBuffer,
      &displayTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("displayTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("displayTask created.");
  return true;
}

void displayTask::taskEntry(void* taskParameter) {
  displayTask* self = static_cast<displayTask*>(taskParameter);
  self->runLoop();
}

void displayTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("displayTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kDisplay, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kDisplay;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "displayTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }

    // TODO: 画面描画更新処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
