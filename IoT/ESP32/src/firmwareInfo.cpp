/**
 * @file firmwareInfo.cpp
 * @brief 稼働中ファームウェアの版数と書換日時を永続化する実装。
 * @details
 * - [重要] OTA成功時刻はNVS(Preferences)へ保存し、再起動後のstatus通知でも参照できるようにする。
 * - [厳守] 保存値は現在の `firmwareVersion` と一致した場合のみ採用し、版数不一致時はフォールバックへ戻す。
 * - [禁止] 機密値を扱う用途へ流用しない。このモジュールは版数と日時だけを扱う。
 */

#include "firmwareInfo.h"

#include <Preferences.h>

#include "log.h"

namespace firmwareInfo {
namespace {

constexpr const char* kPreferencesNamespace = "fwinfo";
constexpr const char* kFirmwareVersionKey = "version";
constexpr const char* kFirmwareWrittenAtKey = "writtenAt";

/**
 * @brief Preferencesを開く。
 * @param preferencesOut 利用するPreferencesインスタンス。
 * @param readOnly 読み取り専用ならtrue。
 * @param functionName 呼び出し元関数名。
 * @return オープン成功時true、失敗時false。
 */
bool openPreferences(Preferences* preferencesOut, bool readOnly, const char* functionName) {
  if (preferencesOut == nullptr) {
    appLogError("%s failed. preferencesOut is null.", functionName == nullptr ? "openPreferences" : functionName);
    return false;
  }
  const char* safeFunctionName = (functionName == nullptr) ? "openPreferences" : functionName;
  if (!preferencesOut->begin(kPreferencesNamespace, readOnly)) {
    appLogError("%s failed. Preferences.begin returned false. namespace=%s readOnly=%d",
                safeFunctionName,
                kPreferencesNamespace,
                readOnly ? 1 : 0);
    return false;
  }
  return true;
}

}  // namespace

bool saveOtaAppliedAt(const String& firmwareVersion, const String& firmwareWrittenAt) {
  constexpr const char* functionName = "firmwareInfo::saveOtaAppliedAt";
  if (firmwareVersion.length() <= 0) {
    appLogError("%s failed. firmwareVersion is empty.", functionName);
    return false;
  }
  if (firmwareWrittenAt.length() <= 0) {
    appLogError("%s failed. firmwareWrittenAt is empty. firmwareVersion=%s", functionName, firmwareVersion.c_str());
    return false;
  }

  Preferences preferences;
  if (!openPreferences(&preferences, false, functionName)) {
    return false;
  }

  const size_t versionWriteSize = preferences.putString(kFirmwareVersionKey, firmwareVersion);
  const size_t writtenAtWriteSize = preferences.putString(kFirmwareWrittenAtKey, firmwareWrittenAt);
  preferences.end();

  if (versionWriteSize <= 0 || writtenAtWriteSize <= 0) {
    appLogError("%s failed. putString returned zero. firmwareVersion=%s firmwareWrittenAt=%s versionWriteSize=%u writtenAtWriteSize=%u",
                functionName,
                firmwareVersion.c_str(),
                firmwareWrittenAt.c_str(),
                static_cast<unsigned>(versionWriteSize),
                static_cast<unsigned>(writtenAtWriteSize));
    return false;
  }

  appLogInfo("%s succeeded. firmwareVersion=%s firmwareWrittenAt=%s",
             functionName,
             firmwareVersion.c_str(),
             firmwareWrittenAt.c_str());
  return true;
}

String resolveFirmwareWrittenAtForStatus(const String& currentFirmwareVersion,
                                         const String& fallbackFirmwareWrittenAt) {
  constexpr const char* functionName = "firmwareInfo::resolveFirmwareWrittenAtForStatus";
  if (currentFirmwareVersion.length() <= 0) {
    appLogWarn("%s fallback applied. currentFirmwareVersion is empty.", functionName);
    return fallbackFirmwareWrittenAt;
  }

  Preferences preferences;
  if (!openPreferences(&preferences, true, functionName)) {
    appLogWarn("%s fallback applied. could not open preferences. firmwareVersion=%s fallback=%s",
               functionName,
               currentFirmwareVersion.c_str(),
               fallbackFirmwareWrittenAt.c_str());
    return fallbackFirmwareWrittenAt;
  }

  const String savedFirmwareVersion = preferences.getString(kFirmwareVersionKey, "");
  const String savedFirmwareWrittenAt = preferences.getString(kFirmwareWrittenAtKey, "");
  preferences.end();

  if (savedFirmwareVersion != currentFirmwareVersion || savedFirmwareWrittenAt.length() <= 0) {
    appLogInfo("%s fallback applied. currentVersion=%s savedVersion=%s savedWrittenAt=%s fallback=%s",
               functionName,
               currentFirmwareVersion.c_str(),
               savedFirmwareVersion.c_str(),
               savedFirmwareWrittenAt.c_str(),
               fallbackFirmwareWrittenAt.c_str());
    return fallbackFirmwareWrittenAt;
  }

  return savedFirmwareWrittenAt;
}

}  // namespace firmwareInfo
