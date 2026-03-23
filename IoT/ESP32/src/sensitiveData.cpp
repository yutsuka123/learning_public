/**
 * @file sensitiveData.cpp
 * @brief 機密データ（Wi-Fi/MQTT/サーバー設定）をNVSで保存・読込する実装。
 * @details
 * - [重要] 主保存先は NVS（Preferences）。
 * - [重要] 旧 `LittleFS:/sensitiveData.json` は初回初期化時にNVSへ移行する。
 * - [厳守] デフォルト値は MQTT TLS=true / MQTT Port=8883。
 * - [禁止] パスワード値をログへ直接出力しない。
 * - [将来対応] NVS格納データの暗号化・署名保護は別タスクで対応する。
 */

#include "sensitiveDataService.h"

#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <cJSON.h>

#include "common.h"
#include "log.h"

namespace {

constexpr const char* sensitiveDataFilePath = "/sensitiveData.json";
constexpr const char* sensitiveDataNvsNamespace = "sensitive";
constexpr const char* sensitiveDataNvsBlobKey = "json_blob";
constexpr const char* wifiRootKey = "wifi";
constexpr const char* mqttRootKey = "mqtt";
constexpr const char* serverRootKey = "server";
constexpr const char* otaRootKey = "ota";
constexpr const char* timeServerRootKey = "timeServer";
constexpr const char* credentialsRootKey = "credentials";
constexpr const char* currentKeyVersionKey = "currentKeyVersion";
constexpr const char* previousKeyDeviceKey = "previousKeyDevice";
constexpr const char* previousKeyVersionKey = "previousKeyVersion";
constexpr const char* previousKeyStateKey = "previousKeyState";
constexpr const char* graceActiveRuntimeMinutesKey = "graceActiveRuntimeMinutes";
constexpr const char* retainedRuntimeMinutesKey = "retainedRuntimeMinutes";
constexpr const char* legacyTimeServerUserKey = "timeServerUser";
constexpr const char* legacyTimeServerPassKey = "timeServerPass";
constexpr const char* mqttTlsCaCertKey = "mqttTlsCaCertPem";
constexpr const char* mqttTlsCertIssueNoKey = "mqttTlsCertIssueNo";
constexpr const char* mqttTlsCertSetAtKey = "mqttTlsCertSetAt";
constexpr const char* mqttTlsCertSha256Key = "mqttTlsCertSha256";
constexpr const char* mqttTlsCertActiveKey = "mqttTlsCertActive";
constexpr const char* certsDirectoryPath = "/certs";
constexpr const char* mqttTlsCertFilePath = "/certs/mqtt-ca.pem";
constexpr const char* mqttTlsCertTempFilePath = "/certs/mqtt-ca.pem.tmp";
constexpr int32_t defaultMqttPort = 8883;
constexpr bool defaultMqttTls = true;
constexpr int32_t defaultServerPort = 443;
constexpr bool defaultServerTls = true;
constexpr int32_t defaultOtaPort = 443;
constexpr bool defaultOtaTls = true;
constexpr int32_t defaultTimeServerPort = 123;
constexpr bool defaultTimeServerTls = false;

/**
 * @brief 機密設定用NVS名前空間を開く。
 * @param preferencesOut 利用するPreferencesインスタンス。
 * @param readOnly 読込専用で開く場合true。
 * @param functionName 呼び出し元関数名。
 * @return 成功時true、失敗時false。
 */
bool openSensitivePreferences(Preferences* preferencesOut, bool readOnly, const char* functionName) {
  if (preferencesOut == nullptr || functionName == nullptr) {
    appLogError("openSensitivePreferences failed. invalid parameter. preferencesOut=%p functionName=%p",
                preferencesOut,
                functionName);
    return false;
  }
  if (!preferencesOut->begin(sensitiveDataNvsNamespace, readOnly)) {
    appLogError("%s failed. Preferences.begin returned false. namespace=%s readOnly=%d",
                functionName,
                sensitiveDataNvsNamespace,
                static_cast<int>(readOnly));
    return false;
  }
  return true;
}

/**
 * @brief LittleFSを必要時に初期化する。
 * @param functionName 呼び出し元関数名。
 * @return 初期化成功時true。
 */
bool ensureLittleFsReady(const char* functionName) {
  if (functionName == nullptr) {
    appLogError("ensureLittleFsReady failed. functionName is null.");
    return false;
  }
  static bool littleFsReady = false;
  if (littleFsReady) {
    return true;
  }
  if (!LittleFS.begin(false)) {
    appLogError("%s failed. LittleFS.begin(formatOnFail=false) returned false.", functionName);
    return false;
  }
  littleFsReady = true;
  return true;
}

/**
 * @brief 証明書格納用ディレクトリを作成する。
 * @param functionName 呼び出し元関数名。
 * @return ディレクトリ利用可能時true。
 */
bool ensureCertDirectoryExists(const char* functionName) {
  if (!ensureLittleFsReady(functionName)) {
    return false;
  }
  if (LittleFS.exists(certsDirectoryPath)) {
    return true;
  }
  if (!LittleFS.mkdir(certsDirectoryPath)) {
    appLogError("%s failed. LittleFS.mkdir failed. path=%s", functionName, certsDirectoryPath);
    return false;
  }
  return true;
}

/**
 * @brief PEM文字列のSHA-256を16進文字列で計算する。
 * @param textInput 入力文字列。
 * @param sha256HexOut 出力先（null不可）。
 * @param functionName 呼び出し元関数名。
 * @return 計算成功時true。
 */
bool computeSha256Hex(const String& textInput, String* sha256HexOut, const char* functionName) {
  if (sha256HexOut == nullptr || functionName == nullptr) {
    appLogError("computeSha256Hex failed. invalid parameter. sha256HexOut=%p functionName=%p",
                sha256HexOut,
                functionName);
    return false;
  }
  uint8_t hashBytes[32] = {0};
  mbedtls_sha256_context sha256Context;
  mbedtls_sha256_init(&sha256Context);
  int startResult = mbedtls_sha256_starts_ret(&sha256Context, 0);
  int updateResult = mbedtls_sha256_update_ret(&sha256Context,
                                               reinterpret_cast<const unsigned char*>(textInput.c_str()),
                                               textInput.length());
  int finishResult = mbedtls_sha256_finish_ret(&sha256Context, hashBytes);
  mbedtls_sha256_free(&sha256Context);
  if (startResult != 0 || updateResult != 0 || finishResult != 0) {
    appLogError("%s failed. sha256 calculation error. start=%d update=%d finish=%d inputLength=%d",
                functionName,
                startResult,
                updateResult,
                finishResult,
                textInput.length());
    return false;
  }

  char hashText[65] = {0};
  for (size_t index = 0; index < 32; ++index) {
    snprintf(&hashText[index * 2], 3, "%02x", hashBytes[index]);
  }
  hashText[64] = '\0';
  *sha256HexOut = String(hashText);
  return true;
}

/**
 * @brief 証明書文字列を tmp->rename でLittleFSへ安全保存する。
 * @param pemText 保存対象PEM文字列。
 * @param functionName 呼び出し元関数名。
 * @return 保存成功時true。
 */
bool writeMqttCertFileAtomically(const String& pemText, const char* functionName) {
  if (functionName == nullptr) {
    appLogError("writeMqttCertFileAtomically failed. functionName is null.");
    return false;
  }
  if (!ensureCertDirectoryExists(functionName)) {
    return false;
  }
  File tempFile = LittleFS.open(mqttTlsCertTempFilePath, "w");
  if (!tempFile) {
    appLogError("%s failed. temp file open failed. path=%s", functionName, mqttTlsCertTempFilePath);
    return false;
  }
  size_t writtenSize = tempFile.print(pemText);
  tempFile.close();
  if (writtenSize != static_cast<size_t>(pemText.length())) {
    appLogError("%s failed. temp write size mismatch. expected=%d actual=%u path=%s",
                functionName,
                pemText.length(),
                static_cast<unsigned>(writtenSize),
                mqttTlsCertTempFilePath);
    LittleFS.remove(mqttTlsCertTempFilePath);
    return false;
  }
  if (LittleFS.exists(mqttTlsCertFilePath)) {
    LittleFS.remove(mqttTlsCertFilePath);
  }
  if (!LittleFS.rename(mqttTlsCertTempFilePath, mqttTlsCertFilePath)) {
    appLogError("%s failed. rename temp->active failed. temp=%s active=%s",
                functionName,
                mqttTlsCertTempFilePath,
                mqttTlsCertFilePath);
    LittleFS.remove(mqttTlsCertTempFilePath);
    return false;
  }
  return true;
}

/**
 * @brief 証明書ファイルを読み込む。
 * @param pemTextOut 出力先（null不可）。
 * @param functionName 呼び出し元関数名。
 * @return 読込成功時true。
 */
bool readMqttCertFile(String* pemTextOut, const char* functionName) {
  if (pemTextOut == nullptr || functionName == nullptr) {
    appLogError("readMqttCertFile failed. invalid parameter. pemTextOut=%p functionName=%p",
                pemTextOut,
                functionName);
    return false;
  }
  if (!ensureLittleFsReady(functionName)) {
    return false;
  }
  if (!LittleFS.exists(mqttTlsCertFilePath)) {
    appLogError("%s failed. cert file does not exist. path=%s", functionName, mqttTlsCertFilePath);
    return false;
  }
  File certFile = LittleFS.open(mqttTlsCertFilePath, "r");
  if (!certFile) {
    appLogError("%s failed. cert file open failed. path=%s", functionName, mqttTlsCertFilePath);
    return false;
  }
  *pemTextOut = certFile.readString();
  certFile.close();
  if (pemTextOut->length() <= 0) {
    appLogError("%s failed. cert file is empty. path=%s", functionName, mqttTlsCertFilePath);
    return false;
  }
  return true;
}

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

