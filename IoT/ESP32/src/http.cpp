/**
 * @file http.cpp
 * @brief HTTP/HTTPS機能のタスクひな形実装。
 */

#include "http.h"

#include "log.h"

bool httpTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "httpTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("httpTask creation failed.");
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
  appLogInfo("httpTask loop started. (skeleton)");
  for (;;) {
    // TODO: HTTP/HTTPSクライアント・サーバ処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
