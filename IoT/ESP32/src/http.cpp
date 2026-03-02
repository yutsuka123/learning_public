/**
 * @file http.cpp
 * @brief HTTP/HTTPS機能のタスクひな形実装。
 */

#include "http.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "led.h"
#include "log.h"

namespace {
StackType_t* httpTaskStackBuffer = nullptr;
StaticTask_t httpTaskControlBlock;
}

bool httpTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kHttp, 8);

  if (httpTaskStackBuffer == nullptr) {
    httpTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (httpTaskStackBuffer == nullptr) {
    appLogWarn("httpTask: PSRAM stack allocation failed. fallback to internal RAM.");
    httpTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (httpTaskStackBuffer == nullptr) {
    appLogError("httpTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "httpTask",
      taskStackSize,
      this,
      taskPriority,
      httpTaskStackBuffer,
      &httpTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("httpTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("httpTask created.");
  return true;
}

void httpTask::taskEntry(void* taskParameter) {
  httpTask* self = static_cast<httpTask*>(taskParameter);
  self->runLoop();
}

void httpTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("httpTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kHttp, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kHttp;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "httpTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }
    if (receiveResult && receivedMessage.messageType != appMessageType::kStartupRequest) {
      // [将来対応] HTTP通信の実処理に入るタイミングで通信アクティビティ表示を行う。
      ledController::indicateCommunicationActivity();
    }

    // TODO: HTTP/HTTPSクライアント・サーバ処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
