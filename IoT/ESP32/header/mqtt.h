/**
 * @file mqtt.h
 * @brief MQTT機能のタスク定義。
 * @details
 * - [重要] mainTaskから受信した設定でブローカー接続とpublishを実行する。
 * - [制限] TLS接続は現時点で未実装。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

class mqttTask {
 public:
  /**
   * @brief MQTTタスクを開始する。
   * @return 開始成功時true、失敗時false。
   */
  bool startTask();

 private:
  static void taskEntry(void* taskParameter);
  void runLoop();

  /** @brief MQTTタスクスタックサイズ。@type uint32_t */
  static constexpr uint32_t taskStackSize = 4096;
  /** @brief MQTTタスク優先度。@type UBaseType_t */
  static constexpr UBaseType_t taskPriority = 1;
};
