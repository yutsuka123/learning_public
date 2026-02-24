/**
 * @file wifi.h
 * @brief Wi-Fi機能のタスクひな形。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

class wifiTask {
 public:
  /**
   * @brief タスクを生成する。
   * @return 生成成功時true。
   */
  bool startTask();

 private:
  /**
   * @brief タスクエントリ関数。
   * @param taskParameter thisポインタ。
   * @return なし。
   */
  static void taskEntry(void* taskParameter);

  /**
   * @brief Wi-Fi初期化と接続処理の実行本体。
   * @return なし。
   */
  void runLoop();

  static constexpr uint32_t taskStackSize = 4096;
  static constexpr UBaseType_t taskPriority = 1;
};
