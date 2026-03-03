/**
 * @file interTaskMessage.h
 * @brief FreeRTOS Queueを用いたタスク間メッセージ伝達サービス。
 * @details
 * - [重要] 各タスクは専用Queueを持ち、宛先タスクIDで配送する。
 * - [推奨] まずは起動通知/ACKの制御に利用し、将来コマンド配送へ拡張する。
 * - [制限] 同一タスクの重複registerは許容しない（falseを返す）。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>
#include "define.h"

/**
 * @brief タスク間で送受信するメッセージ構造体。
 */
struct appTaskMessage {
  /** @brief 送信元タスクID。@type appTaskId */
  appTaskId sourceTaskId;
  /** @brief 宛先タスクID。@type appTaskId */
  appTaskId destinationTaskId;
  /** @brief メッセージ種別。@type appMessageType */
  appMessageType messageType;
  /** @brief 汎用整数パラメータ1。@type int32_t */
  int32_t intValue;
  /** @brief 汎用整数パラメータ2。@type int32_t */
  int32_t intValue2;
  /** @brief 汎用真偽値パラメータ。@type bool */
  bool boolValue;
  /** @brief 汎用テキストパラメータ1。@type char[48] */
  char text[48];
  /** @brief 汎用テキストパラメータ2。@type char[64] */
  char text2[64];
  /** @brief 汎用テキストパラメータ3。@type char[64] */
  char text3[64];
  /** @brief 汎用テキストパラメータ4。@type char[64] */
  char text4[64];
};

/**
 * @brief タスク間メッセージサービス。
 */
class interTaskMessageService {
 public:
  /**
   * @brief サービス初期化。
   * @return 成功時true、失敗時false。
   */
  bool initialize();

  /**
   * @brief 指定タスクID用Queueを登録する。
   * @param taskId 登録するタスクID。
   * @param queueLength Queue長。
   * @return 成功時true、失敗時false。
   */
  bool registerTaskQueue(appTaskId taskId, UBaseType_t queueLength);

  /**
   * @brief 宛先タスクのQueueへメッセージ送信する。
   * @param message 送信するメッセージ。
   * @param timeoutTicks 送信待機Tick。
   * @return 成功時true、失敗時false。
   */
  bool sendMessage(const appTaskMessage& message, TickType_t timeoutTicks);

  /**
   * @brief 指定タスクQueueからメッセージ受信する。
   * @param taskId 受信対象タスクID。
   * @param messageOut 受信データ出力先（null不可）。
   * @param timeoutTicks 受信待機Tick。
   * @return 受信成功時true、失敗/タイムアウト時false。
   */
  bool receiveMessage(appTaskId taskId, appTaskMessage* messageOut, TickType_t timeoutTicks);
};

/**
 * @brief グローバルのメッセージサービス参照を返す。
 * @return interTaskMessageService参照。
 */
interTaskMessageService& getInterTaskMessageService();
