/**
 * @file ota.h
 * @brief OTA更新機能のタスクひな形。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

class otaTask {
 public:
  bool startTask();

 private:
  static void taskEntry(void* taskParameter);
  void runLoop();

  static constexpr uint32_t taskStackSize = 6144;
  static constexpr UBaseType_t taskPriority = 1;
};
