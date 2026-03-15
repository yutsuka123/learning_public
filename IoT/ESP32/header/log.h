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

/**
 * @brief ファイルログ出力の有効/無効を切り替える。
 * @param enabled true: 有効, false: 無効。
 * @return なし。
 * @details
 * - [重要] 既定は無効（OFF）で開始し、必要時のみ明示的に有効化する。
 * - [厳守] シリアル出力は本設定に関係なく継続する。
 */
void setFileLogEnabled(bool enabled);

/**
 * @brief ファイルログ出力が有効かを返す。
 * @return 有効時true。
 */
bool isFileLogEnabled();

#define appLogDebug(format, ...) appLogWrite(ESP_LOG_DEBUG, "DEBUG", format, ##__VA_ARGS__)
#define appLogInfo(format, ...) appLogWrite(ESP_LOG_INFO, "INFO ", format, ##__VA_ARGS__)
#define appLogWarn(format, ...) appLogWrite(ESP_LOG_WARN, "WARN ", format, ##__VA_ARGS__)
#define appLogError(format, ...) appLogWrite(ESP_LOG_ERROR, "ERROR", format, ##__VA_ARGS__)
#define appLogFatal(format, ...) appLogWrite(ESP_LOG_ERROR, "FATAL", format, ##__VA_ARGS__)
