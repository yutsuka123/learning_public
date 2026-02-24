/**
 * @file tcpip.h
 * @brief TCP/IP機能のタスクひな形。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

class tcpipTask {
 public:
  bool startTask();

 private:
  static void taskEntry(void* taskParameter);
  void runLoop();

  static constexpr uint32_t taskStackSize = 3072;
  static constexpr UBaseType_t taskPriority = 1;
};
