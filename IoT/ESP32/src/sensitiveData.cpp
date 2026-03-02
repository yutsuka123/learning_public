/**
 * @file sensitiveData.cpp
 * @brief 機密データ（Wi-Fi/MQTT設定）をJSONで保存・読込する実装。
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
constexpr int32_t defaultMqttPort = 8883;
constexpr bool defaultMqttTls = false;

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

  if (!LittleFS.begin(true)) {
    appLogError("%s failed. LittleFS.begin(formatOnFail=true) returned false.", functionName);
    return false;
  }

  if (!ensureDefaultFileExists()) {
    appLogError("%s failed. ensureDefaultFileExists returned false.", functionName);
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

bool sensitiveDataService::ensureDefaultFileExists() {
  constexpr const char* functionName = "sensitiveDataService::ensureDefaultFileExists";

  if (LittleFS.exists(sensitiveDataFilePath)) {
    return true;
  }

  cJSON* rootObject = cJSON_CreateObject();
  cJSON* wifiObject = cJSON_CreateObject();
  cJSON* mqttObject = cJSON_CreateObject();
  if (rootObject == nullptr || wifiObject == nullptr || mqttObject == nullptr) {
    appLogError("%s failed. create object returned null. root=%p wifi=%p mqtt=%p", functionName, rootObject, wifiObject, mqttObject);
    cJSON_Delete(rootObject);
    cJSON_Delete(wifiObject);
    cJSON_Delete(mqttObject);
    return false;
  }

  cJSON_AddItemToObject(rootObject, wifiRootKey, wifiObject);
  cJSON_AddItemToObject(rootObject, mqttRootKey, mqttObject);

  bool defaultResult =
      setStringItem(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiSsid, "", functionName) &&
      setStringItem(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiPass, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrl, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUser, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPass, "", functionName) &&
      setNumberItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPort, defaultMqttPort, functionName) &&
      setBoolItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttTls, defaultMqttTls, functionName);
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
