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

  /**
   * @brief MQTTタスクスタックサイズ。@type uint32_t
   * @details
   * - [重要] 画像更新は分割I/O（小チャンク）で処理し、スタック肥大化を抑制する。
   * - [厳守] OTA系と同様の保守性を維持するため、スタックは 8192 + 4096 相当（12288）へ留める。
   * - [変更][2026-04-04] MQTT 上層のログ強化とセキュア化前診断余裕のため、最小限として 512 byte だけ追加する。
   */
  static constexpr uint32_t taskStackSize = 12800;
  /** @brief MQTTタスク優先度。@type UBaseType_t */
  static constexpr UBaseType_t taskPriority = 1;
};