  cJSON* mqttObject = cJSON_GetObjectItemCaseSensitive(rootObject, mqttRootKey);
  if (mqttObject == nullptr || !cJSON_IsObject(mqttObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, mqttRootKey);
    mqttObject = cJSON_AddObjectToObject(rootObject, mqttRootKey);
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
  cJSON* credentialsObject = cJSON_GetObjectItemCaseSensitive(rootObject, credentialsRootKey);
  if (credentialsObject == nullptr || !cJSON_IsObject(credentialsObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, credentialsRootKey);
    credentialsObject = cJSON_AddObjectToObject(rootObject, credentialsRootKey);
  }
  if (mqttObject == nullptr || serverObject == nullptr || otaObject == nullptr || timeServerObject == nullptr || credentialsObject == nullptr) {
    appLogError("%s failed. create missing object failed. mqtt=%p server=%p ota=%p timeServer=%p credentials=%p",
                functionName,
                mqttObject,
                serverObject,
                otaObject,
                timeServerObject,
                credentialsObject);
    cJSON_Delete(rootObject);
    return false;
  }
  // [旧仕様] timeServerUser/timeServerPass は廃止済みのため自動除去する。
  cJSON_DeleteItemFromObjectCaseSensitive(timeServerObject, legacyTimeServerUserKey);
  cJSON_DeleteItemFromObjectCaseSensitive(timeServerObject, legacyTimeServerPassKey);

  // [旧仕様] 旧NVS/JSONに残っている証明書本文は LittleFS(`/certs`) へ移行し、NVSにはメタ情報のみ残す。
  String migratedMqttTlsCertSha256 = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertSha256Key)) == nullptr
                                         ? ""
                                         : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertSha256Key)));
  bool migratedMqttTlsCertActive = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertActiveKey));
  String legacyMqttTlsCaCert = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCaCertKey)) == nullptr
                                   ? ""
                                   : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCaCertKey)));
  if (legacyMqttTlsCaCert.length() > 0) {
    if (!writeMqttCertFileAtomically(legacyMqttTlsCaCert, functionName)) {
      appLogError("%s failed. migrate legacy mqtt cert body to /certs returned false.", functionName);
      cJSON_Delete(rootObject);
      return false;
    }
    if (!computeSha256Hex(legacyMqttTlsCaCert, &migratedMqttTlsCertSha256, functionName)) {
      appLogError("%s failed. computeSha256Hex for migrated mqtt cert returned false.", functionName);
      cJSON_Delete(rootObject);
      return false;
    }
    migratedMqttTlsCertActive = true;
  }

  bool migrationResult =
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrl, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrl)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrl))), functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrlName, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrlName)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrlName))), functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUser, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUser)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUser))), functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPass, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPass)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPass))), functionName) &&
      setNumberItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPort, cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPort)) ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPort)->valuedouble) : defaultMqttPort, functionName) &&
      setBoolItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttTls, cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttTls)) ? cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttTls)) : defaultMqttTls, functionName) &&
      setStringItem(mqttObject, mqttTlsCaCertKey, "", functionName) &&
      setStringItem(mqttObject, mqttTlsCertIssueNoKey, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertIssueNoKey)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertIssueNoKey))), functionName) &&
      setStringItem(mqttObject, mqttTlsCertSetAtKey, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertSetAtKey)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertSetAtKey))), functionName) &&
      setStringItem(mqttObject, mqttTlsCertSha256Key, migratedMqttTlsCertSha256, functionName) &&
      setBoolItem(mqttObject, mqttTlsCertActiveKey, migratedMqttTlsCertActive, functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl))), functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrlName, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrlName)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrlName))), functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser))), functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass))), functionName) &&
      setNumberItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort, cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort)) ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort)->valuedouble) : defaultServerPort, functionName) &&
      setBoolItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls, cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls)) ? cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls)) : defaultServerTls, functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl))), functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrlName, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrlName)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrlName))), functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser))), functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass))), functionName) &&
      setNumberItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort, cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort)) ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort)->valuedouble) : defaultOtaPort, functionName) &&
      setBoolItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls, cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls)) ? cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls)) : defaultOtaTls, functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl))), functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrlName, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrlName)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrlName))), functionName) &&
      setNumberItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort, cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort)) ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort)->valuedouble) : defaultTimeServerPort, functionName) &&
      setBoolItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls, cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls)) ? cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls)) : defaultTimeServerTls, functionName) &&
      setStringItem(credentialsObject, iotCommon::mqtt::jsonKey::network::kKeyDevice, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(credentialsObject, iotCommon::mqtt::jsonKey::network::kKeyDevice)) == nullptr ? "" : String(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(credentialsObject, iotCommon::mqtt::jsonKey::network::kKeyDevice))), functionName);
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
                                          const String& mqttUrlName,
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
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrlName, mqttUrlName, functionName) &&
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
                                          String* mqttUrlNameOut,
                                          String* mqttUserOut,
                                          String* mqttPassOut,
                                          int32_t* mqttPortOut,
                                          bool* mqttTlsOut) {
  constexpr const char* functionName = "sensitiveDataService::loadMqttConfig";
  if (mqttUrlOut == nullptr || mqttUrlNameOut == nullptr || mqttUserOut == nullptr || mqttPassOut == nullptr || mqttPortOut == nullptr || mqttTlsOut == nullptr) {
    appLogError("%s failed. output parameter is null. mqttUrlOut=%p, mqttUrlNameOut=%p, mqttUserOut=%p, mqttPassOut=%p, mqttPortOut=%p, mqttTlsOut=%p",
                functionName,
                mqttUrlOut,
                mqttUrlNameOut,
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
  cJSON* mqttUrlNameItem = cJSON_GetObjectItemCaseSensitive(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrlName);
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
  *mqttUrlNameOut = cJSON_IsString(mqttUrlNameItem) ? String(mqttUrlNameItem->valuestring) : String("");
  *mqttUserOut = mqttUserItem->valuestring;
  *mqttPassOut = mqttPassItem->valuestring;
  *mqttPortOut = static_cast<int32_t>(mqttPortItem->valuedouble);
  *mqttTlsOut = cJSON_IsTrue(mqttTlsItem);
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::saveMqttTlsCertificate(const String& mqttTlsCaCertPem,
                                                  const String& certIssueNo,
                                                  const String& certSetAt) {
  constexpr const char* functionName = "sensitiveDataService::saveMqttTlsCertificate";
  String certSha256Hex;
  bool certActive = false;
  if (mqttTlsCaCertPem.length() > 0) {
    if (!writeMqttCertFileAtomically(mqttTlsCaCertPem, functionName)) {
      appLogError("%s failed. writeMqttCertFileAtomically returned false.", functionName);
      return false;
    }
    if (!computeSha256Hex(mqttTlsCaCertPem, &certSha256Hex, functionName)) {
      appLogError("%s failed. computeSha256Hex returned false.", functionName);
      return false;
    }
    certActive = true;
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
  if (mqttTlsCaCertPem.length() == 0) {
    cJSON* existingSha256Item = cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertSha256Key);
    cJSON* existingActiveItem = cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertActiveKey);
    certSha256Hex = cJSON_IsString(existingSha256Item) ? String(existingSha256Item->valuestring) : String("");
    certActive = cJSON_IsTrue(existingActiveItem);
  }
  const bool updateResult =
      setStringItem(mqttObject, mqttTlsCaCertKey, "", functionName) &&
      setStringItem(mqttObject, mqttTlsCertIssueNoKey, certIssueNo, functionName) &&
      setStringItem(mqttObject, mqttTlsCertSetAtKey, certSetAt, functionName) &&
      setStringItem(mqttObject, mqttTlsCertSha256Key, certSha256Hex, functionName) &&
      setBoolItem(mqttObject, mqttTlsCertActiveKey, certActive, functionName);
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
  const bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::loadMqttTlsCertificate(String* mqttTlsCaCertPemOut,
                                                  String* certIssueNoOut,
                                                  String* certSetAtOut) {
  constexpr const char* functionName = "sensitiveDataService::loadMqttTlsCertificate";
  if (mqttTlsCaCertPemOut == nullptr || certIssueNoOut == nullptr || certSetAtOut == nullptr) {
    appLogError("%s failed. output parameter is null.", functionName);
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
  cJSON* certItem = cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCaCertKey);
  cJSON* certSha256Item = cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertSha256Key);
  cJSON* certActiveItem = cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertActiveKey);
  cJSON* issueNoItem = cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertIssueNoKey);
  cJSON* setAtItem = cJSON_GetObjectItemCaseSensitive(mqttObject, mqttTlsCertSetAtKey);
  String certBodyText = cJSON_IsString(certItem) ? String(certItem->valuestring) : String("");
  if (certBodyText.length() == 0 && cJSON_IsTrue(certActiveItem)) {
    if (!readMqttCertFile(&certBodyText, functionName)) {
      appLogError("%s failed. certificate is active but file read failed. certPath=%s",
                  functionName,
                  mqttTlsCertFilePath);
      cJSON_Delete(rootObject);
      return false;
    }
    if (cJSON_IsString(certSha256Item)) {
      String actualSha256Hex;
      if (!computeSha256Hex(certBodyText, &actualSha256Hex, functionName)) {
        appLogError("%s failed. computeSha256Hex for loaded cert returned false.", functionName);
        cJSON_Delete(rootObject);
        return false;
      }
      if (!actualSha256Hex.equalsIgnoreCase(String(certSha256Item->valuestring))) {
        // [重要] 運用中に `/certs/mqtt-ca.pem` を更新した直後でも接続不能に陥らないよう、
        // SHA不一致は警告扱いにして証明書本体の読み込みを継続する。
        appLogWarn("%s warning. cert sha256 mismatch. expected=%s actual=%s (continue with cert body)",
                   functionName,
                   certSha256Item->valuestring,
                   actualSha256Hex.c_str());
      }
    }
  }
  *mqttTlsCaCertPemOut = certBodyText;
  *certIssueNoOut = cJSON_IsString(issueNoItem) ? String(issueNoItem->valuestring) : String("");
  *certSetAtOut = cJSON_IsString(setAtItem) ? String(setAtItem->valuestring) : String("");
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::syncMqttTlsCertificateMetadataFromLittleFs(const String& certIssueNo,
                                                                      const String& certSetAt) {
  constexpr const char* functionName = "sensitiveDataService::syncMqttTlsCertificateMetadataFromLittleFs";
  String certSha256Hex = "";
  bool certActive = false;
  if (LittleFS.exists(mqttTlsCertFilePath)) {
    String certBodyText;
    if (!readMqttCertFile(&certBodyText, functionName)) {
      appLogError("%s failed. readMqttCertFile returned false. certPath=%s",
                  functionName,
                  mqttTlsCertFilePath);
      return false;
    }
    if (!computeSha256Hex(certBodyText, &certSha256Hex, functionName)) {
      appLogError("%s failed. computeSha256Hex returned false. certPath=%s",
                  functionName,
                  mqttTlsCertFilePath);
      return false;
    }
    certActive = true;
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

  const bool updateResult =
      setStringItem(mqttObject, mqttTlsCaCertKey, "", functionName) &&
      setStringItem(mqttObject, mqttTlsCertIssueNoKey, certIssueNo, functionName) &&
      setStringItem(mqttObject, mqttTlsCertSetAtKey, certSetAt, functionName) &&
      setStringItem(mqttObject, mqttTlsCertSha256Key, certSha256Hex, functionName) &&
      setBoolItem(mqttObject, mqttTlsCertActiveKey, certActive, functionName);
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
  const bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::saveServerConfig(const String& serverUrl,
                                            const String& serverUrlName,
                                            const String& serverUser,
                                            const String& serverPass,
                                            int32_t serverPort,
                                            bool serverTls) {
  constexpr const char* functionName = "sensitiveDataService::saveServerConfig";
  if (serverPort <= 0 || serverPort > 65535) {
    appLogError("%s failed. invalid serverPort=%ld", functionName, static_cast<long>(serverPort));
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
  cJSON* serverObject = cJSON_GetObjectItemCaseSensitive(rootObject, serverRootKey);
  if (serverObject == nullptr || !cJSON_IsObject(serverObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, serverRootKey);
    serverObject = cJSON_AddObjectToObject(rootObject, serverRootKey);
    if (serverObject == nullptr) {
      appLogError("%s failed. create server object key=%s", functionName, serverRootKey);
      cJSON_Delete(rootObject);
      return false;
    }
  }
  const bool updateResult =
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl, serverUrl, functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrlName, serverUrlName, functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser, serverUser, functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass, serverPass, functionName) &&
      setNumberItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort, serverPort, functionName) &&
      setBoolItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls, serverTls, functionName);
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
  const bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::loadServerConfig(String* serverUrlOut,
                                            String* serverUrlNameOut,
                                            String* serverUserOut,
                                            String* serverPassOut,
                                            int32_t* serverPortOut,
                                            bool* serverTlsOut) {
  constexpr const char* functionName = "sensitiveDataService::loadServerConfig";
  if (serverUrlOut == nullptr || serverUrlNameOut == nullptr || serverUserOut == nullptr || serverPassOut == nullptr ||
      serverPortOut == nullptr || serverTlsOut == nullptr) {
    appLogError("%s failed. output parameter is null.", functionName);
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
  cJSON* serverObject = cJSON_GetObjectItemCaseSensitive(rootObject, serverRootKey);
  if (serverObject == nullptr || !cJSON_IsObject(serverObject)) {
    appLogError("%s failed. server object is missing. key=%s", functionName, serverRootKey);
    cJSON_Delete(rootObject);
    return false;
  }
  cJSON* serverUrlItem = cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl);
  cJSON* serverUrlNameItem = cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrlName);
  cJSON* serverUserItem = cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser);
  cJSON* serverPassItem = cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass);
  cJSON* serverPortItem = cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort);
  cJSON* serverTlsItem = cJSON_GetObjectItemCaseSensitive(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls);
  if (!cJSON_IsString(serverUrlItem) || !cJSON_IsString(serverUserItem) || !cJSON_IsString(serverPassItem) ||
      !cJSON_IsNumber(serverPortItem) || !cJSON_IsBool(serverTlsItem)) {
    appLogError("%s failed. server item type mismatch.", functionName);
    cJSON_Delete(rootObject);
    return false;
  }
  *serverUrlOut = serverUrlItem->valuestring;
  *serverUrlNameOut = cJSON_IsString(serverUrlNameItem) ? String(serverUrlNameItem->valuestring) : String("");
  *serverUserOut = serverUserItem->valuestring;
  *serverPassOut = serverPassItem->valuestring;
  *serverPortOut = static_cast<int32_t>(serverPortItem->valuedouble);
  *serverTlsOut = cJSON_IsTrue(serverTlsItem);
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::saveOtaConfig(const String& otaUrl,
                                         const String& otaUrlName,
                                         const String& otaUser,
                                         const String& otaPass,
                                         int32_t otaPort,
                                         bool otaTls) {
  constexpr const char* functionName = "sensitiveDataService::saveOtaConfig";
  if (otaPort <= 0 || otaPort > 65535) {
    appLogError("%s failed. invalid otaPort=%ld", functionName, static_cast<long>(otaPort));
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
  cJSON* otaObject = cJSON_GetObjectItemCaseSensitive(rootObject, otaRootKey);
  if (otaObject == nullptr || !cJSON_IsObject(otaObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, otaRootKey);
    otaObject = cJSON_AddObjectToObject(rootObject, otaRootKey);
    if (otaObject == nullptr) {
      appLogError("%s failed. create ota object key=%s", functionName, otaRootKey);
      cJSON_Delete(rootObject);
      return false;
    }
  }
  const bool updateResult =
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl, otaUrl, functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrlName, otaUrlName, functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser, otaUser, functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass, otaPass, functionName) &&
      setNumberItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort, otaPort, functionName) &&
      setBoolItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls, otaTls, functionName);
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
  const bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::loadOtaConfig(String* otaUrlOut,
                                         String* otaUrlNameOut,
                                         String* otaUserOut,
                                         String* otaPassOut,
                                         int32_t* otaPortOut,
                                         bool* otaTlsOut) {
  constexpr const char* functionName = "sensitiveDataService::loadOtaConfig";
  if (otaUrlOut == nullptr || otaUrlNameOut == nullptr || otaUserOut == nullptr || otaPassOut == nullptr ||
      otaPortOut == nullptr || otaTlsOut == nullptr) {
    appLogError("%s failed. output parameter is null.", functionName);
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
  cJSON* otaObject = cJSON_GetObjectItemCaseSensitive(rootObject, otaRootKey);
  if (otaObject == nullptr || !cJSON_IsObject(otaObject)) {
    appLogError("%s failed. ota object is missing. key=%s", functionName, otaRootKey);
    cJSON_Delete(rootObject);
    return false;
  }
  cJSON* otaUrlItem = cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl);
  cJSON* otaUrlNameItem = cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrlName);
  cJSON* otaUserItem = cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser);
  cJSON* otaPassItem = cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass);
  cJSON* otaPortItem = cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort);
  cJSON* otaTlsItem = cJSON_GetObjectItemCaseSensitive(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls);
  if (!cJSON_IsString(otaUrlItem) || !cJSON_IsString(otaUserItem) || !cJSON_IsString(otaPassItem) ||
      !cJSON_IsNumber(otaPortItem) || !cJSON_IsBool(otaTlsItem)) {
    appLogError("%s failed. ota item type mismatch.", functionName);
    cJSON_Delete(rootObject);
    return false;
  }
  *otaUrlOut = otaUrlItem->valuestring;
  *otaUrlNameOut = cJSON_IsString(otaUrlNameItem) ? String(otaUrlNameItem->valuestring) : String("");
  *otaUserOut = otaUserItem->valuestring;
  *otaPassOut = otaPassItem->valuestring;
  *otaPortOut = static_cast<int32_t>(otaPortItem->valuedouble);
  *otaTlsOut = cJSON_IsTrue(otaTlsItem);
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::saveTimeServerConfig(const String& timeServerUrl,
                                                const String& timeServerUrlName,
                                                int32_t timeServerPort,
                                                bool timeServerTls) {
  constexpr const char* functionName = "sensitiveDataService::saveTimeServerConfig";
  if (timeServerPort <= 0 || timeServerPort > 65535) {
    appLogError("%s failed. invalid timeServerPort=%ld", functionName, static_cast<long>(timeServerPort));
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
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, timeServerRootKey);
    timeServerObject = cJSON_AddObjectToObject(rootObject, timeServerRootKey);
    if (timeServerObject == nullptr) {
      appLogError("%s failed. create timeServer object key=%s", functionName, timeServerRootKey);
      cJSON_Delete(rootObject);
      return false;
    }
  }
  const bool updateResult =
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl, timeServerUrl, functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrlName, timeServerUrlName, functionName) &&
      setNumberItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort, timeServerPort, functionName) &&
      setBoolItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls, timeServerTls, functionName);
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
  const bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::loadTimeServerConfig(String* timeServerUrlOut,
                                                int32_t* timeServerPortOut,
                                                bool* timeServerTlsOut) {
  String timeServerUrlNameDummy;
  return loadTimeServerConfig(timeServerUrlOut, &timeServerUrlNameDummy, timeServerPortOut, timeServerTlsOut);
}

