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

  /**
   * @brief TCP/IPタスクスタックサイズ。@type uint32_t
   * @details
   * - [変更][2026-04-04] TCP/IP 系の追加ログと原因切り分け余裕を確保するため、最小限として 512 byte 増やす。
   */
  static constexpr uint32_t taskStackSize = 3584;
  static constexpr UBaseType_t taskPriority = 1;
};
