/**
 * @file otaRollback.cpp
 * @brief OTA rollback 試験と起動確定処理の実装。
 * @details
 * - [重要] rollback 有効環境では `esp_ota_mark_app_valid_cancel_rollback()` を実行して新面を確定する。
 * - [厳守] 試験専用の未確定起動失敗モードは明示有効時のみ動作する。
 * - [禁止] 試験モードを有効化したまま通常運用へ移行しない。
 */

#include "otaRollback.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_err.h>

#include "log.h"

namespace otaRollback {
namespace {

constexpr const char* rollbackPreferencesNamespace = "otaRollback";
constexpr const char* rollbackFailureTestModeKey = "failTestMode";
constexpr const char* rollbackFailureRestartedKey = "failRestarted";

bool triggeredFailureRestartThisBoot = false;

/**
 * @brief Preferences を開く。
 * @param preferencesOut 利用する Preferences インスタンス。
 * @param readOnly 読み取り専用なら true。
 * @param functionName 呼び出し元関数名。
 * @return 成功時 true。
 */
bool openPreferences(Preferences* preferencesOut, bool readOnly, const char* functionName) {
  if (preferencesOut == nullptr) {
    appLogError("%s failed. preferencesOut is null.", functionName == nullptr ? "openPreferences" : functionName);
    return false;
  }
  if (!preferencesOut->begin(rollbackPreferencesNamespace, readOnly)) {
    appLogError("%s failed. Preferences.begin returned false. namespace=%s readOnly=%d",
                functionName == nullptr ? "openPreferences" : functionName,
                rollbackPreferencesNamespace,
                readOnly ? 1 : 0);
    return false;
  }
  return true;
}

}  // namespace

bool initializeOnBoot() {
  constexpr const char* functionName = "otaRollback::initializeOnBoot";
  triggeredFailureRestartThisBoot = false;

  Preferences preferences;
  if (!openPreferences(&preferences, false, functionName)) {
    return false;
  }

  const bool failureTestModeEnabled = preferences.getBool(rollbackFailureTestModeKey, false);
  const bool alreadyRestarted = preferences.getBool(rollbackFailureRestartedKey, false);
  preferences.end();

  if (!failureTestModeEnabled || alreadyRestarted) {
    appLogInfo("%s: skip failure restart. testMode=%d alreadyRestarted=%d",
               functionName,
               failureTestModeEnabled ? 1 : 0,
               alreadyRestarted ? 1 : 0);
    return true;
  }

  Preferences writePreferences;
  if (!openPreferences(&writePreferences, false, functionName)) {
    return false;
  }
  writePreferences.putBool(rollbackFailureRestartedKey, true);
  writePreferences.end();

  triggeredFailureRestartThisBoot = true;
  appLogWarn("%s: rollback failure test mode active. force app invalid and rollback reboot.", functionName);
  const esp_err_t rollbackResult = esp_ota_mark_app_invalid_rollback_and_reboot();
  if (rollbackResult != ESP_OK) {
    appLogError("%s: esp_ota_mark_app_invalid_rollback_and_reboot failed. err=0x%x",
                functionName,
                static_cast<unsigned>(rollbackResult));
    delay(500);
    esp_restart();
  }
  return true;
}

bool confirmCurrentAppIfNeeded() {
  constexpr const char* functionName = "otaRollback::confirmCurrentAppIfNeeded";

  const esp_partition_t* runningPartition = esp_ota_get_running_partition();
  const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
  if (runningPartition != nullptr && bootPartition != nullptr) {
    appLogInfo("%s: runningLabel=%s bootLabel=%s", functionName, runningPartition->label, bootPartition->label);
  }

  const esp_err_t markResult = esp_ota_mark_app_valid_cancel_rollback();
  if (markResult == ESP_OK) {
    appLogInfo("%s succeeded. app marked valid.", functionName);
  } else if (markResult == ESP_ERR_NOT_SUPPORTED || markResult == ESP_ERR_INVALID_STATE) {
    appLogWarn("%s skipped. rollback feature not active. err=0x%x", functionName, static_cast<unsigned>(markResult));
  } else {
    appLogError("%s failed. err=0x%x", functionName, static_cast<unsigned>(markResult));
    return false;
  }

  Preferences preferences;
  if (!openPreferences(&preferences, false, functionName)) {
    return false;
  }
  preferences.putBool(rollbackFailureRestartedKey, false);
  preferences.end();
  return true;
}

bool enableRollbackFailureTestMode() {
  constexpr const char* functionName = "otaRollback::enableRollbackFailureTestMode";
  Preferences preferences;
  if (!openPreferences(&preferences, false, functionName)) {
    return false;
  }
  preferences.putBool(rollbackFailureTestModeKey, true);
  preferences.putBool(rollbackFailureRestartedKey, false);
  preferences.end();
  appLogWarn("%s: enabled.", functionName);
  return true;
}

bool disableRollbackFailureTestMode() {
  constexpr const char* functionName = "otaRollback::disableRollbackFailureTestMode";
  Preferences preferences;
  if (!openPreferences(&preferences, false, functionName)) {
    return false;
  }
  preferences.putBool(rollbackFailureTestModeKey, false);
  preferences.putBool(rollbackFailureRestartedKey, false);
  preferences.end();
  appLogInfo("%s: disabled.", functionName);
  return true;
}

bool isRollbackFailureTestModeEnabled() {
  constexpr const char* functionName = "otaRollback::isRollbackFailureTestModeEnabled";
  Preferences preferences;
  if (!openPreferences(&preferences, true, functionName)) {
    return false;
  }
  const bool enabled = preferences.getBool(rollbackFailureTestModeKey, false);
  preferences.end();
  return enabled;
}

bool hasTriggeredFailureRestartThisBoot() {
  return triggeredFailureRestartThisBoot;
}

}  // namespace otaRollback
