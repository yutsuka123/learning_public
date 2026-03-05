/**
 * @file log.h
 * @brief ログ処理の共通インターフェース。
 * @details ESP_LOGxを一箇所に集約し、各モジュールは本ヘッダ経由でログ出力する。
 */

#pragma once

#include <esp_log.h>

/**
 * @brief 全体ログレベルを初期化する。
 * @return なし。
 */
void initializeLogLevel();

/**
 * @brief UTC時刻付きでログを出力する共通関数。
 * @param logLevel ESP-IDFのログレベル。
 * @param levelText 表示用ログレベル文字列。
 * @param format printf形式フォーマット文字列。
 * @param ... 可変引数。
 * @return なし。
 * @details
 * - [重要] すべてのログは本関数を通し、内部UTC時刻を先頭へ付与する。
 * - [厳守] 未同期時は "(unsynchronized)" を表示し、誤時刻を表示しない。
 */
void appLogWrite(esp_log_level_t logLevel, const char* levelText, const char* format, ...);

#define appLogDebug(format, ...) appLogWrite(ESP_LOG_DEBUG, "DEBUG", format, ##__VA_ARGS__)
#define appLogInfo(format, ...) appLogWrite(ESP_LOG_INFO, "INFO ", format, ##__VA_ARGS__)
#define appLogWarn(format, ...) appLogWrite(ESP_LOG_WARN, "WARN ", format, ##__VA_ARGS__)
#define appLogError(format, ...) appLogWrite(ESP_LOG_ERROR, "ERROR", format, ##__VA_ARGS__)
#define appLogFatal(format, ...) appLogWrite(ESP_LOG_ERROR, "FATAL", format, ##__VA_ARGS__)
