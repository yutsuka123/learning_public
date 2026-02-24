/**
 * @file display.cpp
 * @brief ディスプレー表示タスクのひな形実装。
 */

#include "display.h"

#include "log.h"

bool displayTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "displayTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("displayTask creation failed.");
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
  appLogInfo("displayTask loop started. (skeleton)");
  for (;;) {
    // TODO: 画面描画更新処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
