/**
 * @file externalDevice.cpp
 * @brief 外部デバイス管理タスクのひな形実装。
 */

#include "externalDevice.h"

#include "log.h"

bool externalDeviceTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "externalDeviceTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("externalDeviceTask creation failed.");
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
  appLogInfo("externalDeviceTask loop started. (skeleton)");
  for (;;) {
    // TODO: センサー/アクチュエータ等の外部デバイス管理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
