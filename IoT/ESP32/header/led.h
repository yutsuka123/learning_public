/**
 * @file led.h
 * @brief LED表示制御とLEDタスクの定義。
 * @details
 * - [重要] 現時点ではメッセージ経由ではなく、各モジュールから直接メソッドを呼んでLED表示を制御する。
 * - [将来対応] タスク間メッセージ連携へ統一する際は、ledControllerの公開メソッドをメッセージハンドラ内部へ移設する。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

/**
 * @brief GPIO直叩きでLED表示を制御するサービス。
 */
class ledController {
 public:
  /**
   * @brief Mainから起動時に呼び出す初期表示（青LED）。
   * @details 再起動時を考慮して全LEDを消灯後、最低0.5秒待って青を点灯する。
   */
  static void initializeByMainOnBoot();

  /**
   * @brief Wi-Fi接続中表示（緑LED 0.5秒間隔点滅）。
   */
  static void indicateWifiConnecting();

  /**
   * @brief Wi-Fi接続完了表示（緑LED 2秒点灯）。
   */
  static void indicateWifiConnected();

  /**
   * @brief MQTT接続中表示（緑LED 0.2秒間隔点滅）。
   */
  static void indicateMqttConnecting();

  /**
   * @brief MQTT接続完了表示（緑LED 点灯維持）。
   */
  static void indicateMqttConnected();

  /**
   * @brief 通信アクティビティ表示（緑LED: 一旦消灯して0.3秒点灯）。
   * @details MQTT通信・HTTP通信が発生したタイミングで呼び出す。
   */
  static void indicateCommunicationActivity();

  /**
   * @brief 再起動時エラー表示（赤LED: 0.3秒点灯, 1秒消灯 を3回）。
   */
  static void indicateRebootPattern();

  /**
   * @brief アボート時表示（赤LED: 2回短点滅 + 1秒消灯 を3回）。
   */
  static void indicateAbortPattern();

  /**
   * @brief エラー時表示（赤LED: 4回短点滅 + 1秒消灯 を3回）。
   */
  static void indicateErrorPattern();
};

class ledTask {
 public:
  /**
   * @brief LEDタスクを開始する。
   * @return 開始成功時true、失敗時false。
   */
  bool startTask();

 private:
  static void taskEntry(void* taskParameter);
  void runLoop();

  /** @brief LEDタスクスタックサイズ。@type uint32_t */
  static constexpr uint32_t taskStackSize = 4096;
  /** @brief LEDタスク優先度。@type UBaseType_t */
  static constexpr UBaseType_t taskPriority = 1;
};
