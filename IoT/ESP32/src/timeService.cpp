/**
 * @file timeService.cpp
 * @brief タイムサーバー同期と内部時刻状態を管理するサービス実装。
 * @details
 * - [重要] 同期後はESP32内部時刻が進み続けるため、常時サーバー接続は不要。
 * - [厳守] 同期失敗時は詳細ログを残すが、機密値はマスクして扱う。
 * - [制限] NTP同期のみ対応し、ユーザー認証/TLS時刻配信は将来対応。
 */

#include "timeService.h"

#include <time.h>
#include <WiFi.h>

#include "log.h"

namespace {
/** @brief 同期完了判定に使う最小妥当UNIX時刻（2021-01-01 UTC）。 */
constexpr time_t minimumValidEpochSeconds = 1609459200;
/** @brief 同期完了待機回数。 */
constexpr int32_t syncWaitRetryCount = 60;
/** @brief 同期完了待機間隔(ms)。 */
constexpr int32_t syncWaitIntervalMs = 500;
/** @brief 予備NTPサーバー1。 */
constexpr const char* fallbackNtpServer1 = "pool.ntp.org";
/** @brief 予備NTPサーバー2。 */
constexpr const char* fallbackNtpServer2 = "time.google.com";
}

bool timeService::initializeAndSync(const String& timeServerUrl,
                                    const String& timeServerUser,
                                    const String& timeServerPass,
                                    int32_t timeServerPort,
                                    bool timeServerTls) {
  constexpr const char* functionName = "timeService::initializeAndSync";
  if (timeServerUrl.length() <= 0) {
    appLogError("%s failed. timeServerUrl is empty.", functionName);
    return false;
  }
  if (timeServerPort <= 0 || timeServerPort > 65535) {
    appLogError("%s failed. invalid timeServerPort=%ld", functionName, static_cast<long>(timeServerPort));
    return false;
  }

  timeServerUrl_ = timeServerUrl;
  timeServerUser_ = timeServerUser;
  timeServerPass_ = timeServerPass;
  timeServerPort_ = timeServerPort;
  timeServerTls_ = timeServerTls;

  return syncNow();
}

bool timeService::syncNow() {
  constexpr const char* functionName = "timeService::syncNow";
  if (timeServerUrl_.length() <= 0) {
    appLogError("%s failed. timeServerUrl is empty. initializeAndSync was not completed.", functionName);
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    appLogWarn("%s skipped. wifi is not connected. wifiStatus=%d", functionName, static_cast<int>(WiFi.status()));
    return false;
  }
  if (timeServerTls_) {
    appLogWarn("%s note. timeServerTls=true is currently not supported for NTP. fallback to standard NTP request.", functionName);
  }
  if (timeServerPort_ != 123) {
    appLogWarn("%s note. timeServerPort=%ld is set, but Arduino configTime() uses SNTP standard flow.", functionName, static_cast<long>(timeServerPort_));
  }
  if (timeServerUser_.length() > 0 || timeServerPass_.length() > 0) {
    appLogWarn("%s note. user/password for time server are currently not used by NTP implementation.", functionName);
  }

  appLogInfo("%s start. primary=%s fallback1=%s fallback2=%s",
             functionName,
             timeServerUrl_.c_str(),
             fallbackNtpServer1,
             fallbackNtpServer2);
  // [重要] 主サーバー不達時に過去回帰を避けるため、既知の公開NTPを予備として同時設定する。
  configTime(0, 0, timeServerUrl_.c_str(), fallbackNtpServer1, fallbackNtpServer2);
  bool syncResult = waitForSntpSync(functionName);
  if (!syncResult) {
    appLogError("%s failed. waitForSntpSync returned false. timeServerUrl=%s", functionName, timeServerUrl_.c_str());
    return false;
  }

  hasSynchronizedOnce_ = true;
  appLogInfo("%s succeeded. utcNow=%s", functionName, getCurrentUtcIso8601().c_str());
  return true;
}

bool timeService::hasSynchronizedOnce() const {
  return hasSynchronizedOnce_;
}

String timeService::getCurrentUtcIso8601() const {
  struct tm utcTimeInfo {};
  time_t currentEpochSeconds = time(nullptr);
  if (currentEpochSeconds < minimumValidEpochSeconds) {
    return "(unsynchronized)";
  }
  bool gmTimeResult = gmtime_r(&currentEpochSeconds, &utcTimeInfo) != nullptr;
  if (!gmTimeResult) {
    return "(gmtime_r failed)";
  }
  char formattedBuffer[32] = {};
  size_t writtenLength = strftime(formattedBuffer, sizeof(formattedBuffer), "%Y-%m-%dT%H:%M:%SZ", &utcTimeInfo);
  if (writtenLength <= 0) {
    return "(strftime failed)";
  }
  return String(formattedBuffer);
}

bool timeService::waitForSntpSync(const char* functionName) {
  if (functionName == nullptr) {
    functionName = "timeService::waitForSntpSync";
  }

  for (int32_t retryIndex = 0; retryIndex < syncWaitRetryCount; ++retryIndex) {
    time_t currentEpochSeconds = time(nullptr);
    if (currentEpochSeconds >= minimumValidEpochSeconds) {
      return true;
    }
    appLogDebug("%s waiting SNTP sync. retry=%ld/%ld currentEpoch=%ld",
                functionName,
                static_cast<long>(retryIndex + 1),
                static_cast<long>(syncWaitRetryCount),
                static_cast<long>(currentEpochSeconds));
    vTaskDelay(pdMS_TO_TICKS(syncWaitIntervalMs));
  }

  return false;
}
