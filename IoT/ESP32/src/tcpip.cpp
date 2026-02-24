/**
 * @file tcpip.cpp
 * @brief TCP/IP機能のタスクひな形実装。
 */

#include "tcpip.h"

#include "log.h"

bool tcpipTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "tcpipTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("tcpipTask creation failed.");
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
  appLogInfo("tcpipTask loop started. (optional skeleton)");
  for (;;) {
    // TODO: 必要な場合のみ、低レベルTCP/IP管理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
