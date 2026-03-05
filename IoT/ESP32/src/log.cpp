/**
 * @file log.cpp
 * @brief ログ設定処理の実装。
 * @details
 * - [重要] すべてのログに内部管理UTC時刻を付与して出力する。
 * - [厳守] 時刻未同期時は "(unsynchronized)" を表示する。
 */

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

namespace {

/**
 * @brief 現在のUTC時刻文字列を生成する。
 * @param utcTextOut 出力先バッファ。
 * @param utcTextOutSize 出力先バッファサイズ。
 * @return 成功時true、失敗時false。
 */
bool buildUtcTimestampText(char* utcTextOut, size_t utcTextOutSize) {
  if (utcTextOut == nullptr || utcTextOutSize == 0) {
    return false;
  }

  time_t currentEpochSeconds = time(nullptr);
  struct tm utcTimeInfo {};
  bool gmtimeResult = gmtime_r(&currentEpochSeconds, &utcTimeInfo) != nullptr;
  if (!gmtimeResult || currentEpochSeconds <= 0) {
    snprintf(utcTextOut, utcTextOutSize, "(unsynchronized)");
    return false;
  }

  size_t writtenLength = strftime(utcTextOut, utcTextOutSize, "%Y-%m-%dT%H:%M:%SZ", &utcTimeInfo);
  if (writtenLength == 0) {
    snprintf(utcTextOut, utcTextOutSize, "(unsynchronized)");
    return false;
  }

  return true;
}

}  // namespace

void appLogWrite(esp_log_level_t logLevel, const char* levelText, const char* format, ...) {
  if (levelText == nullptr || format == nullptr) {
    esp_log_write(ESP_LOG_ERROR, "iotApp", "[ERROR][UTC:(unsynchronized)] appLogWrite invalid parameter. levelText=%p format=%p",
                  levelText,
                  format);
    return;
  }

  char formattedMessage[512] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(formattedMessage, sizeof(formattedMessage), format, args);
  va_end(args);

  char utcText[32] = {};
  buildUtcTimestampText(utcText, sizeof(utcText));

  esp_log_write(logLevel, "iotApp", "[%s][UTC:%s] %s", levelText, utcText, formattedMessage);
}

void initializeLogLevel() {
  esp_log_level_set("*", ESP_LOG_DEBUG);
  appLogInfo("log level initialized to DEBUG.");
}
