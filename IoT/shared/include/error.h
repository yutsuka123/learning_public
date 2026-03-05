/**
 * @file error.h
 * @brief Cloud/Local/ESP32で共通利用するエラーコード定義。
 * @details
 * - [重要] エラーコードは 000〜999 の3桁固定で管理し、ログは `ERRxxx` 形式で出力する。
 * - [厳守] 0 はエラーなし。エラーは 001〜999 を使用する。
 * - [厳守] 桁数可読性のため、コード表示は常にゼロ埋め3桁とする。
 * - [禁止] モジュール固有の独自フォーマット（例: E12, ERR-12）を新規導入しない。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace iotError {

using errorCodeType = uint16_t;

/** @brief エラーなし。 */
constexpr errorCodeType kNoError = 0;  // エラーなし（正常終了）。

/**
 * @brief 共通エラーコード。
 * @details
 * - [推奨] 既知エラーは本enumへ追加し、未定義は範囲代表コードを暫定利用する。
 */
enum class errorCodeEnum : errorCodeType {
  // 001-099: 起動/初期化（ESP32専用）
  kEspStartupI2cTaskStartFailed = 1,      // ESP32起動時にI2Cタスク開始が失敗したことを示す。
  kEspStartupWifiInitRequestFailed = 2,   // ESP32起動時にWi-Fi初期化要求の送信が失敗したことを示す。
  kEspStartupWifiInitTimeout = 3,         // ESP32起動時にWi-Fi初期化完了待機がタイムアウトしたことを示す。
  kEspStartupMqttInitRequestFailed = 4,   // ESP32起動時にMQTT初期化要求の送信が失敗したことを示す。
  kEspStartupMqttInitTimeout = 5,         // ESP32起動時にMQTT初期化完了待機がタイムアウトしたことを示す。
  kEspStartupTimeInitRequestFailed = 6,   // ESP32起動時に時刻同期初期化要求の送信が失敗したことを示す。
  kEspStartupTimeInitTimeout = 7,         // ESP32起動時に時刻同期初期化完了待機がタイムアウトしたことを示す。

  // 100-199: ネットワーク系（MQTT/HTTP/HTTPS/OTA）
  kNetworkWifiTaskError = 101,            // ネットワーク層のWi-Fiタスクでエラーが発生したことを示す。
  kNetworkMqttTaskError = 110,            // ネットワーク層のMQTTタスクでエラーが発生したことを示す。
  kNetworkHttpTaskError = 120,            // ネットワーク層のHTTPタスクでエラーが発生したことを示す。
  kNetworkOtaTaskError = 130,             // ネットワーク層のOTA処理でエラーが発生したことを示す。
  kNetworkTimeServerTaskError = 131,      // ネットワーク層のタイムサーバー同期タスクでエラーが発生したことを示す。

  // 200-299: 認証/セキュリティ
  kSecurityGeneral = 200,                 // 認証・セキュリティカテゴリの一般エラーを示す。

  // 300-399: 内部通信
  kInternalTaskMessageError = 300,        // タスク間メッセージなど内部通信でエラーが発生したことを示す。

  // 400-499: その他
  kOtherGeneral = 400,                    // 既存カテゴリに該当しないその他一般エラーを示す。

  // 500-599: ローカル特有
  kLocalGeneral = 500,                    // ローカル特有機能で発生した一般エラーを示す。

  // 600-699: サーバー特有
  kServerGeneral = 600,                   // サーバー特有機能で発生した一般エラーを示す。

  // 700-799: クラウド
  kCloudGeneral = 700,                    // クラウド側機能で発生した一般エラーを示す。

  // 800-899: 外部機器/ハード
  kHardwareGeneral = 800,                 // 外部機器・ハードウェア起因の一般エラーを示す。

  // 900-999: 致命的
  kFatalGeneral = 900,                    // 致命的エラーの一般コードを示す。
  kFatalMainTaskCreateFailed = 901,       // mainTask生成に失敗し継続不能であることを示す。
  kFatalUnexpected = 999,                 // 想定外の致命的エラー（フォールバック）を示す。
};

/**
 * @brief エラーコードを `ERRxxx` 形式へ変換する。
 * @param errorCode 0〜999 を想定するエラーコード。
 * @param textOut 出力先バッファ。
 * @param textOutSize 出力先サイズ（最低7推奨）。
 * @return 変換成功時true、失敗時false。
 */
inline bool formatErrCodeText(errorCodeType errorCode, char* textOut, size_t textOutSize) {
  if (textOut == nullptr || textOutSize == 0) {
    return false;
  }
  const errorCodeType normalizedCode = static_cast<errorCodeType>(errorCode % 1000);
  const int writeResult = snprintf(textOut, textOutSize, "ERR%03u", static_cast<unsigned>(normalizedCode));
  return (writeResult > 0 && static_cast<size_t>(writeResult) < textOutSize);
}

}  // namespace iotError
