/**
 * @file wifi.h
 * @brief Wi-Fi機能のタスク定義。
 * @details
 * - [重要] mainTaskから受信した資格情報を用いてSTA接続を実施する。
 * - [将来対応] 再接続ポリシーとイベント駆動処理の拡張を行う。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

class wifiTask {
 public:
  /**
   * @brief Wi-Fiタスクを開始する。
   * @return 開始成功時true、失敗時false。
   */
  bool startTask();

 private:
  static void taskEntry(void* taskParameter);
  void runLoop();

  /** @brief Wi-Fiタスクスタックサイズ。@type uint32_t */
  static constexpr uint32_t taskStackSize = 4096;
  /** @brief Wi-Fiタスク優先度。@type UBaseType_t */
  static constexpr UBaseType_t taskPriority = 1;
};