bool sensitiveDataService::loadTimeServerConfig(String* timeServerUrlOut,
                                                String* timeServerUrlNameOut,
                                                int32_t* timeServerPortOut,
                                                bool* timeServerTlsOut) {
  constexpr const char* functionName = "sensitiveDataService::loadTimeServerConfig";
  if (timeServerUrlOut == nullptr || timeServerUrlNameOut == nullptr || timeServerPortOut == nullptr || timeServerTlsOut == nullptr) {
    appLogError("%s failed. output parameter is null.", functionName);
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
  cJSON* timeServerUrlNameItem = cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrlName);
  cJSON* timeServerPortItem = cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort);
  cJSON* timeServerTlsItem = cJSON_GetObjectItemCaseSensitive(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls);
  if (!cJSON_IsString(timeServerUrlItem) || !cJSON_IsNumber(timeServerPortItem) || !cJSON_IsBool(timeServerTlsItem)) {
    appLogError("%s failed. timeServer item type mismatch.", functionName);
    cJSON_Delete(rootObject);
    return false;
  }
  *timeServerUrlOut = timeServerUrlItem->valuestring;
  *timeServerUrlNameOut = cJSON_IsString(timeServerUrlNameItem) ? String(timeServerUrlNameItem->valuestring) : String("");
  *timeServerPortOut = static_cast<int32_t>(timeServerPortItem->valuedouble);
  *timeServerTlsOut = cJSON_IsTrue(timeServerTlsItem);
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::saveKeyDevice(const String& keyDeviceBase64) {
  constexpr const char* functionName = "sensitiveDataService::saveKeyDevice";

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

  cJSON* credentialsObject = cJSON_GetObjectItemCaseSensitive(rootObject, credentialsRootKey);
  if (credentialsObject == nullptr || !cJSON_IsObject(credentialsObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, credentialsRootKey);
    credentialsObject = cJSON_AddObjectToObject(rootObject, credentialsRootKey);
    if (credentialsObject == nullptr) {
      appLogError("%s failed. create credentials object key=%s", functionName, credentialsRootKey);
      cJSON_Delete(rootObject);
      return false;
    }
  }

  if (!setStringItem(credentialsObject, iotCommon::mqtt::jsonKey::network::kKeyDevice, keyDeviceBase64, functionName)) {
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

bool sensitiveDataService::loadKeyDevice(String* keyDeviceBase64Out) {
  constexpr const char* functionName = "sensitiveDataService::loadKeyDevice";
  if (keyDeviceBase64Out == nullptr) {
    appLogError("%s failed. keyDeviceBase64Out is null.", functionName);
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

  cJSON* credentialsObject = cJSON_GetObjectItemCaseSensitive(rootObject, credentialsRootKey);
  if (credentialsObject == nullptr || !cJSON_IsObject(credentialsObject)) {
    appLogError("%s failed. credentials object is missing. key=%s", functionName, credentialsRootKey);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* keyDeviceItem = cJSON_GetObjectItemCaseSensitive(credentialsObject, iotCommon::mqtt::jsonKey::network::kKeyDevice);
  if (!cJSON_IsString(keyDeviceItem)) {
    appLogError("%s failed. keyDevice item type mismatch. isString=%d", functionName, cJSON_IsString(keyDeviceItem));
    cJSON_Delete(rootObject);
    return false;
  }

  *keyDeviceBase64Out = keyDeviceItem->valuestring;
  cJSON_Delete(rootObject);
  return true;
}

bool sensitiveDataService::savePairingKeySlots(const String& nextKeyDeviceBase64, const String& nextKeyVersion) {
  constexpr const char* functionName = "sensitiveDataService::savePairingKeySlots";
  if (nextKeyDeviceBase64.length() == 0 || nextKeyVersion.length() == 0) {
    appLogError("%s failed. next key slot values are empty. keyDeviceLength=%d keyVersionLength=%d",
                functionName,
                nextKeyDeviceBase64.length(),
                nextKeyVersion.length());
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

  cJSON* credentialsObject = cJSON_GetObjectItemCaseSensitive(rootObject, credentialsRootKey);
  if (credentialsObject == nullptr || !cJSON_IsObject(credentialsObject)) {
    cJSON_DeleteItemFromObjectCaseSensitive(rootObject, credentialsRootKey);
    credentialsObject = cJSON_AddObjectToObject(rootObject, credentialsRootKey);
    if (credentialsObject == nullptr) {
      appLogError("%s failed. create credentials object key=%s", functionName, credentialsRootKey);
      cJSON_Delete(rootObject);
      return false;
    }
  }

  const char* currentKeyDeviceChars =
      cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(credentialsObject, iotCommon::mqtt::jsonKey::network::kKeyDevice));
  const char* currentKeyVersionChars = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(credentialsObject, currentKeyVersionKey));
  const String currentKeyDevice = currentKeyDeviceChars == nullptr ? "" : String(currentKeyDeviceChars);
  const String currentKeyVersion = currentKeyVersionChars == nullptr ? "" : String(currentKeyVersionChars);
  const bool hasCurrentKeySlot = currentKeyDevice.length() > 0;
  const String nextPreviousKeyState = hasCurrentKeySlot ? "grace" : "none";

  bool updateResult = true;
  updateResult = updateResult &&
                 setStringItem(credentialsObject, previousKeyDeviceKey, hasCurrentKeySlot ? currentKeyDevice : "", functionName) &&
                 setStringItem(credentialsObject, previousKeyVersionKey, hasCurrentKeySlot ? currentKeyVersion : "", functionName) &&
                 setStringItem(credentialsObject, previousKeyStateKey, nextPreviousKeyState, functionName) &&
                 setNumberItem(credentialsObject, graceActiveRuntimeMinutesKey, 0, functionName) &&
                 setNumberItem(credentialsObject, retainedRuntimeMinutesKey, 0, functionName) &&
                 setStringItem(credentialsObject, iotCommon::mqtt::jsonKey::network::kKeyDevice, nextKeyDeviceBase64, functionName) &&
                 setStringItem(credentialsObject, currentKeyVersionKey, nextKeyVersion, functionName);
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

  const bool writeResult = writeJsonText(String(serializedText), functionName);
  cJSON_free(serializedText);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool sensitiveDataService::ensureDefaultFileExists() {
  constexpr const char* functionName = "sensitiveDataService::ensureDefaultFileExists";

  String existingJsonText;
  if (readJsonText(&existingJsonText, functionName)) {
    return true;
  }

  // [重要] 旧仕様の LittleFS 保存が残っている場合は、NVSへ移行する。
  String legacyJsonText;
  if (readLegacyJsonText(&legacyJsonText, functionName)) {
    if (!writeJsonText(legacyJsonText, functionName)) {
      appLogError("%s failed. writeJsonText for legacy migration returned false.", functionName);
      return false;
    }
    appLogWarn("%s migrated legacy LittleFS sensitive data to NVS.", functionName);
    return true;
  }

  cJSON* rootObject = cJSON_CreateObject();
  cJSON* wifiObject = cJSON_CreateObject();
  cJSON* mqttObject = cJSON_CreateObject();
  cJSON* serverObject = cJSON_CreateObject();
  cJSON* otaObject = cJSON_CreateObject();
  cJSON* timeServerObject = cJSON_CreateObject();
  cJSON* credentialsObject = cJSON_CreateObject();
  if (rootObject == nullptr || wifiObject == nullptr || mqttObject == nullptr ||
      serverObject == nullptr || otaObject == nullptr || timeServerObject == nullptr || credentialsObject == nullptr) {
    appLogError("%s failed. create object returned null. root=%p wifi=%p mqtt=%p server=%p ota=%p timeServer=%p credentials=%p",
                functionName,
                rootObject,
                wifiObject,
                mqttObject,
                serverObject,
                otaObject,
                timeServerObject,
                credentialsObject);
    cJSON_Delete(rootObject);
    cJSON_Delete(wifiObject);
    cJSON_Delete(mqttObject);
    cJSON_Delete(serverObject);
    cJSON_Delete(otaObject);
    cJSON_Delete(timeServerObject);
    cJSON_Delete(credentialsObject);
    return false;
  }

  cJSON_AddItemToObject(rootObject, wifiRootKey, wifiObject);
  cJSON_AddItemToObject(rootObject, mqttRootKey, mqttObject);
  cJSON_AddItemToObject(rootObject, serverRootKey, serverObject);
  cJSON_AddItemToObject(rootObject, otaRootKey, otaObject);
  cJSON_AddItemToObject(rootObject, timeServerRootKey, timeServerObject);
  cJSON_AddItemToObject(rootObject, credentialsRootKey, credentialsObject);

  bool defaultResult =
      setStringItem(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiSsid, "", functionName) &&
      setStringItem(wifiObject, iotCommon::mqtt::jsonKey::network::kWifiPass, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrl, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUrlName, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttUser, "", functionName) &&
      setStringItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPass, "", functionName) &&
      setStringItem(mqttObject, mqttTlsCaCertKey, "", functionName) &&
      setStringItem(mqttObject, mqttTlsCertIssueNoKey, "", functionName) &&
      setStringItem(mqttObject, mqttTlsCertSetAtKey, "", functionName) &&
      setStringItem(mqttObject, mqttTlsCertSha256Key, "", functionName) &&
      setBoolItem(mqttObject, mqttTlsCertActiveKey, false, functionName) &&
      setNumberItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttPort, defaultMqttPort, functionName) &&
      setBoolItem(mqttObject, iotCommon::mqtt::jsonKey::network::kMqttTls, defaultMqttTls, functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrl, "", functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUrlName, "", functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerUser, "", functionName) &&
      setStringItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPass, "", functionName) &&
      setNumberItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerPort, defaultServerPort, functionName) &&
      setBoolItem(serverObject, iotCommon::mqtt::jsonKey::network::kServerTls, defaultServerTls, functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrl, "", functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUrlName, "", functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaUser, "", functionName) &&
      setStringItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPass, "", functionName) &&
      setNumberItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaPort, defaultOtaPort, functionName) &&
      setBoolItem(otaObject, iotCommon::mqtt::jsonKey::network::kOtaTls, defaultOtaTls, functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrl, "", functionName) &&
      setStringItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerUrlName, "", functionName) &&
      setNumberItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerPort, defaultTimeServerPort, functionName) &&
      setBoolItem(timeServerObject, iotCommon::mqtt::jsonKey::network::kTimeServerTls, defaultTimeServerTls, functionName) &&
      setStringItem(credentialsObject, iotCommon::mqtt::jsonKey::network::kKeyDevice, "", functionName);
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

  Preferences preferences;
  if (!openSensitivePreferences(&preferences, true, functionName)) {
    return false;
  }

  const size_t storedLength = preferences.getBytesLength(sensitiveDataNvsBlobKey);
  if (storedLength == 0U) {
    preferences.end();
    appLogError("%s failed. NVS blob is missing. key=%s", functionName, sensitiveDataNvsBlobKey);
    return false;
  }

  char* valueBuffer = static_cast<char*>(malloc(storedLength + 1U));
  if (valueBuffer == nullptr) {
    preferences.end();
    appLogError("%s failed. malloc returned null. storedLength=%u", functionName, static_cast<unsigned>(storedLength));
    return false;
  }

  const size_t loadedLength = preferences.getBytes(sensitiveDataNvsBlobKey, valueBuffer, storedLength);
  preferences.end();
  if (loadedLength != storedLength) {
    appLogError("%s failed. getBytes length mismatch. expected=%u actual=%u",
                functionName,
                static_cast<unsigned>(storedLength),
                static_cast<unsigned>(loadedLength));
    free(valueBuffer);
    return false;
  }
  valueBuffer[storedLength] = '\0';
  *jsonTextOut = String(valueBuffer);
  free(valueBuffer);

  if (jsonTextOut->length() <= 0) {
    appLogError("%s failed. NVS payload is empty. key=%s", functionName, sensitiveDataNvsBlobKey);
    return false;
  }

  return true;
}

