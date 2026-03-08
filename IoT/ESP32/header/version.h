/**
 * @file version.h
 * @brief ESP32ファームウェアの版数定義。
 * @details
 * - [重要] MQTT status通知、OTA完了確認、画面表示で同一の版数文字列を共有する。
 * - [厳守] 版数はこのファイルで一元管理し、文字列の直書きを各所へ分散させない。
 * - [禁止] 開発中の暫定版数を複数ファイルへ重複定義しない。
 */

#pragma once

namespace appVersion {

/**
 * @brief 現在稼働中ファームウェアの版数文字列。
 * @details
 * - [重要] OTA成功後はstatus通知でこの値がLocalServerへ通知され、ブラウザ表示更新に利用される。
 * - [重要] 当面の間は書換有無を識別しやすくするため、ESP32へ書き込むたびに `beta.x` の `x` を必ずインクリメントする。
 * - [厳守] `LocalServer/.env` の `OTA_FIRMWARE_VERSION` と同じ値へ合わせる。
 */
constexpr const char* kFirmwareVersion = "1.1.0-beta.8";

/**
 * @brief 現在稼働中ファームウェアの書換時刻文字列。
 * @details
 * - [重要] 初期フォールバック値として現在起動しているファームウェアイメージのビルド時刻を埋め込む。
 * - [重要] status通知では OTA成功時に永続化した実UTC時刻を優先し、この値は未保存時の代替として使う。
 * - [旧仕様] 以前はこの値をそのまま `firmwareWrittenAt` として通知していた。
 */
constexpr const char* kFirmwareWrittenAt = __DATE__ " " __TIME__;

}  // namespace appVersion
