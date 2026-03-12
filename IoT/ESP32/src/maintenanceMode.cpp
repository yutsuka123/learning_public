/**
 * @file maintenanceMode.cpp
 * @brief メンテナンス(AP)モード遷移要求のNVS保存処理。
 * @details
 * - [重要] 不揮発フラグを使って、再起動をまたいだメンテナンスモード遷移を成立させる。
 * - [厳守] consume 時にフラグをfalseへ戻し、次回起動へ持ち越さない。
 * - [禁止] 失敗時に黙って成功扱いしない（必ずログを残す）。
 */

#include "../header/maintenanceMode.h"

#include <Preferences.h>

#include "log.h"

namespace {
constexpr const char* kPreferencesNamespace = "maintmode";
constexpr const char* kRequestKey = "requestAp";

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

namespace maintenanceMode {

bool requestMaintenanceModeOnNextBoot() {
  constexpr const char* functionName = "maintenanceMode::requestMaintenanceModeOnNextBoot";
  Preferences preferences;
  if (!openPreferences(&preferences, false, functionName)) {
    return false;
  }
  preferences.putBool(kRequestKey, true);
  preferences.end();
  appLogWarn("%s success. next boot enters maintenance mode.", functionName);
  return true;
}

bool consumeMaintenanceModeRequest() {
  constexpr const char* functionName = "maintenanceMode::consumeMaintenanceModeRequest";
  Preferences preferences;
  if (!openPreferences(&preferences, false, functionName)) {
    return false;
  }
  const bool requested = preferences.getBool(kRequestKey, false);
  if (requested) {
    preferences.putBool(kRequestKey, false);
  }
  preferences.end();
  appLogInfo("%s checked. requested=%d", functionName, requested ? 1 : 0);
  return requested;
}

}  // namespace maintenanceMode

