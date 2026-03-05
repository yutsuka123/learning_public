/**
 * @file timeService.h
 * @brief タイムサーバー同期と内部時刻状態を管理するサービス定義。
 * @details
 * - [重要] 時刻はUTCで保持し、同期後はESP32の内部時刻で継続運用する。
 * - [厳守] 時刻同期失敗時でもパスワード等の機密値は生ログ出力しない。
 * - [制限] 現実装はNTP（UDP/123）を前提とし、TLS時刻配信は未対応。
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * @brief 時刻同期サービス。
 */
class timeService {
 public:
  /**
   * @brief タイムサーバー設定を保存して初回同期を実行する。
   * @param timeServerUrl タイムサーバーURLまたはホスト名。
   * @param timeServerUser タイムサーバーユーザー名（現実装では未使用）。
   * @param timeServerPass タイムサーバーパスワード（現実装では未使用）。
   * @param timeServerPort タイムサーバーポート（通常123）。
   * @param timeServerTls TLS利用フラグ（現実装では未対応）。
   * @return 同期成功時true、失敗時false。
   */
  bool initializeAndSync(const String& timeServerUrl,
                         const String& timeServerUser,
                         const String& timeServerPass,
                         int32_t timeServerPort,
                         bool timeServerTls);

  /**
   * @brief 保存済み設定で時刻同期を再実行する。
   * @return 同期成功時true、失敗時false。
   */
  bool syncNow();

  /**
   * @brief 最終同期済みかを返す。
   * @return 同期成功済みであればtrue。
   */
  bool hasSynchronizedOnce() const;

  /**
   * @brief 現在のUTC時刻をISO8601文字列で返す。
   * @return UTC時刻文字列（取得不可時は "(unsynchronized)"）。
   */
  String getCurrentUtcIso8601() const;

 private:
  /**
   * @brief NTP同期完了待機を行う。
   * @param functionName 呼び出し元関数名。
   * @return 同期完了時true、失敗時false。
   */
  bool waitForSntpSync(const char* functionName);

  /** @brief タイムサーバーホスト名。@type String */
  String timeServerUrl_;
  /** @brief タイムサーバーユーザー名。@type String */
  String timeServerUser_;
  /** @brief タイムサーバーパスワード。@type String */
  String timeServerPass_;
  /** @brief タイムサーバーポート番号。@type int32_t */
  int32_t timeServerPort_ = 123;
  /** @brief TLS利用フラグ。@type bool */
  bool timeServerTls_ = false;
  /** @brief 少なくとも1回同期成功したか。@type bool */
  bool hasSynchronizedOnce_ = false;
};
