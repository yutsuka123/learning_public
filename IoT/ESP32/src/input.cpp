/**
 * @file input.cpp
 * @brief 入力処理タスクのひな形実装。
 */

#include "input.h"

#include "log.h"

bool inputTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "inputTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("inputTask creation failed.");
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
  appLogInfo("inputTask loop started. (skeleton)");
  for (;;) {
    // TODO: ボタン/外部入力の読み取り処理を実装する။
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
