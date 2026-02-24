/**
 * @file led.cpp
 * @brief LED表示タスクのひな形実装。
 */

#include "led.h"

#include "log.h"

bool ledTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "ledTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("ledTask creation failed.");
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
  appLogInfo("ledTask loop started. (skeleton)");
  for (;;) {
    // TODO: LED表示パターン制御を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
