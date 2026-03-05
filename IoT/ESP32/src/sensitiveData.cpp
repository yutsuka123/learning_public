/**
 * @file sensitiveData.cpp
 * @brief 機密データ（Wi-Fi/MQTT/サーバー設定）をJSONで保存・読込する実装。
 * @details
 * - [重要] 保存先は LittleFS の `/sensitiveData.json` 固定。
 * - [厳守] デフォルト値は MQTT TLS=false / MQTT Port=8883。
 * - [禁止] パスワード値をログへ直接出力しない。
 * - [将来対応] 暗号化保存やNVS移行は別タスクで対応する。
 */

#include "sensitiveDataService.h"

#include <FS.h>
#include <LittleFS.h>
#include <cJSON.h>

#include "common.h"
#include "log.h"

namespace {

constexpr const char* sensitiveDataFilePath = "/sensitiveData.json";
constexpr const char* wifiRootKey = "wifi";
constexpr const char* mqttRootKey = "mqtt";
constexpr const char* serverRootKey = "server";
constexpr const char* otaRootKey = "ota";
constexpr const char* timeServerRootKey = "timeServer";
constexpr int32_t defaultMqttPort = 8883;
constexpr bool defaultMqttTls = false;
constexpr int32_t defaultServerPort = 443;
constexpr bool defaultServerTls = true;
constexpr int32_t defaultOtaPort = 443;
constexpr bool defaultOtaTls = true;
constexpr int32_t defaultTimeServerPort = 123;
constexpr bool defaultTimeServerTls = false;

/**
 * @brief オブジェクトに文字列項目を上書き設定する。
 * @param targetObject 設定先JSONオブジェクト。
 * @param itemKey 項目キー。
 * @param itemValue 設定する値。
 * @param functionName 呼び出し元関数名。
 * @return 成功時true、失敗時false。
 */
bool setStringItem(cJSON* targetObject,
                   const char* itemKey,
                   const String& itemValue,
                   const char* functionName) {
  if (targetObject == nullptr || itemKey == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. targetObject=%p, itemKey=%p", functionName, targetObject, itemKey);
    return false;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(targetObject, itemKey);
  cJSON* addedItem = cJSON_AddStringToObject(targetObject, itemKey, itemValue.c_str());
  if (addedItem == nullptr) {
    appLogError("%s failed. cJSON_AddStringToObject key=%s", functionName, itemKey);
    return false;
  }
  return true;
}

/**
 * @brief オブジェクトに数値項目を上書き設定する。
 * @param targetObject 設定先JSONオブジェクト。
 * @param itemKey 項目キー。
 * @param itemValue 設定する値。
 * @param functionName 呼び出し元関数名。
 * @return 成功時true、失敗時false。
 */
bool setNumberItem(cJSON* targetObject,
                   const char* itemKey,
                   int32_t itemValue,
                   const char* functionName) {
  if (targetObject == nullptr || itemKey == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. targetObject=%p, itemKey=%p", functionName, targetObject, itemKey);
    return false;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(targetObject, itemKey);
  cJSON* addedItem = cJSON_AddNumberToObject(targetObject, itemKey, static_cast<double>(itemValue));
  if (addedItem == nullptr) {
    appLogError("%s failed. cJSON_AddNumberToObject key=%s, value=%ld", functionName, itemKey, static_cast<long>(itemValue));
    return false;
  }
  return true;
}

/**
 * @brief オブジェクトに真偽値項目を上書き設定する。
 * @param targetObject 設定先JSONオブジェクト。
 * @param itemKey 項目キー。
 * @param itemValue 設定する値。
 * @param functionName 呼び出し元関数名。
 * @return 成功時true、失敗時false。
 */
bool setBoolItem(cJSON* targetObject,
                 const char* itemKey,
                 bool itemValue,
                 const char* functionName) {
  if (targetObject == nullptr || itemKey == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. targetObject=%p, itemKey=%p", functionName, targetObject, itemKey);
    return false;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(targetObject, itemKey);
  cJSON* addedItem = cJSON_AddBoolToObject(targetObject, itemKey, itemValue);
  if (addedItem == nullptr) {
    appLogError("%s failed. cJSON_AddBoolToObject key=%s, value=%d", functionName, itemKey, static_cast<int>(itemValue));
    return false;
  }
  return true;
}

}  // namespace

bool sensitiveDataService::initialize() {
  constexpr const char* functionName = "sensitiveDataService::initialize";

  if (!LittleFS.begin(false)) {
    appLogError("%s failed. LittleFS.begin(formatOnFail=false) returned false.", functionName);
    appLogError("%s hint. filesystem image may be missing. run uploadfs before boot.", functionName);
    return false;
  }

  if (!ensureDefaultFileExists()) {
    appLogError("%s failed. ensureDefaultFileExists returned false.", functionName);
    return false;
  }

  // [重要] 旧形式ファイルからの移行を吸収するため、不足セクションを自動補完する。
  String jsonText;
  if (!readJsonText(&jsonText, functionName)) {
    return false;
  }
  cJSON* rootObject = cJSON_Parse(jsonText.c_str());
  if (rootObject == nullptr || !cJSON_IsObject(rootObject)) {
    appLogError("%s failed. cJSON_Parse error while migration. payloadLength=%d", functionName, jsonText.length());
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* serverObject = cJSON_GetObjectItemCaseSensitive(rootObject, serverRootKey);
  if (serverObject == nullptr || !cJSON_IsObject(serverObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, serverRootKey);
    serverObject = cJSON_AddObjectToObject(rootObject, serverRootKey);
  }
  cJSON* otaObject = cJSON_GetObjectItemCaseSensitive(rootObject, otaRootKey);
  if (otaObject == nullptr || !cJSON_IsObject(otaObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, otaRootKey);
    otaObject = cJSON_AddObjectToObject(rootObject, otaRootKey);
  }
  cJSON* timeServerObject = cJSON_GetObjectItemCaseSensitive(rootObject, timeServerRootKey);
  if (timeServerObject == nullptr || !cJSON_IsObject(timeServerObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, timeServerRootKey);
    timeServerObject = cJSON_AddObjectToObject(rootObject, timeServerRootKey);
  }
  if (serverObject == nullptr || otaObject == nullptr || timeServerObject == nullptr) {
    appLogError("%s failed. create missing object failed. server=%p ota=%p timeServer=%p",
                functionName,
                serverObject,
                otaObject,
                timeServerObject);
    cJSON_Delete(rootObject);
    return false;
  }

  bool migrationResult =
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl))), functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser))), functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass))), functionName) &&
      setNumberItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort, cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort)) ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort)->valuedouble) : defaultServerPort, functionName) &&
      setBoolItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls, cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls)) ? cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls)) : defaultServerTls, functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl))), functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser))), functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass))), functionName) &&
      setNumberItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort, cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort)) ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort)->valuedouble) : defaultOtaPort, functionName) &&
      setBoolItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls, cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls)) ? cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls)) : defaultOtaTls, functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl))), functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUser, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUser)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUser))), functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPass, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPass)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPass))), functionName) &&
      setNumberItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort, cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort)) ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort)->valuedouble) : defaultTimeServerPort, functionName) &&
      setBoolItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls, cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls)) ? cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls)) : defaultTimeServerTls, functionName);
  if (!migrationResult) {
    appLogError("%s failed. migration update failed.", functionName);
    cJSON_Delete(rootObject);
    return false;
  }

  char* migratedText = cJSON_PrintUnformatted(rootObject);
  if (migratedText == nullptr) {
    appLogError("%s failed. cJSON_PrintUnformatted returned null after migration.", functionName);
    cJSON_Delete(rootObject);
    return false;
  }
  bool migrationWriteResult = writeJsonText(String(migratedText), functionName);
  cJSON_free(migratedText);
  cJSON_Delete(rootObject);
  if (!migrationWriteResult) {
    appLogError("%s failed. writeJsonText after migration returned false.", functionName);
    return false;
  }

  appLogInfo("%s succeeded.", functionName);
  return true;
}

