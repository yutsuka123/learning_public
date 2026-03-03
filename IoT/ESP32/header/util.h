/**
 * @file util.h
 * @brief 汎用ユーティリティ関数の宣言。
 * @details
 * - [重要] 文字列/数値変換とタスク間通信ヘルパーを提供する。
 * - [厳守] タスク間通信は本ヘルパー経由で統一し、個別実装の乱立を防ぐ。
 * - [禁止] 他タスクのフラグを直接変更する運用を常態化しない。
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "define.h"
#include "interTaskMessage.h"

namespace appUtil {

/**
 * @brief eFuse base_mac配列からpublic_id文字列を生成する。
 * @param baseMac 6バイトのMAC配列。
 * @return public_id（16文字の16進文字列）。
 */
String createPublicIdFromBaseMac(const uint8_t baseMac[6]);

/**
 * @brief 16進MAC文字列をバイト配列へ変換する。
 * @param baseMacHex 12桁またはコロン区切り16進文字列。
 * @param outBaseMac 変換先6バイト配列。
 * @return 変換成功時true。
 */
bool parseBaseMacHex(const String& baseMacHex, uint8_t outBaseMac[6]);

/**
 * @brief 秒をミリ秒へ変換する（オーバーフロー安全）。
 * @param seconds 秒。
 * @return ミリ秒。
 */
uint32_t secondsToMilliseconds(uint32_t seconds);

/**
 * @brief タスク間メッセージの詳細条件/詳細値。
 * @details
 * - sendMessageでは「設定済み項目のみ」メッセージへ反映する。
 * - waitMessageでは「設定済み項目のみ」一致判定を行う。
 */
struct appTaskMessageDetail {
  /** @brief intValueを使用する場合true。@type bool */
  bool hasIntValue;
  /** @brief intValueの値。@type int32_t */
  int32_t intValue;
  /** @brief intValue2を使用する場合true。@type bool */
  bool hasIntValue2;
  /** @brief intValue2の値。@type int32_t */
  int32_t intValue2;
  /** @brief boolValueを使用する場合true。@type bool */
  bool hasBoolValue;
  /** @brief boolValueの値。@type bool */
  bool boolValue;
  /** @brief text一致/設定条件（nullなら無視）。@type const char* */
  const char* text;
  /** @brief text2一致/設定条件（nullなら無視）。@type const char* */
  const char* text2;
  /** @brief text3一致/設定条件（nullなら無視）。@type const char* */
  const char* text3;
  /** @brief text4一致/設定条件（nullなら無視）。@type const char* */
  const char* text4;
};

/**
 * @brief appTaskMessageDetailの初期化済み既定値を返す。
 * @return すべて未指定の詳細構造体。
 */
appTaskMessageDetail createEmptyMessageDetail();

/**
 * @brief タスク間メッセージを汎用送信する。
 * @param destinationTaskId 送信先タスクID。
 * @param sourceTaskId 送信元タスクID。
 * @param requestType 要求内容（メッセージ種別）。
 * @param requestDetail 要求詳細（null可）。
 * @param timeoutMs 送信待機ms。
 * @return 送信成功時true、失敗時false。
 */
bool sendMessage(appTaskId destinationTaskId,
                 appTaskId sourceTaskId,
                 appMessageType requestType,
                 const appTaskMessageDetail* requestDetail,
                 int32_t timeoutMs);

/**
 * @brief タスク間メッセージを条件付きで待機する。
 * @param expectedSourceTaskId どこから待つか（送信元）。
 * @param selfTaskId 自タスクID（受信Queue特定用）。
 * @param waitType 待機する要求内容（メッセージ種別）。
 * @param waitDetail 待機詳細条件（null可）。
 * @param waitTimeMs 待機時間(ms)。0=ポーリング、-1=無期限、それ以外=指定ms。
 * @param receivedMessageOut 受信メッセージ出力先（null不可）。
 * @return 条件一致メッセージ受信時true、タイムアウト/エラー時false。
 */
bool waitMessage(appTaskId expectedSourceTaskId,
                 appTaskId selfTaskId,
                 appMessageType waitType,
                 const appTaskMessageDetail* waitDetail,
                 int32_t waitTimeMs,
                 appTaskMessage* receivedMessageOut);

/**
 * @brief 他タスクのフラグ状態を取得する。
 * @param taskId 確認対象タスクID。
 * @param flagName フラグ名。
 * @param flagValueOut 出力先（null不可）。
 * @return 取得成功時true、失敗時false。
 */
bool getFlagOtherTask(appTaskId taskId, appTaskFlagName flagName, bool* flagValueOut);

/**
 * @brief 他タスクのフラグ状態を設定する。
 * @param taskId 設定対象タスクID。
 * @param flagName フラグ名。
 * @param flagValue 設定値。
 * @return 設定成功時true、失敗時false。
 * @note [新規利用禁止] 原則はsendMessageで要求し、対象タスク自身が更新すること。
 */
bool setFlagOtherTask(appTaskId taskId, appTaskFlagName flagName, bool flagValue);

}  // namespace appUtil