bool sensitiveDataService::writeJsonText(const String& jsonText, const char* functionName) {
  if (functionName == nullptr) {
    appLogError("sensitiveDataService::writeJsonText failed. functionName is null.");
    return false;
  }
  if (jsonText.length() <= 0) {
    appLogError("%s failed. jsonText is empty.", functionName);
    return false;
  }

  Preferences preferences;
  if (!openSensitivePreferences(&preferences, false, functionName)) {
    return false;
  }

  const size_t writtenSize = preferences.putBytes(sensitiveDataNvsBlobKey, jsonText.c_str(), static_cast<size_t>(jsonText.length()));
  preferences.end();

  if (writtenSize != static_cast<size_t>(jsonText.length())) {
    appLogError("%s failed. putBytes length mismatch. expected=%d actual=%u",
                functionName,
                jsonText.length(),
                static_cast<unsigned>(writtenSize));
    return false;
  }

  return true;
}

bool sensitiveDataService::readLegacyJsonText(String* jsonTextOut, const char* functionName) {
  if (jsonTextOut == nullptr || functionName == nullptr) {
    appLogError("sensitiveDataService::readLegacyJsonText failed. invalid parameter. jsonTextOut=%p functionName=%p",
                jsonTextOut,
                functionName);
    return false;
  }
  if (!LittleFS.begin(false)) {
    appLogWarn("%s skipped legacy migration. LittleFS.begin(formatOnFail=false) returned false.", functionName);
    return false;
  }
  if (!LittleFS.exists(sensitiveDataFilePath)) {
    return false;
  }

  File file = LittleFS.open(sensitiveDataFilePath, "r");
  if (!file) {
    appLogError("%s failed. legacy file open failed. path=%s", functionName, sensitiveDataFilePath);
    return false;
  }
  *jsonTextOut = file.readString();
  file.close();
  if (jsonTextOut->length() <= 0) {
    appLogError("%s failed. legacy file is empty. path=%s", functionName, sensitiveDataFilePath);
    return false;
  }

  if (!LittleFS.remove(sensitiveDataFilePath)) {
    appLogWarn("%s warning. legacy file remove failed. path=%s", functionName, sensitiveDataFilePath);
  }
  return true;
}