bool sensitiveDataService::saveWifiCredentials(const String& wifiSsid, const String& wifiPass) {
  constexpr const char* functionName = "sensitiveDataService::saveWifiCredentials";

  String jsonText;
  if (!readJsonText(&jsonText, functionName)) {
    return false;
  }

  cJSON* rootObject = cJSON_Parse(jsonText.c_str());
  if (rootObject == nullptr || !cJSON_IsObject(rootObject)) {
    appLogError("%s failed. cJSON_Parse error. payloadLength=%d", functionName, jsonText.length());
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* wifiObject = cJSON_GetObjectItemCaseSensitive(rootObject, wifiRootKey);
  if (wifiObject == nullptr || !cJSON_IsObject(wifiObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, wifiRootKey);
    wifiObject = cJSON_AddObjectToObject(rootObject, wifiRootKey);
    if (wifiObject == nullptr) {
      appLogError("%s failed. create wifi object key=%s", functionName, wifiRootKey);
      cJSON_Delete(rootObject);
      return false;
    }
  }

  bool updateResult = setStringItem(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiSsid, wifiSsid, functionName) &&
                      setStringItem(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiPass, wifiPass, functionName);
  if (!updateResult) {
    cJSON_Delete(rootObject);
    return false;
  }

  char* serializedText = cJSON_PrintUnformatted(rootObject);
  if (serializedText == nullptr) {
    appLogError("%s failed. cJSON_PrintUnformatted returned null.", functionName);
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::loadWifiCredentials(String* wifiSsidOut, String* wifiPassOut) {
  constexpr const char* functionName = "sensitiveDataService::loadWifiCredentials";
  if (wifiSsidOut == nullptr || wifiPassOut == nullptr) {
    appLogError("%s failed. output parameter is null. wifiSsidOut=%p, wifiPassOut=%p", functionName, wifiSsidOut, wifiPassOut);
    return false;
  }

  String jsonText;
  if (!readJsonText(&jsonText, functionName)) {
    return false;
  }

  cJSON* rootObject = cJSON_Parse(jsonText.c_str());
  if (rootObject == nullptr || !cJSON_IsObject(rootObject)) {
    appLogError("%s failed. cJSON_Parse error. payloadLength=%d", functionName, jsonText.length());
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* wifiObject = cJSON_GetObjectItemCaseSensitive(rootObject, wifiRootKey);
  if (wifiObject == nullptr || !cJSON_IsObject(wifiObject)) {
    appLogError("%s failed. wifi object is missing. key=%s", functionName, wifiRootKey);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* wifiSsidItem = cJSON_GetObjectItemCaseSensitive(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiSsid);
  cJSON* wifiPassItem = cJSON_GetObjectItemCaseSensitive(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiPass);
  if (!cJSON_IsString(wifiSsidItem) || !cJSON_IsString(wifiPassItem)) {
    appLogError("%s failed. wifi item type mismatch. ssidTypeOk=%d, passTypeOk=%d",
                functionName,
                cJSON_IsString(wifiSsidItem),
                cJSON_IsString(wifiPassItem));
    cJSON_Delete(rootObject);
    return false;
  }

  *wifiSsidOut = wifiSsidItem->valuestring;
  *wifiPassOut = wifiPassItem->valuestring;
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::saveMqttConfig(const String& mqttUrl,
                                          const String& mqttUser,
                                          const String& mqttPass,
                                          int32_t mqttPort,
                                          bool mqttTls) {
  constexpr const char* functionName = "sensitiveDataService::saveMqttConfig";

  if (mqttPort <= 0 || mqttPort > 65535) {
    appLogError("%s failed. invalid mqttPort=%ld", functionName, static_cast<long>(mqttPort));
    return false;
  }

  String jsonText;
  if (!readJsonText(&jsonText, functionName)) {
    return false;
  }

  cJSON* rootObject = cJSON_Parse(jsonText.c_str());
  if (rootObject == nullptr || !cJSON_IsObject(rootObject)) {
    appLogError("%s failed. cJSON_Parse error. payloadLength=%d", functionName, jsonText.length());
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* mqttObject = cJSON_GetObjectItemCaseSensitive(rootObject, mqttRootKey);
  if (mqttObject == nullptr || !cJSON_IsObject(mqttObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, mqttRootKey);
    mqttObject = cJSON_AddObjectToObject(rootObject, mqttRootKey);
    if (mqttObject == nullptr) {
      appLogError("%s failed. create mqtt object key=%s", functionName, mqttRootKey);
      cJSON_Delete(rootObject);
      return false;
    }
  }

  bool updateResult =
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrl, mqttUrl, functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUser, mqttUser, functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPass, mqttPass, functionName) &&
      setNumberItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPort, mqttPort, functionName) &&
      setBoolItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttTls, mqttTls, functionName);
  if (!updateResult) {
    cJSON_Delete(rootObject);
    return false;
  }

  char* serializedText = cJSON_PrintUnformatted(rootObject);
  if (serializedText == nullptr) {
    appLogError("%s failed. cJSON_PrintUnformatted returned null.", functionName);
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::loadMqttConfig(String* mqttUrlOut,
                                          String* mqttUserOut,
                                          String* mqttPassOut,
                                          int32_t* mqttPortOut,
                                          bool* mqttTlsOut) {
  constexpr const char* functionName = "sensitiveDataService::loadMqttConfig";
  if (mqttUrlOut == nullptr || mqttUserOut == nullptr || mqttPassOut == nullptr || mqttPortOut == nullptr || mqttTlsOut == nullptr) {
    appLogError("%s failed. output parameter is null. mqttUrlOut=%p, mqttUserOut=%p, mqttPassOut=%p, mqttPortOut=%p, mqttTlsOut=%p",
                functionName,
                mqttUrlOut,
                mqttUserOut,
                mqttPassOut,
                mqttPortOut,
                mqttTlsOut);
    return false;
  }

  String jsonText;
  if (!readJsonText(&jsonText, functionName)) {
    return false;
  }

  cJSON* rootObject = cJSON_Parse(jsonText.c_str());
  if (rootObject == nullptr || !cJSON_IsObject(rootObject)) {
    appLogError("%s failed. cJSON_Parse error. payloadLength=%d", functionName, jsonText.length());
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* mqttObject = cJSON_GetObjectItemCaseSensitive(rootObject, mqttRootKey);
  if (mqttObject == nullptr || !cJSON_IsObject(mqttObject)) {
    appLogError("%s failed. mqtt object is missing. key=%s", functionName, mqttRootKey);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* mqttUrlItem = cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrl);
  cJSON* mqttUserItem = cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUser);
  cJSON* mqttPassItem = cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPass);
  cJSON* mqttPortItem = cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPort);
  cJSON* mqttTlsItem = cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttTls);
  if (!cJSON_IsString(mqttUrlItem) || !cJSON_IsString(mqttUserItem) || !cJSON_IsString(mqttPassItem) ||
      !cJSON_IsNumber(mqttPortItem) || !cJSON_IsBool(mqttTlsItem)) {
    appLogError("%s failed. mqtt item type mismatch. url=%d user=%d pass=%d port=%d tls=%d",
                functionName,
                cJSON_IsString(mqttUrlItem),
                cJSON_IsString(mqttUserItem),
                cJSON_IsString(mqttPassItem),
                cJSON_IsNumber(mqttPortItem),
                cJSON_IsBool(mqttTlsItem));
    cJSON_Delete(rootObject);
    return false;
  }

  *mqttUrlOut = mqttUrlItem->valuestring;
  *mqttUserOut = mqttUserItem->valuestring;
  *mqttPassOut = mqttPassItem->valuestring;
  *mqttPortOut = static_cast<int32_t>(mqttPortItem->valuedouble);
  *mqttTlsOut = cJSON_IsTrue(mqttTlsItem);
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::loadTimeServerConfig(String* timeServerUrlOut,
                                                String* timeServerUserOut,
                                                String* timeServerPassOut,
                                                int32_t* timeServerPortOut,
                                                bool* timeServerTlsOut) {
  constexpr const char* functionName = "sensitiveDataService::loadTimeServerConfig";
  if (timeServerUrlOut == nullptr || timeServerUserOut == nullptr || timeServerPassOut == nullptr ||
      timeServerPortOut == nullptr || timeServerTlsOut == nullptr) {
    appLogError("%s failed. output parameter is null. timeServerUrlOut=%p, timeServerUserOut=%p, timeServerPassOut=%p, timeServerPortOut=%p, timeServerTlsOut=%p",
                functionName,
                timeServerUrlOut,
                timeServerUserOut,
                timeServerPassOut,
                timeServerPortOut,
                timeServerTlsOut);
    return false;
  }

  String jsonText;
  if (!readJsonText(&jsonText, functionName)) {
    return false;
  }

  cJSON* rootObject = cJSON_Parse(jsonText.c_str());
  if (rootObject == nullptr || !cJSON_IsObject(rootObject)) {
    appLogError("%s failed. cJSON_Parse error. payloadLength=%d", functionName, jsonText.length());
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* timeServerObject = cJSON_GetObjectItemCaseSensitive(rootObject, timeServerRootKey);
  if (timeServerObject == nullptr || !cJSON_IsObject(timeServerObject)) {
    appLogError("%s failed. timeServer object is missing. key=%s", functionName, timeServerRootKey);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* timeServerUrlItem = cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl);
  cJSON* timeServerUserItem = cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUser);
  cJSON* timeServerPassItem = cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPass);
  cJSON* timeServerPortItem = cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort);
  cJSON* timeServerTlsItem = cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls);
  if (!cJSON_IsString(timeServerUrlItem) || !cJSON_IsString(timeServerUserItem) || !cJSON_IsString(timeServerPassItem) ||
      !cJSON_IsNumber(timeServerPortItem) || !cJSON_IsBool(timeServerTlsItem)) {
    appLogError("%s failed. timeServer item type mismatch. url=%d user=%d pass=%d port=%d tls=%d",
                functionName,
                cJSON_IsString(timeServerUrlItem),
                cJSON_IsString(timeServerUserItem),
                cJSON_IsString(timeServerPassItem),
                cJSON_IsNumber(timeServerPortItem),
                cJSON_IsBool(timeServerTlsItem));
    cJSON_Delete(rootObject);
    return false;
  }

  *timeServerUrlOut = timeServerUrlItem->valuestring;
  *timeServerUserOut = timeServerUserItem->valuestring;
  *timeServerPassOut = timeServerPassItem->valuestring;
  *timeServerPortOut = static_cast<int32_t>(timeServerPortItem->valuedouble);
  *timeServerTlsOut = cJSON_IsTrue(timeServerTlsItem);
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::ensureDefaultFileExists() {
  constexpr const char* functionName = "sensitiveDataService::ensureDefaultFileExists";

  if (LittleFS.exists(sensitiveDataFilePath)) {
    return true;
  }

  cJSON* rootObject = cJSON_CreateObject();
  cJSON* wifiObject = cJSON_CreateObject();
  cJSON* mqttObject = cJSON_CreateObject();
  cJSON* serverObject = cJSON_CreateObject();
  cJSON* otaObject = cJSON_CreateObject();
  cJSON* timeServerObject = cJSON_CreateObject();
  if (rootObject == nullptr || wifiObject == nullptr || mqttObject == nullptr ||
      serverObject == nullptr || otaObject == nullptr || timeServerObject == nullptr) {
    appLogError("%s failed. create object returned null. root=%p wifi=%p mqtt=%p server=%p ota=%p timeServer=%p",
                functionName,
                rootObject,
                wifiObject,
                mqttObject,
                serverObject,
                otaObject,
                timeServerObject);
    cJSON_Delete(rootObject);
    cJSON_Delete(wifiObject);
    cJSON_Delete(mqttObject);
    cJSON_Delete(serverObject);
    cJSON_Delete(otaObject);
    cJSON_Delete(timeServerObject);
    return false;
  }

  cJSON_AddItemToObject(rootObject, wifiRootKey, wifiObject);
  cJSON_AddItemToObject(rootObject, mqttRootKey, mqttObject);
  cJSON_AddItemToObject(rootObject, serverRootKey, serverObject);
  cJSON_AddItemToObject(rootObject, otaRootKey, otaObject);
  cJSON_AddItemToObject(rootObject, timeServerRootKey, timeServerObject);

  bool defaultResult =
      setStringItem(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiSsid, "", functionName) &&
      setStringItem(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiPass, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrl, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUser, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPass, "", functionName) &&
      setNumberItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPort, defaultMqttPort, functionName) &&
      setBoolItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttTls, defaultMqttTls, functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl, "", functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser, "", functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass, "", functionName) &&
      setNumberItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort, defaultServerPort, functionName) &&
      setBoolItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls, defaultServerTls, functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl, "", functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser, "", functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass, "", functionName) &&
      setNumberItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort, defaultOtaPort, functionName) &&
      setBoolItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls, defaultOtaTls, functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl, "", functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUser, "", functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPass, "", functionName) &&
      setNumberItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort, defaultTimeServerPort, functionName) &&
      setBoolItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls, defaultTimeServerTls, functionName);
  if (!defaultResult) {
    cJSON_Delete(rootObject);
    return false;
  }

  char* serializedText = cJSON_PrintUnformatted(rootObject);
  if (serializedText == nullptr) {
    appLogError("%s failed. cJSON_PrintUnformatted returned null.", functionName);
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::readJsonText(String* jsonTextOut, const char* functionName) {
  if (jsonTextOut == nullptr || functionName == nullptr) {
    appLogError("sensitiveDataService::readJsonText failed. invalid parameter. jsonTextOut=%p, functionName=%p", jsonTextOut, functionName);
    return false;
  }

  File file = LittleFS.open(sensitiveDataFilePath, "r");
  if (!file) {
    appLogError("%s failed. open read file failed. path=%s", functionName, sensitiveDataFilePath);
    return false;
  }

  *jsonTextOut = file.readString();
  file.close();

  if (jsonTextOut->length() <= 0) {
    appLogError("%s failed. file is empty. path=%s", functionName, sensitiveDataFilePath);
    return false;
  }

  return true;
}

bool sensitiveDataService::writeJsonText(const String& jsonText, const char* functionName) {
  if (functionName == nullptr) {
    appLogError("sensitiveDataService::writeJsonText failed. functionName is null.");
    return false;
  }

  File file = LittleFS.open(sensitiveDataFilePath, "w");
  if (!file) {
    appLogError("%s failed. open write file failed. path=%s", functionName, sensitiveDataFilePath);
    return false;
  }

  size_t writtenSize = file.print(jsonText);
  file.close();

  if (writtenSize != static_cast<size_t>(jsonText.length())) {
    appLogError("%s failed. write size mismatch. expected=%d actual=%u", functionName, jsonText.length(), static_cast<unsigned>(writtenSize));
    return false;
  }

  return true;
}
