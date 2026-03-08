/**
 * @file firmwareInfo.h
 * @brief 稼働中ファームウェアの版数と書換日時を管理する補助関数。
 * @details
 * - [重要] `firmwareWrittenAt` はビルド時刻ではなく、OTA成功時の実UTC時刻を優先して返す。
 * - [厳守] 永続化した値は現在稼働中の `firmwareVersion` と一致する場合のみ採用する。
 * - [禁止] OTA成功日時をRAMの一時値だけで扱わない。再起動後も参照できるよう永続化する。
 */

#pragma once

#include <Arduino.h>

namespace firmwareInfo {

/**
 * @brief OTA成功時刻を現在のファームウェア版数と紐付けて保存する。
 * @param firmwareVersion OTA対象ファームウェア版数。
 * @param firmwareWrittenAt OTA成功UTC時刻（ISO8601想定）。
 * @return 保存成功時true、失敗時false。
 */
bool saveOtaAppliedAt(const String& firmwareVersion, const String& firmwareWrittenAt);

/**
 * @brief status通知へ載せる `firmwareWrittenAt` を解決する。
 * @details
 * - [重要] 保存済み版数が現在版数と一致したときはOTA成功時刻を返す。
 * - [補足] 保存済み版数が不一致または未保存なら、ビルド時刻のフォールバック値を返す。
 * @param currentFirmwareVersion 現在稼働中のファームウェア版数。
 * @param fallbackFirmwareWrittenAt フォールバック用の時刻文字列（通常はビルド時刻）。
 * @return status通知へ載せる書換日時文字列。
 */
String resolveFirmwareWrittenAtForStatus(const String& currentFirmwareVersion,
                                         const String& fallbackFirmwareWrittenAt);

}  // namespace firmwareInfo
