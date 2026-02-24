/**
 * @file mqtt.cpp
 * @brief MQTT機能のタスクひな形実装。
 */

#include "mqtt.h"

#include "log.h"

bool mqttTask::startTask() {
  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      taskEntry, "mqttTask", taskStackSize, this, taskPriority, nullptr, tskNO_AFFINITY);
  if (createTaskResult != pdPASS) {
    appLogError("mqttTask creation failed.");
    return false;
  }
  appLogInfo("mqttTask created.");
  return true;
}

void mqttTask::taskEntry(void* taskParameter) {
  mqttTask* self = static_cast<mqttTask*>(taskParameter);
  self->runLoop();
}

void mqttTask::runLoop() {
  appLogInfo("mqttTask loop started. (skeleton)");
  for (;;) {
    // TODO: MQTT初期化、接続、subscribe/publish処理を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
