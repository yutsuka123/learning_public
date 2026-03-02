/**
 * @file externalDevice.cpp
 * @brief 外部デバイス管理タスクのひな形実装。
 */

#include "externalDevice.h"

#include <esp_heap_caps.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
StackType_t* externalDeviceTaskStackBuffer = nullptr;
StaticTask_t externalDeviceTaskControlBlock;
}

bool externalDeviceTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kExternalDevice, 8);

  if (externalDeviceTaskStackBuffer == nullptr) {
    externalDeviceTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (externalDeviceTaskStackBuffer == nullptr) {
    appLogWarn("externalDeviceTask: PSRAM stack allocation failed. fallback to internal RAM.");
    externalDeviceTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (externalDeviceTaskStackBuffer == nullptr) {
    appLogError("externalDeviceTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "externalDeviceTask",
      taskStackSize,
      this,
      taskPriority,
      externalDeviceTaskStackBuffer,
      &externalDeviceTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("externalDeviceTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("externalDeviceTask created.");
  return true;
}

void externalDeviceTask::taskEntry(void* taskParameter) {
  externalDeviceTask* self = static_cast<externalDeviceTask*>(taskParameter);
  self->runLoop();
}

void externalDeviceTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("externalDeviceTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kExternalDevice, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kExternalDevice;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "externalDeviceTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }

    // TODO: センサー/アクチュエータ等の外部デバイス管理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
