/**
 * @file ota.cpp
 * @brief OTA更新機能のタスクひな形実装。
 */

#include "ota.h"

#include "log.h"

bool otaTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "otaTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("otaTask creation failed.");
    return false;
  }
  appLogInfo("otaTask created.");
  return true;
}

void otaTask::taskEntry(void* taskParameter) {
  otaTask* self = static_cast<otaTask*>(taskParameter);
  self->runLoop();
}

void otaTask::runLoop() {
  appLogInfo("otaTask loop started. (skeleton)");
  for (;;) {
    // TODO: OTA判定、ダウンロード、検証、適用処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
