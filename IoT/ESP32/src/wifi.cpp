/**
 * @file wifi.cpp
 * @brief Wi-Fi機能のタスクひな形実装。
 */

#include "wifi.h"

#include "log.h"

bool wifiTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "wifiTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("wifiTask creation failed.");
    return false;
  }
  appLogInfo("wifiTask created.");
  return true;
}

void wifiTask::taskEntry(void* taskParameter) {
  wifiTask* self = static_cast<wifiTask*>(taskParameter);
  self->runLoop();
}

void wifiTask::runLoop() {
  appLogInfo("wifiTask loop started. (skeleton)");
  for (;;) {
    // TODO: Wi-Fi初期化、AP接続、再接続制御を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
