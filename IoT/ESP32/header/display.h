/**
 * @file display.h
 * @brief ディスプレー表示タスクのひな形。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

class displayTask {
 public:
  bool startTask();

 private:
  static void taskEntry(void* taskParameter);
  void runLoop();

  static constexpr uint32_t taskStackSize = 4096;
  static constexpr UBaseType_t taskPriority = 1;
};
