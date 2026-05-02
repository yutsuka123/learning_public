/**
 * @file log.cpp
 * @brief ログ設定処理の実装。
 * @details
 * - [重要] すべてのログに内部管理UTC時刻を付与して出力する。
 * - [厳守] 時刻未同期時は "(unsynchronized)" を表示する。
 */

#include "log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <vector>

#include "../header/firmwareMode.h"

#ifndef IOT_ENABLE_FILE_LOG
#define IOT_ENABLE_FILE_LOG 1
#endif

namespace {
/** @brief ファイルログ有効フラグ（既定OFF）。 */
bool fileLogEnabled = false;
/** @brief ファイルログ用LittleFS初期化完了フラグ。 */
bool fileLogLittleFsReady = false;
/** @brief ログ時刻を同期済みとみなす最小UNIX時刻（2021-01-01 UTC）。 */
constexpr time_t minimumValidEpochSeconds = 1609459200;
/** @brief ファイルログの起動回数保存名前空間。 */
constexpr const char* fileLogPreferencesNamespace = "filelog";
/** @brief ファイルログの起動回数保存キー。 */
constexpr const char* fileLogBootCountKey = "bootCount";
/** @brief 実行中ブート回数。 */
uint32_t runtimeBootCount = 1;
/** @brief 同期済み時ログファイル連番。 */
uint32_t syncedLogFileSequence = 1;
/** @brief 未同期時ログファイル連番。 */
uint32_t unsyncedLogFileSequence = 1;
/** @brief 現在のアクティブファイルログパス。 */
char activeFileLogPath[160] = {0};
/** @brief アクティブパスの初期化有無。 */
bool activeFileLogPathInitialized = false;
/** @brief アクティブパスが同期済みルールか。 */
bool activeFileLogPathUsesSyncedTime = false;
/** @brief 現在のアクティブログファイルが実体作成済みか。 */
bool activeFileLogPathCreated = false;
/** @brief ローテーション閾値（byte）。 */
constexpr size_t fileLogRotateThresholdBytes = 1024;
/** @brief 保存総量上限（byte）。 */
constexpr size_t fileLogTotalSizeLimitBytes = 100 * 1024;
/** @brief 保存日数上限（日）。 */
constexpr int32_t fileLogRetentionDays = 30;
/** @brief 7048/7049 切り分け用: ファイルログを最小1行のみに制限する。 */
constexpr bool fileLogDebugMinimalOneShotMode = false;
/** @brief 最小1行を書いたかどうか。 */
bool fileLogDebugOneShotWritten = false;
/** @brief 7048/7049 切り分け用: ファイルログ失敗段階を低レベル出力する。 */
constexpr bool fileLogDebugFailureTraceMode = true;
/** @brief 失敗トレース出力の上限件数。 */
constexpr uint32_t fileLogDebugFailureTraceLimit = 300;
/** @brief 現在までに出力した失敗トレース件数。 */
uint32_t fileLogDebugFailureTraceCount = 0;
/** @brief ファイルログへ常時記録する最小レベル。 */
constexpr esp_log_level_t fileLogMinimumLevel = ESP_LOG_WARN;
/** @brief INFOログをファイルへ記録する間引き周期。 */
constexpr uint32_t fileLogInfoSamplingInterval = 0;
/** @brief ファイルログへ載せる本文最大文字数。 */
constexpr size_t fileLogMessageBodyMaxChars = 96;
/** @brief INFOログの累積件数。 */
uint32_t fileLogInfoSeenCount = 0;
/** @brief 間引きでドロップした件数。 */
uint32_t fileLogDroppedCount = 0;
/** @brief ファイルログ連続失敗時に自動停止する閾値。 */
constexpr uint32_t fileLogFailureAutoDisableThreshold = 3;
/** @brief ファイルログ連続失敗回数。 */
uint32_t fileLogConsecutiveFailureCount = 0;
/** @brief ファイルログ書込みキュー長。 */
constexpr uint32_t fileLogQueueLength = 64;
/**
 * @brief ファイルログ書込みタスクのスタックサイズ。
 * @details
 * - [変更][2026-04-04] セキュア化最終版の診断ログ増加を見込みつつ、過剰確保を避けるため 512 byte のみ加算する。
 */
constexpr uint32_t fileLogWriterTaskStackSize = 6656;
/** @brief ファイルログ書込みタスク優先度。 */
constexpr UBaseType_t fileLogWriterTaskPriority = 0;
/** @brief ファイルログ1行の最大文字数（終端含む）。 */
constexpr size_t fileLogQueueLineMaxChars = 192;
/** @brief バッチ書込みの最大件数。 */
constexpr uint32_t fileLogBatchMaxItems = 8;
/** @brief バッチ書込みの最大待機時間(ms)。 */
constexpr uint32_t fileLogBatchMaxWaitMs = 200;
/** @brief WARNログの1時間あたり最大書込み回数。 */
constexpr uint32_t fileLogWarnWriteLimitPerHour = 120;
/** @brief ファイルログ書込みキュー。 */
QueueHandle_t fileLogQueue = nullptr;
/** @brief ファイルログ書込みタスクハンドル。 */
TaskHandle_t fileLogWriterTaskHandle = nullptr;
/** @brief ファイルログ書込みタスクスタック領域。 */
StackType_t* fileLogWriterTaskStackBuffer = nullptr;
/** @brief ファイルログ書込みタスク制御ブロック。 */
StaticTask_t fileLogWriterTaskControlBlock;

/**
 * @brief ファイルログ書込みキュー要素。
 */
struct fileLogQueueItem {
  esp_log_level_t logLevel = ESP_LOG_INFO;
  char lineText[fileLogQueueLineMaxChars];
};

/** @brief WARN書込みレート制限の開始時刻(ms)。 */
uint32_t fileLogWarnWindowStartMs = 0;
/** @brief 現在ウィンドウのWARN書込み件数。 */
uint32_t fileLogWarnWrittenInWindow = 0;
/** @brief 現在ウィンドウで抑止したWARN件数。 */
uint32_t fileLogWarnDroppedInWindow = 0;

struct logFileEntry {
  String path;
  size_t size = 0;
  String sortKey;
  String timestamp14;
  bool timestampAvailable = false;
};

bool appendLineToFileLog(const char* lineText);
bool appendBatchToFileLog(const fileLogQueueItem* queueItems, size_t itemCount);
bool shouldAllowFileWriteByRateLimit(esp_log_level_t logLevel);
void writeFileLogFailureTrace(const char* format, ...);

/**
 * @brief フォーマット文字列から短いイベントコードを算出する。
 * @param formatText ログフォーマット文字列。
 * @return 1000-9999 のイベントコード。
 */
uint16_t calculateEventCodeFromFormat(const char* formatText) {
  if (formatText == nullptr) {
    return 9999;
  }
  uint32_t hashValue = 2166136261u;
  for (size_t index = 0; formatText[index] != '\0'; ++index) {
    hashValue ^= static_cast<uint8_t>(formatText[index]);
    hashValue *= 16777619u;
  }
  return static_cast<uint16_t>((hashValue % 9000u) + 1000u);
}

/**
 * @brief ログレベルに応じてファイルログへ記録するか判定する。
 * @param logLevel ログレベル。
 * @return 記録対象ならtrue。
 */
bool shouldWriteToFileLog(esp_log_level_t logLevel) {
  if (logLevel >= fileLogMinimumLevel) {
    return true;
  }
  if (logLevel == ESP_LOG_INFO) {
    fileLogInfoSeenCount += 1;
    bool shouldWrite = (fileLogInfoSamplingInterval > 0) &&
                       ((fileLogInfoSeenCount % fileLogInfoSamplingInterval) == 0);
    if (!shouldWrite) {
      fileLogDroppedCount += 1;
    }
    return shouldWrite;
  }
  fileLogDroppedCount += 1;
  return false;
}

/**
 * @brief ファイルログ書込みタスク本体。
 * @param taskParameter 未使用。
 * @return なし。
 */
void fileLogWriterTaskEntry(void* taskParameter) {
  (void)taskParameter;
  for (;;) {
    fileLogQueueItem batchItems[fileLogBatchMaxItems] = {};
    size_t batchCount = 0;
    uint32_t batchStartMs = millis();

    fileLogQueueItem firstItem{};
    BaseType_t firstReceiveResult = xQueueReceive(fileLogQueue, &firstItem, pdMS_TO_TICKS(100));
    if (firstReceiveResult == pdTRUE) {
      if (shouldAllowFileWriteByRateLimit(firstItem.logLevel)) {
        batchItems[batchCount] = firstItem;
        batchCount += 1;
      }
      batchStartMs = millis();

      while (batchCount < fileLogBatchMaxItems) {
        uint32_t nowMs = millis();
        uint32_t elapsedMs = nowMs - batchStartMs;
        if (elapsedMs >= fileLogBatchMaxWaitMs) {
          break;
        }
        uint32_t waitMs = fileLogBatchMaxWaitMs - elapsedMs;
        if (waitMs > 20) {
          waitMs = 20;
        }
        fileLogQueueItem nextItem{};
        BaseType_t nextReceiveResult = xQueueReceive(fileLogQueue, &nextItem, pdMS_TO_TICKS(waitMs));
        if (nextReceiveResult != pdTRUE) {
          continue;
        }
        if (shouldAllowFileWriteByRateLimit(nextItem.logLevel)) {
          batchItems[batchCount] = nextItem;
          batchCount += 1;
        }
      }
    }

    if (batchCount > 0) {
      bool batchResult = appendBatchToFileLog(batchItems, batchCount);
      if (!batchResult) {
        writeFileLogFailureTrace("fileLogWriterTaskEntry batch write failed. itemCount=%u",
                                 static_cast<unsigned>(batchCount));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/**
 * @brief ファイルログ書込み基盤（キュー/タスク）を起動する。
 * @return 起動済みまたは起動成功時true。
 */
bool ensureFileLogWriterReady() {
  if (fileLogQueue == nullptr) {
    fileLogQueue = xQueueCreate(fileLogQueueLength, sizeof(fileLogQueueItem));
    if (fileLogQueue == nullptr) {
      return false;
    }
  }
  if (fileLogWriterTaskHandle != nullptr) {
    return true;
  }

  if (fileLogWriterTaskStackBuffer == nullptr) {
    fileLogWriterTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(fileLogWriterTaskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (fileLogWriterTaskStackBuffer == nullptr) {
    fileLogWriterTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(fileLogWriterTaskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (fileLogWriterTaskStackBuffer == nullptr) {
    return false;
  }

  fileLogWriterTaskHandle = xTaskCreateStaticPinnedToCore(
      fileLogWriterTaskEntry,
      "fileLogWriterTask",
      fileLogWriterTaskStackSize,
      nullptr,
      fileLogWriterTaskPriority,
      fileLogWriterTaskStackBuffer,
      &fileLogWriterTaskControlBlock,
      tskNO_AFFINITY);
  if (fileLogWriterTaskHandle != nullptr) {
    appLogInfo("ensureFileLogWriterReady: fileLogWriterTask created. stackBytes=%u",
               static_cast<unsigned>(fileLogWriterTaskStackSize));
  }
  return (fileLogWriterTaskHandle != nullptr);
}

/**
 * @brief ファイルログ1行をキューへ投入する。
 * @param lineText 追記対象1行。
 * @return キュー投入成功時true。
 */
bool enqueueFileLogLine(esp_log_level_t logLevel, const char* lineText) {
  if (lineText == nullptr) {
    return false;
  }
  if (!ensureFileLogWriterReady()) {
    return false;
  }
  fileLogQueueItem queueItem{};
  queueItem.logLevel = logLevel;
  strncpy(queueItem.lineText, lineText, sizeof(queueItem.lineText) - 1);
  queueItem.lineText[sizeof(queueItem.lineText) - 1] = '\0';
  BaseType_t sendResult = xQueueSend(fileLogQueue, &queueItem, 0);
  if (sendResult != pdTRUE) {
    fileLogDroppedCount += 1;
    return false;
  }
  return true;
}

/**
 * @brief WARNログ書込みのレート制限を判定する。
 * @param logLevel ログレベル。
 * @return 書込み許可時true。
 */
bool shouldAllowFileWriteByRateLimit(esp_log_level_t logLevel) {
  if (logLevel != ESP_LOG_WARN) {
    return true;
  }
  uint32_t nowMs = millis();
  if (fileLogWarnWindowStartMs == 0 || (nowMs - fileLogWarnWindowStartMs) >= (60UL * 60UL * 1000UL)) {
    if (fileLogWarnDroppedInWindow > 0) {
      esp_log_write(ESP_LOG_WARN,
                    "iotApp",
                    "[WARN][UTC:(unsynchronized)] file log warn rate-limit summary. dropped=%u written=%u",
                    static_cast<unsigned>(fileLogWarnDroppedInWindow),
                    static_cast<unsigned>(fileLogWarnWrittenInWindow));
    }
    fileLogWarnWindowStartMs = nowMs;
    fileLogWarnWrittenInWindow = 0;
    fileLogWarnDroppedInWindow = 0;
  }
  if (fileLogWarnWrittenInWindow >= fileLogWarnWriteLimitPerHour) {
    fileLogWarnDroppedInWindow += 1;
    return false;
  }
  fileLogWarnWrittenInWindow += 1;
  return true;
}

/**
 * @brief ファイルログ失敗切り分け用の低レベルトレースを出力する。
 * @param format 書式文字列。
 * @return なし。
 * @details
 * - [重要] 再帰ログを避けるため `appLog*` を使わず `esp_log_write` を直接呼ぶ。
 * - [厳守] 出力件数を上限管理し、異常時のログスパムで二次障害を起こさない。
 */
void writeFileLogFailureTrace(const char* format, ...) {
  if (!fileLogDebugFailureTraceMode || format == nullptr) {
    return;
  }
  if (fileLogDebugFailureTraceCount >= fileLogDebugFailureTraceLimit) {
    return;
  }
  char messageBuffer[256] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(messageBuffer, sizeof(messageBuffer), format, args);
  va_end(args);
  esp_log_write(ESP_LOG_WARN, "iotApp", "[WARN][UTC:(unsynchronized)] fileLogTrace: %s", messageBuffer);
  fileLogDebugFailureTraceCount += 1;
}

/**
 * @brief 指定パスがディレクトリとして存在するか判定する。
 * @param path 判定対象パス。
 * @return ディレクトリ存在時true。
 */
bool isExistingDirectoryPath(const char* path) {
  if (path == nullptr || strlen(path) == 0) {
    return false;
  }
  File target = LittleFS.open(path, "r");
  if (!target) {
    return false;
  }
  bool isDirectory = target.isDirectory();
  target.close();
  return isDirectory;
}

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

/**
 * @brief ファイルログ用 LittleFS 初期化を遅延実行する。
 * @return 初期化済みならtrue。
 */
bool ensureFileLogLittleFsReady() {
  if (fileLogLittleFsReady) {
    return true;
  }
  fileLogLittleFsReady = LittleFS.begin(false);
  if (!fileLogLittleFsReady) {
    writeFileLogFailureTrace("ensureFileLogLittleFsReady LittleFS.begin(false) failed.");
  }
  return fileLogLittleFsReady;
}

/**
 * @brief ディレクトリを再帰的に作成する。
 * @param directoryPath 作成対象ディレクトリ（`/` 始まり）。
 * @return 作成成功または既存時true。
 */
bool ensureDirectoryPathExistsRecursive(const char* directoryPath) {
  if (directoryPath == nullptr || directoryPath[0] != '/') {
    writeFileLogFailureTrace("ensureDirectoryPathExistsRecursive invalid path. directoryPath=%s",
                             (directoryPath == nullptr ? "(null)" : directoryPath));
    return false;
  }
  if (LittleFS.exists(directoryPath)) {
    if (isExistingDirectoryPath(directoryPath)) {
      return true;
    }
    // [重要] ディレクトリ想定パスにファイルが残っている場合はログ運用を優先して置換する。
    if (!LittleFS.remove(directoryPath)) {
      writeFileLogFailureTrace("ensureDirectoryPathExistsRecursive remove existing file failed. path=%s", directoryPath);
      return false;
    }
  }
  char pathBuffer[160] = {};
  strncpy(pathBuffer, directoryPath, sizeof(pathBuffer) - 1);
  size_t pathLength = strnlen(pathBuffer, sizeof(pathBuffer));
  for (size_t index = 1; index < pathLength; ++index) {
    if (pathBuffer[index] != '/') {
      continue;
    }
    pathBuffer[index] = '\0';
    if (strlen(pathBuffer) > 0) {
      if (LittleFS.exists(pathBuffer)) {
        if (!isExistingDirectoryPath(pathBuffer)) {
          if (!LittleFS.remove(pathBuffer) || !LittleFS.mkdir(pathBuffer)) {
            writeFileLogFailureTrace("ensureDirectoryPathExistsRecursive replace file->dir failed. partialPath=%s",
                                     pathBuffer);
            pathBuffer[index] = '/';
            return false;
          }
        }
      } else {
        if (!LittleFS.mkdir(pathBuffer)) {
          writeFileLogFailureTrace("ensureDirectoryPathExistsRecursive mkdir failed. partialPath=%s", pathBuffer);
          pathBuffer[index] = '/';
          return false;
        }
      }
    }
    pathBuffer[index] = '/';
  }
  if (LittleFS.exists(pathBuffer)) {
    if (!isExistingDirectoryPath(pathBuffer)) {
      if (!LittleFS.remove(pathBuffer) || !LittleFS.mkdir(pathBuffer)) {
        writeFileLogFailureTrace("ensureDirectoryPathExistsRecursive replace final file->dir failed. path=%s", pathBuffer);
        return false;
      }
    }
  } else {
    if (!LittleFS.mkdir(pathBuffer)) {
      writeFileLogFailureTrace("ensureDirectoryPathExistsRecursive final mkdir failed. path=%s", pathBuffer);
      return false;
    }
  }
  return true;
}

/**
 * @brief ファイルパスから親ディレクトリを取り出して作成する。
 * @param filePath ファイルパス。
 * @return 親ディレクトリ準備成功時true。
 */
bool ensureParentDirectoryExistsForFilePath(const char* filePath) {
  if (filePath == nullptr) {
    writeFileLogFailureTrace("ensureParentDirectoryExistsForFilePath filePath is null.");
    return false;
  }
  char directoryBuffer[160] = {};
  strncpy(directoryBuffer, filePath, sizeof(directoryBuffer) - 1);
  char* lastSlash = strrchr(directoryBuffer, '/');
  if (lastSlash == nullptr) {
    writeFileLogFailureTrace("ensureParentDirectoryExistsForFilePath slash not found. filePath=%s", filePath);
    return false;
  }
  if (lastSlash == directoryBuffer) {
    // ルート直下ファイル
    return true;
  }
  *lastSlash = '\0';
  bool ensureResult = ensureDirectoryPathExistsRecursive(directoryBuffer);
  if (!ensureResult) {
    writeFileLogFailureTrace("ensureParentDirectoryExistsForFilePath ensure dir failed. filePath=%s parent=%s",
                             filePath,
                             directoryBuffer);
  }
  return ensureResult;
}

/**
 * @brief ファイルパスから親ディレクトリパスを抽出する。
 * @param filePath 対象ファイルパス。
 * @param parentPathOut 抽出先バッファ。
 * @param parentPathOutSize 抽出先サイズ。
 * @return 抽出成功時true。
 */
bool extractParentDirectoryPath(const char* filePath, char* parentPathOut, size_t parentPathOutSize) {
  if (filePath == nullptr || parentPathOut == nullptr || parentPathOutSize == 0) {
    return false;
  }
  strncpy(parentPathOut, filePath, parentPathOutSize - 1);
  parentPathOut[parentPathOutSize - 1] = '\0';
  char* lastSlash = strrchr(parentPathOut, '/');
  if (lastSlash == nullptr || lastSlash == parentPathOut) {
    return false;
  }
  *lastSlash = '\0';
  return true;
}

/**
 * @brief 現在時刻がUTC同期済みか判定する。
 * @return 同期済み時true。
 */
bool isCurrentUtcSynchronized() {
  time_t currentEpochSeconds = time(nullptr);
  return currentEpochSeconds >= minimumValidEpochSeconds;
}

/**
 * @brief 未同期時ログファイルパスを生成する。
 * @param filePathOut 出力先。
 * @param filePathOutSize 出力先サイズ。
 * @return 生成成功時true。
 */
bool buildUnsynchronizedLogFilePath(uint32_t fileSequence, char* filePathOut, size_t filePathOutSize) {
  if (filePathOut == nullptr || filePathOutSize == 0) {
    return false;
  }
  int printedLength = snprintf(filePathOut,
                               filePathOutSize,
                               "/logs/%lu/%05lu.log",
                               static_cast<unsigned long>(runtimeBootCount),
                               static_cast<unsigned long>(fileSequence));
  return printedLength > 0 && printedLength < static_cast<int>(filePathOutSize);
}

/**
 * @brief 同期済み時ログファイルパスを生成する。
 * @param filePathOut 出力先。
 * @param filePathOutSize 出力先サイズ。
 * @return 生成成功時true。
 * @details
 * - [重要] 形式: `/logs/<年>/<月日>/<年月日時分秒ms-連番>.log`
 */
bool buildSynchronizedLogFilePath(char* filePathOut, size_t filePathOutSize) {
  if (filePathOut == nullptr || filePathOutSize == 0) {
    return false;
  }
  struct timeval currentTimeValue {};
  if (gettimeofday(&currentTimeValue, nullptr) != 0) {
    return false;
  }
  time_t epochSeconds = static_cast<time_t>(currentTimeValue.tv_sec);
  struct tm utcTimeInfo {};
  if (gmtime_r(&epochSeconds, &utcTimeInfo) == nullptr) {
    return false;
  }
  long millisecondPart = static_cast<long>(currentTimeValue.tv_usec / 1000L);
  int printedLength = snprintf(filePathOut,
                               filePathOutSize,
                               "/logs/%04d/%02d%02d/%04d%02d%02d%02d%02d%02d%03ld-%05lu.log",
                               utcTimeInfo.tm_year + 1900,
                               utcTimeInfo.tm_mon + 1,
                               utcTimeInfo.tm_mday,
                               utcTimeInfo.tm_year + 1900,
                               utcTimeInfo.tm_mon + 1,
                               utcTimeInfo.tm_mday,
                               utcTimeInfo.tm_hour,
                               utcTimeInfo.tm_min,
                               utcTimeInfo.tm_sec,
                               millisecondPart,
                               static_cast<unsigned long>(syncedLogFileSequence));
  return printedLength > 0 && printedLength < static_cast<int>(filePathOutSize);
}

/**
 * @brief 現在状態に応じたアクティブログファイルを解決する。
 * @return 解決成功時true。
 */
bool resolveActiveFileLogPath() {
  bool nowSynchronized = isCurrentUtcSynchronized();
  if (activeFileLogPathInitialized && activeFileLogPathUsesSyncedTime == nowSynchronized && strlen(activeFileLogPath) > 0) {
    return true;
  }
  char resolvedPath[160] = {};
  bool resolveResult = false;
  if (nowSynchronized) {
    if (!activeFileLogPathUsesSyncedTime) {
      syncedLogFileSequence = 1;
    }
    resolveResult = buildSynchronizedLogFilePath(resolvedPath, sizeof(resolvedPath));
    if (resolveResult) {
      syncedLogFileSequence += 1;
    }
  } else {
    if (activeFileLogPathUsesSyncedTime) {
      unsyncedLogFileSequence = 1;
    }
    resolveResult = buildUnsynchronizedLogFilePath(unsyncedLogFileSequence, resolvedPath, sizeof(resolvedPath));
  }
  if (!resolveResult) {
    writeFileLogFailureTrace("resolveActiveFileLogPath build path failed. synced=%d unsyncedSeq=%lu syncedSeq=%lu",
                             nowSynchronized ? 1 : 0,
                             static_cast<unsigned long>(unsyncedLogFileSequence),
                             static_cast<unsigned long>(syncedLogFileSequence));
    return false;
  }
  if (!ensureParentDirectoryExistsForFilePath(resolvedPath)) {
    writeFileLogFailureTrace("resolveActiveFileLogPath parent dir ensure failed. resolvedPath=%s", resolvedPath);
    return false;
  }
  strncpy(activeFileLogPath, resolvedPath, sizeof(activeFileLogPath) - 1);
  activeFileLogPathInitialized = true;
  activeFileLogPathUsesSyncedTime = nowSynchronized;
  activeFileLogPathCreated = false;
  return true;
}

/**
 * @brief NVS上の起動回数を更新し、今回の起動回数を確定する。
 * @return 更新成功時true。
 */
bool initializeFileLogBootCount() {
  Preferences preferences;
  if (!preferences.begin(fileLogPreferencesNamespace, false)) {
    return false;
  }
  uint32_t storedBootCount = preferences.getUInt(fileLogBootCountKey, 0);
  runtimeBootCount = storedBootCount + 1;
  unsyncedLogFileSequence = 1;
  syncedLogFileSequence = 1;
  preferences.putUInt(fileLogBootCountKey, runtimeBootCount);
  preferences.end();
  return true;
}

/**
 * @brief ログファイル一覧を再帰取得する。
 * @param directoryPath 対象ディレクトリ。
 * @param fileEntriesOut 出力先。
 */
void collectLogFilesRecursively(const String& directoryPath, std::vector<logFileEntry>* fileEntriesOut) {
  if (fileEntriesOut == nullptr) {
    return;
  }
  File directory = LittleFS.open(directoryPath, "r");
  if (!directory || !directory.isDirectory()) {
    if (directory) {
      directory.close();
    }
    return;
  }
  File child = directory.openNextFile();
  while (child) {
    String childPath = String(child.path());
    if (child.isDirectory()) {
      child.close();
      collectLogFilesRecursively(childPath, fileEntriesOut);
    } else {
      logFileEntry entry;
      entry.path = childPath;
      entry.size = static_cast<size_t>(child.size());

      // 同期済み形式: /logs/YYYY/MMDD/YYYYMMDDHHMMSSmmm-xxxxx.log
      int yearDir = 0;
      int monthDayDir = 0;
      char timestampText[18] = {0};
      unsigned long sequence = 0;
      int syncedParsed = sscanf(childPath.c_str(),
                                "/logs/%4d/%4d/%17[0-9]-%5lu.log",
                                &yearDir,
                                &monthDayDir,
                                timestampText,
                                &sequence);
      if (syncedParsed == 4 && strlen(timestampText) >= 14) {
        entry.timestampAvailable = true;
        entry.timestamp14 = String(timestampText).substring(0, 14);
        entry.sortKey = String("0-") + String(timestampText);
      } else {
        // 未同期形式: /logs/<bootCount>/<fileSequence>.log
        unsigned long bootCount = 0;
        unsigned long fileSequence = 0;
        int unsyncedParsed = sscanf(childPath.c_str(),
                                    "/logs/%lu/%lu.log",
                                    &bootCount,
                                    &fileSequence);
        if (unsyncedParsed == 2) {
          char unsyncedSortKey[48] = {};
          snprintf(unsyncedSortKey,
                   sizeof(unsyncedSortKey),
                   "1-%010lu-%010lu",
                   bootCount,
                   fileSequence);
          entry.sortKey = String(unsyncedSortKey);
        } else {
          entry.sortKey = String("9-") + childPath;
        }
      }
      fileEntriesOut->push_back(entry);
      child.close();
    }
    child = directory.openNextFile();
  }
  directory.close();
}

/**
 * @brief 30日超過/総量超過のログを削除する。
 */
void pruneFileLogsIfNeeded() {
  if (!ensureFileLogLittleFsReady()) {
    return;
  }
  std::vector<logFileEntry> fileEntries;
  collectLogFilesRecursively("/logs", &fileEntries);
  if (fileEntries.empty()) {
    return;
  }

  // 30日超過削除（同期済み形式のみ）
  if (isCurrentUtcSynchronized()) {
    time_t nowEpochSeconds = time(nullptr);
    time_t retentionThreshold = nowEpochSeconds - (static_cast<time_t>(fileLogRetentionDays) * 24 * 60 * 60);
    struct tm thresholdUtc {};
    if (gmtime_r(&retentionThreshold, &thresholdUtc) != nullptr) {
      char thresholdTimestamp14[16] = {};
      strftime(thresholdTimestamp14, sizeof(thresholdTimestamp14), "%Y%m%d%H%M%S", &thresholdUtc);
      for (const logFileEntry& entry : fileEntries) {
        if (!entry.timestampAvailable) {
          continue;
        }
        if (entry.timestamp14.compareTo(String(thresholdTimestamp14)) < 0) {
          if (entry.path != String(activeFileLogPath)) {
            LittleFS.remove(entry.path);
          }
        }
      }
    }
  }

  fileEntries.clear();
  collectLogFilesRecursively("/logs", &fileEntries);
  if (fileEntries.empty()) {
    return;
  }

  size_t totalSize = 0;
  for (const logFileEntry& entry : fileEntries) {
    totalSize += entry.size;
  }
  if (totalSize <= fileLogTotalSizeLimitBytes) {
    return;
  }

  std::sort(fileEntries.begin(), fileEntries.end(), [](const logFileEntry& left, const logFileEntry& right) {
    return left.sortKey < right.sortKey;
  });

  for (const logFileEntry& entry : fileEntries) {
    if (totalSize <= fileLogTotalSizeLimitBytes) {
      break;
    }
    if (entry.path == String(activeFileLogPath)) {
      continue;
    }
    if (LittleFS.remove(entry.path)) {
      // [重要] appLog* を使うと再帰するため、低レベル出力を使って削除証跡を残す。
      esp_log_write(ESP_LOG_INFO,
                    "iotApp",
                    "[INFO][UTC:(unsynchronized)] pruneFileLogsIfNeeded removed. path=%s size=%u",
                    entry.path.c_str(),
                    static_cast<unsigned>(entry.size));
      if (entry.size <= totalSize) {
        totalSize -= entry.size;
      } else {
        totalSize = 0;
      }
    }
  }
}

/**
 * @brief 必要に応じてログファイルをローテーションする。
 * @param nextWriteBytes 次に書き込むバイト数（改行含む）。
 * @return ローテーション後に書込み可能ならtrue。
 */
bool rotateLogFileIfNeeded(size_t nextWriteBytes) {
  if (!resolveActiveFileLogPath()) {
    writeFileLogFailureTrace("rotateLogFileIfNeeded resolve failed. nextWriteBytes=%lu",
                             static_cast<unsigned long>(nextWriteBytes));
    return false;
  }
  const String previousPath = String(activeFileLogPath);
  size_t currentFileSize = 0;
  if (activeFileLogPathCreated) {
    File currentLogFile = LittleFS.open(activeFileLogPath, "r");
    if (currentLogFile) {
      currentFileSize = static_cast<size_t>(currentLogFile.size());
      currentLogFile.close();
    } else {
      writeFileLogFailureTrace("rotateLogFileIfNeeded open current file failed. path=%s", activeFileLogPath);
    }
  } else {
    // [重要] 未作成ファイルに対して exists/open を行うと、環境依存で不要なVFSエラーが出ることがある。
    // 理由: 初回書込み前の存在確認を避け、誤検知ノイズを抑えるため。
    writeFileLogFailureTrace("rotateLogFileIfNeeded skip file size probe for not-yet-created path. path=%s", activeFileLogPath);
  } 
  if (currentFileSize + nextWriteBytes <= fileLogRotateThresholdBytes) {
    return true;
  }
  if (!activeFileLogPathUsesSyncedTime) {
    unsyncedLogFileSequence += 1;
  }
  activeFileLogPathInitialized = false;
  memset(activeFileLogPath, 0, sizeof(activeFileLogPath));
  bool resolveResult = resolveActiveFileLogPath();
  if (resolveResult) {
    // [重要] appLog* を使うと再帰するため、低レベル出力でローテーション証跡を残す。
    esp_log_write(ESP_LOG_INFO,
                  "iotApp",
                  "[INFO][UTC:(unsynchronized)] rotateLogFileIfNeeded rotated. previous=%s next=%s",
                  previousPath.c_str(),
                  activeFileLogPath);
  }
  return resolveResult;
}

/**
 * @brief ファイルへ1行追記する。
 * @param lineText 追記行（改行なし）。
 * @return 追記成功時true。
 * @details
 * - [重要] 本関数はシリアルログと分離したファイルログ専用。
 * - [厳守] 内部で `appLog*` を呼ばず、再帰ログ出力を避ける。
 */
bool appendLineToFileLog(const char* lineText) {
  if (lineText == nullptr) {
    writeFileLogFailureTrace("appendLineToFileLog lineText is null.");
    fileLogConsecutiveFailureCount += 1;
    return false;
  }
  fileLogQueueItem singleItem{};
  singleItem.logLevel = ESP_LOG_INFO;
  strncpy(singleItem.lineText, lineText, sizeof(singleItem.lineText) - 1);
  singleItem.lineText[sizeof(singleItem.lineText) - 1] = '\0';
  return appendBatchToFileLog(&singleItem, 1);
}

/**
 * @brief ファイルへ複数行をまとめて追記する。
 * @param queueItems 追記行配列。
 * @param itemCount 追記行数。
 * @return 追記成功時true。
 */
bool appendBatchToFileLog(const fileLogQueueItem* queueItems, size_t itemCount) {
  if (queueItems == nullptr || itemCount == 0) {
    writeFileLogFailureTrace("appendBatchToFileLog invalid parameter. queueItems=%p itemCount=%u",
                             queueItems,
                             static_cast<unsigned>(itemCount));
    fileLogConsecutiveFailureCount += 1;
    return false;
  }
  if (itemCount > fileLogBatchMaxItems) {
    writeFileLogFailureTrace("appendBatchToFileLog itemCount too large. itemCount=%u max=%u",
                             static_cast<unsigned>(itemCount),
                             static_cast<unsigned>(fileLogBatchMaxItems));
    itemCount = fileLogBatchMaxItems;
  }
  if (!ensureFileLogLittleFsReady()) {
    writeFileLogFailureTrace("appendBatchToFileLog LittleFS not ready.");
    fileLogConsecutiveFailureCount += 1;
    return false;
  }
  size_t totalWriteBytes = 0;
  for (size_t index = 0; index < itemCount; ++index) {
    totalWriteBytes += strlen(queueItems[index].lineText) + 1;
  }
  if (!rotateLogFileIfNeeded(totalWriteBytes)) {
    writeFileLogFailureTrace("appendBatchToFileLog rotate failed. path=%s totalWriteBytes=%lu itemCount=%u",
                             activeFileLogPath,
                             static_cast<unsigned long>(totalWriteBytes),
                             static_cast<unsigned>(itemCount));
    fileLogConsecutiveFailureCount += 1;
    return false;
  }
  File logFile = LittleFS.open(activeFileLogPath, "a");
  if (!logFile) {
    char parentPath[160] = {};
    bool parentPathReady = extractParentDirectoryPath(activeFileLogPath, parentPath, sizeof(parentPath));
    bool logsRootExists = LittleFS.exists("/logs");
    bool parentExists = parentPathReady ? LittleFS.exists(parentPath) : false;
    bool parentIsDirectory = parentPathReady ? isExistingDirectoryPath(parentPath) : false;
    writeFileLogFailureTrace("appendBatchToFileLog open failed. path=%s exists=%d",
                             activeFileLogPath,
                             LittleFS.exists(activeFileLogPath) ? 1 : 0);
    writeFileLogFailureTrace("appendBatchToFileLog open context. logsRootExists=%d parentReady=%d parent=%s parentExists=%d parentIsDir=%d",
                             logsRootExists ? 1 : 0,
                             parentPathReady ? 1 : 0,
                             (parentPathReady ? parentPath : "(unavailable)"),
                             parentExists ? 1 : 0,
                             parentIsDirectory ? 1 : 0);
    // [重要] FS状態が揺らいだケースの切り分けのため、親ディレクトリ再作成を1回だけ試す。
    // 理由: mkdir再実行で復旧するなら、open失敗の主因はディレクトリ実体の不整合と判断できるため。
    bool ensureParentResult = ensureParentDirectoryExistsForFilePath(activeFileLogPath);
    if (ensureParentResult) {
      File retryLogFile = LittleFS.open(activeFileLogPath, "a");
      if (retryLogFile) {
        logFile = retryLogFile;
        writeFileLogFailureTrace("appendBatchToFileLog open retry succeeded. path=%s", activeFileLogPath);
      } else {
        writeFileLogFailureTrace("appendBatchToFileLog open retry failed. path=%s", activeFileLogPath);
      }
    } else {
      writeFileLogFailureTrace("appendBatchToFileLog ensure parent before retry failed. path=%s", activeFileLogPath);
    }
  }
  if (!logFile) {
    fileLogConsecutiveFailureCount += 1;
    return false;
  }
  bool writeResult = true;
  for (size_t index = 0; index < itemCount; ++index) {
    size_t lineLength = strlen(queueItems[index].lineText);
    size_t writtenLength = logFile.write(reinterpret_cast<const uint8_t*>(queueItems[index].lineText), lineLength);
    size_t newlineLength = logFile.write(reinterpret_cast<const uint8_t*>("\n"), 1);
    if (!(writtenLength == lineLength && newlineLength == 1)) {
      writeFileLogFailureTrace("appendBatchToFileLog write failed. index=%u path=%s expected=%lu actual=%lu newline=%lu",
                               static_cast<unsigned>(index),
                               activeFileLogPath,
                               static_cast<unsigned long>(lineLength),
                               static_cast<unsigned long>(writtenLength),
                               static_cast<unsigned long>(newlineLength));
      writeResult = false;
      break;
    }
  }
  logFile.flush();
  logFile.close();
  if (writeResult) {
    activeFileLogPathCreated = true;
    fileLogConsecutiveFailureCount = 0;
    pruneFileLogsIfNeeded();
  } else {
    fileLogConsecutiveFailureCount += 1;
  }
  if (fileLogConsecutiveFailureCount >= fileLogFailureAutoDisableThreshold) {
    fileLogEnabled = false;
    esp_log_write(ESP_LOG_ERROR,
                  "iotApp",
                  "[ERROR][UTC:(unsynchronized)] file log auto disabled after consecutive failures. count=%u",
                  static_cast<unsigned>(fileLogConsecutiveFailureCount));
  }
  return writeResult;
}

/**
 * @brief シリアルログ向け整形済み内容をファイルへ追記する。
 * @param levelText 表示レベル文字列。
 * @param utcText UTC表示文字列。
 * @param formattedMessage フォーマット済みメッセージ。
 * @return なし。
 */
void writeFileLogLine(esp_log_level_t logLevel,
                      const char* levelText,
                      const char* utcText,
                      const char* formattedMessage,
                      uint16_t eventCode) {
  if (levelText == nullptr || utcText == nullptr || formattedMessage == nullptr) {
    return;
  }
  if (fileLogDebugMinimalOneShotMode) {
    if (fileLogDebugOneShotWritten) {
      return;
    }
    // [重要] 7048/7049 の再現切り分けのため、ファイルログ負荷を最小化する。
    // 理由: 連続書込みを止めた状態で stack canary 再現有無を判定するため。
    const bool writeResult = appendLineToFileLog("[DEBUG][MINIMAL] one-shot file log line");
    if (writeResult) {
      fileLogDebugOneShotWritten = true;
    }
    return;
  }
  // [重要] タスクスタックを圧迫しないため、整形バッファは静的領域を使う。
  static char lineBuffer[768] = {};
  int printedLength = 0;
  if (logLevel >= ESP_LOG_WARN) {
    printedLength = snprintf(lineBuffer,
                             sizeof(lineBuffer),
                             "[%s][UTC:%s][E:%04u] %.*s",
                             levelText,
                             utcText,
                             static_cast<unsigned>(eventCode),
                             static_cast<int>(fileLogMessageBodyMaxChars),
                             formattedMessage);
  } else {
    printedLength = snprintf(lineBuffer,
                             sizeof(lineBuffer),
                             "[%s][UTC:%s][E:%04u]",
                             levelText,
                             utcText,
                             static_cast<unsigned>(eventCode));
  }
  if (printedLength <= 0 || printedLength >= static_cast<int>(sizeof(lineBuffer))) {
    return;
  }
  enqueueFileLogLine(logLevel, lineBuffer);
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

  if (IOT_ENABLE_FILE_LOG && fileLogEnabled) {
    bool shouldWriteFile = shouldWriteToFileLog(logLevel);
    if (shouldWriteFile) {
      uint16_t eventCode = calculateEventCodeFromFormat(format);
      writeFileLogLine(logLevel, levelText, utcText, formattedMessage, eventCode);
    }
  }
  esp_log_write(logLevel, "iotApp", "[%s][UTC:%s] %s", levelText, utcText, formattedMessage);
}

void setFileLogEnabled(bool enabled) {
  if (!IOT_ENABLE_FILE_LOG) {
    fileLogEnabled = false;
    return;
  }
  fileLogEnabled = enabled;
  if (enabled) {
    ensureFileLogWriterReady();
    fileLogDebugOneShotWritten = false;
    fileLogInfoSeenCount = 0;
    fileLogDroppedCount = 0;
    fileLogDebugFailureTraceCount = 0;
    fileLogConsecutiveFailureCount = 0;
    fileLogWarnWindowStartMs = millis();
    fileLogWarnWrittenInWindow = 0;
    fileLogWarnDroppedInWindow = 0;
  }
}

bool isFileLogEnabled() {
  return IOT_ENABLE_FILE_LOG && fileLogEnabled;
}

void initializeLogLevel() {
  initializeFileLogBootCount();
  const esp_log_level_t selectedLogLevel = firmwareMode::kDiagnosticLogEnabled ? ESP_LOG_DEBUG : ESP_LOG_WARN;
  const char* selectedLogLevelText = firmwareMode::kDiagnosticLogEnabled ? "DEBUG" : "WARN";
  esp_log_level_set("*", selectedLogLevel);
  appLogWarn("initializeLogLevel completed. firmwareOperationMode=%s serialOutputMode=%s factoryApisEnabled=%d serialLogLevel=%s",
             firmwareMode::kFirmwareOperationMode,
             firmwareMode::kSerialOutputMode,
             static_cast<int>(firmwareMode::kFactoryApisEnabled),
             selectedLogLevelText);
}
