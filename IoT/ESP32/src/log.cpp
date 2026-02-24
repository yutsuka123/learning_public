/**
 * @file log.cpp
 * @brief ログ設定処理の実装。
 */

#include "log.h"

void initializeLogLevel() {
  esp_log_level_set("*", ESP_LOG_DEBUG);
  appLogInfo("log level initialized to DEBUG.");
}
