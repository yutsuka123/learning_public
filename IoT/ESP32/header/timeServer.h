/**
 * @file timeServer.h
 * @brief タイムサーバー時刻同期タスク定義。
 * @details
 * - [重要] 起動時同期と24時間ごとの再同期を担当する。
 * - [厳守] 同期後の時刻管理はESP32内部時刻（UTC）を利用する。
 * - [制限] タスク間I/Fは init request / init done の2種メッセージのみ。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

/**
 * @brief タイムサーバー同期タスク。
 */
class timeServerTask {
 public:
  /**
   * @brief タスクを開始する。
   * @return 開始成功時true、失敗時false。
   */
  bool startTask();

 private:
  /**
   * @brief FreeRTOSタスクエントリ。
   * @param taskParameter thisポインタ。
   */
  static void taskEntry(void* taskParameter);

  /**
   * @brief タスク常駐ループ。
   */
  void runLoop();

  /** @brief タスクスタックサイズ。@type uint32_t */
  static constexpr uint32_t taskStackSize = 4096;
  /** @brief タスク優先度。@type UBaseType_t */
  static constexpr UBaseType_t taskPriority = 0;
};
