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

#define appLogDebug(format, ...) ESP_LOGD("iotApp", "[DEBUG] " format, ##__VA_ARGS__)
#define appLogInfo(format, ...) ESP_LOGI("iotApp", "[INFO ] " format, ##__VA_ARGS__)
#define appLogWarn(format, ...) ESP_LOGW("iotApp", "[WARN ] " format, ##__VA_ARGS__)
#define appLogError(format, ...) ESP_LOGE("iotApp", "[ERROR] " format, ##__VA_ARGS__)
#define appLogFatal(format, ...) ESP_LOGE("iotApp", "[FATAL] " format, ##__VA_ARGS__)
