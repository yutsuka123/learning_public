/**
 * @file tcpip.cpp
 * @brief TCP/IP機能のタスクひな形実装。
 */

#include "tcpip.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
StackType_t* tcpipTaskStackBuffer = nullptr;
StaticTask_t tcpipTaskControlBlock;
}

bool tcpipTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kTcpip, 8);

  if (tcpipTaskStackBuffer == nullptr) {
    tcpipTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (tcpipTaskStackBuffer == nullptr) {
    appLogWarn("tcpipTask: PSRAM stack allocation failed. fallback to internal RAM.");
    tcpipTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (tcpipTaskStackBuffer == nullptr) {
    appLogError("tcpipTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "tcpipTask",
      taskStackSize,
      this,
      taskPriority,
      tcpipTaskStackBuffer,
      &tcpipTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("tcpipTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("tcpipTask created.");
  return true;
}

void tcpipTask::taskEntry(void* taskParameter) {
  tcpipTask* self = static_cast<tcpipTask*>(taskParameter);
  self->runLoop();
}

void tcpipTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("tcpipTask loop started. (optional skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kTcpip, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kTcpip;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "tcpipTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }

    // TODO: 必要な場合のみ、低レベルTCP/IP管理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
