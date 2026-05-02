/**
 * @file maintenanceApServer.cpp
 * @brief APメンテナンスモード用HTTPサーバー実装。
 * @details
 * - [重要] LocalServer からのログイン、ネットワーク設定投入、再起動要求を処理する。
 * - [厳守] ログイン成功後のトークンが一致する場合のみ設定更新を許可する。
 * - [禁止] 未認証・権限不足で `k-device` 更新を許可しない。
 */

#include "../header/maintenanceApServer.h"
#include "../header/firmwareMode.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <cctype>
#include <cstring>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include "jsonService.h"
#include "log.h"
#include "sensitiveData.h"
#include "sensitiveDataService.h"
#include "version.h"

#include <vector>

namespace {

enum class maintenanceRole : uint8_t {
  kNone = 0,
  kUser = 1,
  kMaintenance = 2,
  kAdmin = 3,
  kMfg = 4,
};

struct roleCredential {
  String username;
  String password;
};

WebServer maintenanceWebServer(80);
bool isServerStarted = false;
String currentApSsid = "";
String activeToken = "";
maintenanceRole activeRole = maintenanceRole::kNone;
sensitiveDataService* sensitiveDataServiceInstance = nullptr;
bool rebootScheduled = false;
uint32_t rebootScheduledAtMs = 0;
constexpr uint32_t rebootDelayMs = 600;
constexpr const char* apAuthPreferencesNamespace = "ap-auth";
constexpr const char* apAuthPasswordKeyUser = "role-user";
constexpr const char* apAuthPasswordKeyMaint = "role-maint";
constexpr const char* apAuthPasswordKeyAdmin = "role-admin";
constexpr const char* apAuthPasswordKeyMfg = "role-mfg";
String lastPairingSessionId = "";
String lastPairingBundleId = "";
String lastPairingState = "idle";
String lastPairingTargetDeviceId = "";
String lastPairingKeyVersion = "";
String lastPairingPublicId = "";
String lastPairingNonce = "";
String lastPairingSignature = "";
String lastPairingRequestedSettingsSha256 = "";
String lastPairingAcceptedKeyAgreement = "";
String lastPairingAcceptedBundleProtection = "";
String lastPairingTransportSharedSecretFingerprint = "";
String lastPairingTransportServerPublicKeyBase64 = "";
String lastPairingSavedCurrentKeyVersion = "";
String lastPairingPreviousKeyState = "none";
String lastPairingDetail = "pairing state placeholder";
String lastPairingResult = "";
std::vector<uint8_t> lastPairingTransportSessionKeyBytes;
String lastProductionRunId = "";
String lastProductionState = "idle";
String lastProductionResult = "";
String lastProductionDetail = "production state placeholder";
String lastProductionObservedFirmwareVersion = "";
String lastProductionObservedMac = "";
uint32_t lastProductionObservedFreeHeapBytes = 0;
uint32_t lastProductionObservedStackMarginBytes = 0;

roleCredential userRoleCredential = {String(SENSITIVE_AP_ROLE_USER_USERNAME), String(SENSITIVE_AP_ROLE_USER_PASSWORD)};
roleCredential maintenanceRoleCredential = {String(SENSITIVE_AP_ROLE_MAINTENANCE_USERNAME), String(SENSITIVE_AP_ROLE_MAINTENANCE_PASSWORD)};
roleCredential adminRoleCredential = {String(SENSITIVE_AP_ROLE_ADMIN_USERNAME), String(SENSITIVE_AP_ROLE_ADMIN_PASSWORD)};
roleCredential mfgRoleCredential = {String(SENSITIVE_AP_ROLE_MFG_USERNAME), String(SENSITIVE_AP_ROLE_MFG_PASSWORD)};

const char* getPasswordStorageKeyByRole(maintenanceRole role) {
  switch (role) {
    case maintenanceRole::kUser:
      return apAuthPasswordKeyUser;
    case maintenanceRole::kMaintenance:
      return apAuthPasswordKeyMaint;
    case maintenanceRole::kAdmin:
      return apAuthPasswordKeyAdmin;
    case maintenanceRole::kMfg:
      return apAuthPasswordKeyMfg;
    default:
      return nullptr;
  }
}

roleCredential* getCredentialByRole(maintenanceRole role) {
  switch (role) {
    case maintenanceRole::kUser:
      return &userRoleCredential;
    case maintenanceRole::kMaintenance:
      return &maintenanceRoleCredential;
    case maintenanceRole::kAdmin:
      return &adminRoleCredential;
    case maintenanceRole::kMfg:
      return &mfgRoleCredential;
    default:
      return nullptr;
  }
}

maintenanceRole parseRoleText(const String& roleTextRaw) {
  String roleText = roleTextRaw;
  roleText.trim();
  roleText.toLowerCase();
  if (roleText == "user") {
    return maintenanceRole::kUser;
  }
  if (roleText == "maintenance") {
    return maintenanceRole::kMaintenance;
  }
  if (roleText == "admin") {
    return maintenanceRole::kAdmin;
  }
  if (roleText == "mfg") {
    return maintenanceRole::kMfg;
  }
  return maintenanceRole::kNone;
}

bool saveRolePasswordToPreferences(maintenanceRole role, const String& nextPassword) {
  const char* keyText = getPasswordStorageKeyByRole(role);
  if (keyText == nullptr) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(apAuthPreferencesNamespace, false)) {
    appLogError("saveRolePasswordToPreferences failed. preferences.begin failed. role=%ld",
                static_cast<long>(static_cast<uint8_t>(role)));
    return false;
  }
  const bool writeResult = preferences.putString(keyText, nextPassword) > 0;
  preferences.end();
  return writeResult;
}

bool loadRolePasswordsFromPreferences() {
  Preferences preferences;
  if (!preferences.begin(apAuthPreferencesNamespace, true)) {
    appLogWarn("loadRolePasswordsFromPreferences skipped. preferences.begin failed.");
    return false;
  }
  const String loadedUserPassword = preferences.getString(apAuthPasswordKeyUser, "");
  const String loadedMaintenancePassword = preferences.getString(apAuthPasswordKeyMaint, "");
  const String loadedAdminPassword = preferences.getString(apAuthPasswordKeyAdmin, "");
  const String loadedMfgPassword = preferences.getString(apAuthPasswordKeyMfg, "");
  preferences.end();
  if (loadedUserPassword.length() > 0) {
    userRoleCredential.password = loadedUserPassword;
  }
  if (loadedMaintenancePassword.length() > 0) {
    maintenanceRoleCredential.password = loadedMaintenancePassword;
  }
  if (loadedAdminPassword.length() > 0) {
    adminRoleCredential.password = loadedAdminPassword;
  }
  if (loadedMfgPassword.length() > 0) {
    mfgRoleCredential.password = loadedMfgPassword;
  }
  appLogInfo("loadRolePasswordsFromPreferences summary. userLoaded=%d maintLoaded=%d adminLoaded=%d mfgLoaded=%d",
             static_cast<int>(loadedUserPassword.length() > 0),
             static_cast<int>(loadedMaintenancePassword.length() > 0),
             static_cast<int>(loadedAdminPassword.length() > 0),
             static_cast<int>(loadedMfgPassword.length() > 0));
  return true;
}

maintenanceRole resolveRoleByCredentials(const String& username, const String& password) {
  if (username == mfgRoleCredential.username && password == mfgRoleCredential.password) {
    return maintenanceRole::kMfg;
  }
  if (username == adminRoleCredential.username && password == adminRoleCredential.password) {
    return maintenanceRole::kAdmin;
  }
  if (username == maintenanceRoleCredential.username && password == maintenanceRoleCredential.password) {
    return maintenanceRole::kMaintenance;
  }
  if (username == userRoleCredential.username && password == userRoleCredential.password) {
    return maintenanceRole::kUser;
  }
  return maintenanceRole::kNone;
}

String toRoleText(maintenanceRole role) {
  switch (role) {
    case maintenanceRole::kUser:
      return "user";
    case maintenanceRole::kMaintenance:
      return "maintenance";
    case maintenanceRole::kAdmin:
      return "admin";
    case maintenanceRole::kMfg:
      return "mfg";
    default:
      return "none";
  }
}

const char* toResetReasonText(esp_reset_reason_t resetReason) {
  switch (resetReason) {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "other_wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

void logMaintenanceApRuntimeSnapshot(const char* reasonText, const char* remoteIpText = nullptr) {
  const esp_reset_reason_t resetReason = esp_reset_reason();
  const esp_partition_t* runningPartition = esp_ota_get_running_partition();
  const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
  const String runningPartitionLabel =
      (runningPartition != nullptr && runningPartition->label != nullptr) ? String(runningPartition->label) : String("(null)");
  const String bootPartitionLabel =
      (bootPartition != nullptr && bootPartition->label != nullptr) ? String(bootPartition->label) : String("(null)");
  const String activeRoleText = toRoleText(activeRole);
  const char* safeReasonText = reasonText != nullptr ? reasonText : "(none)";
  const char* safeRemoteIpText = (remoteIpText != nullptr && remoteIpText[0] != '\0') ? remoteIpText : "(none)";
  appLogWarn(
      "maintenanceApServer snapshot. reason=%s remoteIp=%s uptimeMs=%lu resetReason=%d(%s) apSsid=%s serverStarted=%d "
      "activeRole=%s activeTokenSet=%d rebootScheduled=%d runningPartition=%s bootPartition=%s "
      "lastPairingState=%s lastPairingResult=%s lastPairingTargetDeviceId=%s lastProductionRunId=%s lastProductionState=%s "
      "lastProductionResult=%s lastProductionDetail=%s lastProductionObservedFirmwareVersion=%s lastProductionObservedMac=%s "
      "lastProductionObservedFreeHeapBytes=%lu lastProductionObservedStackMarginBytes=%lu",
      safeReasonText,
      safeRemoteIpText,
      static_cast<unsigned long>(millis()),
      static_cast<int>(resetReason),
      toResetReasonText(resetReason),
      currentApSsid.c_str(),
      static_cast<int>(isServerStarted),
      activeRoleText.c_str(),
      static_cast<int>(activeToken.length() > 0),
      static_cast<int>(rebootScheduled),
      runningPartitionLabel.c_str(),
      bootPartitionLabel.c_str(),
      lastPairingState.c_str(),
      lastPairingResult.c_str(),
      lastPairingTargetDeviceId.c_str(),
      lastProductionRunId.c_str(),
      lastProductionState.c_str(),
      lastProductionResult.c_str(),
      lastProductionDetail.c_str(),
      lastProductionObservedFirmwareVersion.c_str(),
      lastProductionObservedMac.c_str(),
      static_cast<unsigned long>(lastProductionObservedFreeHeapBytes),
      static_cast<unsigned long>(lastProductionObservedStackMarginBytes));
}

/**
 * @brief 現在APモードで扱っている対象デバイスIDを返す。
 * @details
 * - [重要] 現段階では AP SSID 末尾の compactMac を用いて `IoT_<compactMac>` を返す。
 * - [将来対応] public_id や永続メタ情報と統合できる段階で、正式な targetDeviceId 解決へ差し替える。
 * @return 対象デバイスID。解決できない場合は `pending`。
 */
String resolvePairingTargetDeviceId() {
  const int separatorIndex = currentApSsid.lastIndexOf('-');
  if (separatorIndex < 0 || separatorIndex + 1 >= currentApSsid.length()) {
    return "pending";
  }
  const String compactMacText = currentApSsid.substring(separatorIndex + 1);
  if (compactMacText.length() == 0) {
    return "pending";
  }
  return String("IoT_") + compactMacText;
}

String resolveCompactMacFromApSsid() {
  const int separatorIndex = currentApSsid.lastIndexOf('-');
  if (separatorIndex < 0 || separatorIndex + 1 >= currentApSsid.length()) {
    return "";
  }
  String compactMacText = currentApSsid.substring(separatorIndex + 1);
  compactMacText.trim();
  compactMacText.toUpperCase();
  return compactMacText;
}

String formatCompactMacAsColonSeparated(const String& compactMacText) {
  if (compactMacText.length() != 12) {
    return compactMacText;
  }
  String formattedMacText;
  formattedMacText.reserve(17);
  for (size_t index = 0; index < compactMacText.length(); ++index) {
    if (index > 0 && (index % 2) == 0) {
      formattedMacText += ':';
    }
    formattedMacText += static_cast<char>(toupper(compactMacText[index]));
  }
  return formattedMacText;
}

String normalizeMacTextForComparison(const String& rawMacText) {
  String normalizedText;
  normalizedText.reserve(rawMacText.length());
  for (size_t index = 0; index < rawMacText.length(); ++index) {
    const char currentChar = rawMacText[index];
    if ((currentChar >= '0' && currentChar <= '9') || (currentChar >= 'a' && currentChar <= 'f') ||
        (currentChar >= 'A' && currentChar <= 'F')) {
      normalizedText += static_cast<char>(toupper(currentChar));
    }
  }
  return normalizedText;
}

String toJsonSafeText(const String& value) {
  String escapedValue;
  escapedValue.reserve(value.length() + 8);
  for (size_t index = 0; index < value.length(); ++index) {
    const char currentChar = value[index];
    if (currentChar == '\\' || currentChar == '"') {
      escapedValue += '\\';
      escapedValue += currentChar;
      continue;
    }
    escapedValue += currentChar;
  }
  return escapedValue;
}

bool parseBodyStringValue(const String& bodyText, const char* pathText, String* valueOut) {
  if (valueOut == nullptr) {
    return false;
  }
  jsonService parser;
  return parser.getValueByPath(bodyText, pathText, valueOut);
}

bool parseBodyLongValue(const String& bodyText, const char* pathText, long* valueOut) {
  if (valueOut == nullptr) {
    return false;
  }
  jsonService parser;
  return parser.getValueByPath(bodyText, pathText, valueOut);
}

bool parseBodyBoolValue(const String& bodyText, const char* pathText, bool* valueOut) {
  if (valueOut == nullptr) {
    return false;
  }
  jsonService parser;
  return parser.getValueByPath(bodyText, pathText, valueOut);
}

/**
 * @brief APモード局所更新API向けに管理領域ルートを解決する。
 * @param targetArea `images` または `certs`。
 * @param areaRootOut 解決先。
 * @return 解決成功時true。
 */
bool resolveManagedAreaRoot(const String& targetArea, String* areaRootOut) {
  if (areaRootOut == nullptr) {
    return false;
  }
  if (targetArea.equalsIgnoreCase("images")) {
    *areaRootOut = "/images";
    return true;
  }
  if (targetArea.equalsIgnoreCase("certs")) {
    *areaRootOut = "/certs";
    return true;
  }
  return false;
}

/**
 * @brief 連続スラッシュと相対遷移を除去し、管理領域内の絶対パスへ正規化する。
 * @param targetArea 管理領域。
 * @param requestedPath 要求パス。
 * @param normalizedPathOut 正規化結果。
 * @return 正規化成功時true。
 */
bool normalizeManagedFilePathForAp(const String& targetArea, const String& requestedPath, String* normalizedPathOut) {
  if (normalizedPathOut == nullptr) {
    return false;
  }
  String areaRoot;
  if (!resolveManagedAreaRoot(targetArea, &areaRoot)) {
    return false;
  }
  String normalizedPath = requestedPath;
  normalizedPath.trim();
  if (normalizedPath.length() == 0) {
    return false;
  }
  if (!normalizedPath.startsWith("/")) {
    normalizedPath = String("/") + normalizedPath;
  }
  while (normalizedPath.indexOf("//") >= 0) {
    normalizedPath.replace("//", "/");
  }
  if (normalizedPath.indexOf("..") >= 0) {
    appLogError("normalizeManagedFilePathForAp failed. traversal detected. targetArea=%s path=%s",
                targetArea.c_str(),
                normalizedPath.c_str());
    return false;
  }
  if (!normalizedPath.startsWith(areaRoot + "/") && normalizedPath != areaRoot) {
    appLogError("normalizeManagedFilePathForAp failed. out of area. targetArea=%s path=%s areaRoot=%s",
                targetArea.c_str(),
                normalizedPath.c_str(),
                areaRoot.c_str());
    return false;
  }
  *normalizedPathOut = normalizedPath;
  return true;
}

/**
 * @brief ディレクトリを再帰的に作成する。
 * @param directoryPath 対象ディレクトリ。
 * @return 作成または既存時true。
 */
bool ensureDirectoryPathExistsForAp(const String& directoryPath) {
  if (directoryPath.length() == 0) {
    return false;
  }
  if (LittleFS.exists(directoryPath)) {
    return true;
  }
  int32_t slashIndex = directoryPath.indexOf('/', 1);
  while (slashIndex >= 0) {
    String partialPath = directoryPath.substring(0, slashIndex);
    if (partialPath.length() > 0 && !LittleFS.exists(partialPath)) {
      if (!LittleFS.mkdir(partialPath)) {
        appLogError("ensureDirectoryPathExistsForAp failed. mkdir partialPath=%s", partialPath.c_str());
        return false;
      }
    }
    slashIndex = directoryPath.indexOf('/', slashIndex + 1);
  }
  if (!LittleFS.exists(directoryPath)) {
    if (!LittleFS.mkdir(directoryPath)) {
      appLogError("ensureDirectoryPathExistsForAp failed. mkdir directoryPath=%s", directoryPath.c_str());
      return false;
    }
  }
  return true;
}

/**
 * @brief Base64文字列をバイト列へ復号する。
 * @param inputBase64 入力Base64文字列。
 * @param outputBytesOut 出力先。
 * @return 復号成功時true。
 */
bool decodeBase64TextForAp(const String& inputBase64, std::vector<uint8_t>* outputBytesOut) {
  if (outputBytesOut == nullptr) {
    return false;
  }
  outputBytesOut->clear();
  size_t outputLength = 0;
  int probeResult = mbedtls_base64_decode(nullptr,
                                          0,
                                          &outputLength,
                                          reinterpret_cast<const unsigned char*>(inputBase64.c_str()),
                                          inputBase64.length());
  if (probeResult != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && probeResult != 0) {
    appLogError("decodeBase64TextForAp failed. probeResult=%d inputLength=%ld",
                probeResult,
                static_cast<long>(inputBase64.length()));
    return false;
  }
  outputBytesOut->resize(outputLength);
  if (outputLength == 0) {
    return true;
  }
  int decodeResult = mbedtls_base64_decode(outputBytesOut->data(),
                                           outputBytesOut->size(),
                                           &outputLength,
                                           reinterpret_cast<const unsigned char*>(inputBase64.c_str()),
                                           inputBase64.length());
  if (decodeResult != 0) {
    appLogError("decodeBase64TextForAp failed. decodeResult=%d inputLength=%ld",
                decodeResult,
                static_cast<long>(inputBase64.length()));
    return false;
  }
  outputBytesOut->resize(outputLength);
  return true;
}

/**
 * @brief バイト列をBase64文字列へ変換する。
 * @param inputBytes 入力バイト列。
 * @param outputBase64Out 出力先。
 * @return 変換成功時true。
 */
bool encodeBase64TextForAp(const std::vector<uint8_t>& inputBytes, String* outputBase64Out) {
  if (outputBase64Out == nullptr) {
    return false;
  }
  size_t outputLength = 0;
  int probeResult = mbedtls_base64_encode(nullptr, 0, &outputLength, inputBytes.data(), inputBytes.size());
  if (probeResult != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && probeResult != 0) {
    appLogError("encodeBase64TextForAp failed. probeResult=%d inputSize=%ld", probeResult, static_cast<long>(inputBytes.size()));
    return false;
  }
  std::vector<uint8_t> outputBytes(outputLength + 1, 0);
  int encodeResult = mbedtls_base64_encode(outputBytes.data(),
                                           outputBytes.size(),
                                           &outputLength,
                                           inputBytes.data(),
                                           inputBytes.size());
  if (encodeResult != 0) {
    appLogError("encodeBase64TextForAp failed. encodeResult=%d inputSize=%ld", encodeResult, static_cast<long>(inputBytes.size()));
    return false;
  }
  outputBase64Out->remove(0);
  outputBase64Out->reserve(outputLength);
  for (size_t index = 0; index < outputLength; ++index) {
    *outputBase64Out += static_cast<char>(outputBytes[index]);
  }
  return true;
}

/**
 * @brief APモード局所更新API向けに LittleFS 初期化状態を保証する。
 * @return 初期化済みならtrue。
 */
bool ensureLittleFsReadyForAp() {
  static bool littleFsInitialized = false;
  if (littleFsInitialized) {
    return true;
  }
  littleFsInitialized = LittleFS.begin(false);
  if (!littleFsInitialized) {
    appLogError("ensureLittleFsReadyForAp failed. LittleFS.begin(false) failed.");
  }
  return littleFsInitialized;
}

/**
 * @brief バイト列のSHA-256（16進小文字）を計算する。
 * @param inputBytes 入力バイト列。
 * @param sha256HexOut 出力先。
 * @return 計算成功時true。
 */
bool computeSha256HexForBytesForAp(const std::vector<uint8_t>& inputBytes, String* sha256HexOut) {
  if (sha256HexOut == nullptr) {
    return false;
  }
  uint8_t hashBytes[32] = {0};
  mbedtls_sha256_context sha256Context;
  mbedtls_sha256_init(&sha256Context);
  int startResult = mbedtls_sha256_starts_ret(&sha256Context, 0);
  int updateResult = mbedtls_sha256_update_ret(&sha256Context, inputBytes.data(), inputBytes.size());
  int finishResult = mbedtls_sha256_finish_ret(&sha256Context, hashBytes);
  mbedtls_sha256_free(&sha256Context);
  if (startResult != 0 || updateResult != 0 || finishResult != 0) {
    appLogError("computeSha256HexForBytesForAp failed. start=%d update=%d finish=%d size=%ld",
                startResult,
                updateResult,
                finishResult,
                static_cast<long>(inputBytes.size()));
    return false;
  }
  char hashText[65] = {0};
  for (size_t index = 0; index < sizeof(hashBytes); ++index) {
    snprintf(hashText + (index * 2), sizeof(hashText) - (index * 2), "%02x", hashBytes[index]);
  }
  *sha256HexOut = String(hashText);
  return true;
}

/**
 * @brief バイト列のSHA-256生値を計算する。
 * @param inputBytes 入力バイト列。
 * @param sha256BytesOut 出力先。
 * @return 計算成功時true。
 */
bool computeSha256BytesForAp(const std::vector<uint8_t>& inputBytes, std::vector<uint8_t>* sha256BytesOut) {
  if (sha256BytesOut == nullptr) {
    return false;
  }
  uint8_t hashBytes[32] = {0};
  mbedtls_sha256_context sha256Context;
  mbedtls_sha256_init(&sha256Context);
  int startResult = mbedtls_sha256_starts_ret(&sha256Context, 0);
  int updateResult = 0;
  if (!inputBytes.empty()) {
    updateResult = mbedtls_sha256_update_ret(&sha256Context, inputBytes.data(), inputBytes.size());
  }
  int finishResult = mbedtls_sha256_finish_ret(&sha256Context, hashBytes);
  mbedtls_sha256_free(&sha256Context);
  if (startResult != 0 || updateResult != 0 || finishResult != 0) {
    appLogError("computeSha256BytesForAp failed. start=%d update=%d finish=%d size=%ld",
                startResult,
                updateResult,
                finishResult,
                static_cast<long>(inputBytes.size()));
    return false;
  }
  sha256BytesOut->assign(hashBytes, hashBytes + sizeof(hashBytes));
  return true;
}

/**
 * @brief P-256 ECDH の共有秘密から Pairing transport 用 32byte セッション鍵を導出する。
 * @param sharedSecretBytes ECDH 共有秘密。
 * @param sessionId セッションID。
 * @param bundleId bundle ID。
 * @param sessionKeyBytesOut 導出鍵出力先。
 * @return 導出成功時true。
 */
bool derivePairingTransportSessionKeyForAp(const std::vector<uint8_t>& sharedSecretBytes,
                                           const String& sessionId,
                                           const String& bundleId,
                                           std::vector<uint8_t>* sessionKeyBytesOut) {
  if (sessionKeyBytesOut == nullptr) {
    return false;
  }
  std::vector<uint8_t> materialBytes;
  materialBytes.reserve(sharedSecretBytes.size() + sessionId.length() + bundleId.length() + 1);
  materialBytes.insert(materialBytes.end(), sharedSecretBytes.begin(), sharedSecretBytes.end());
  for (size_t index = 0; index < sessionId.length(); ++index) {
    materialBytes.push_back(static_cast<uint8_t>(sessionId[index]));
  }
  materialBytes.push_back(static_cast<uint8_t>('|'));
  for (size_t index = 0; index < bundleId.length(); ++index) {
    materialBytes.push_back(static_cast<uint8_t>(bundleId[index]));
  }
  return computeSha256BytesForAp(materialBytes, sessionKeyBytesOut);
}

/**
 * @brief Pairing secure bundle の AAD を生成する。
 * @param targetDeviceId 対象デバイスID。
 * @param sessionId セッションID。
 * @param bundleId bundle ID。
 * @param keyVersion 鍵バージョン。
 * @param aadBytesOut AAD出力先。
 * @return 生成成功時true。
 */
bool createPairingSecureBundleAadForAp(const String& targetDeviceId,
                                       const String& sessionId,
                                       const String& bundleId,
                                       const String& keyVersion,
                                       std::vector<uint8_t>* aadBytesOut) {
  if (aadBytesOut == nullptr) {
    return false;
  }
  const String aadText = String("pairing-secure-bundle-v1|") + targetDeviceId + "|" + sessionId + "|" + bundleId + "|" + keyVersion;
  aadBytesOut->clear();
  aadBytesOut->reserve(aadText.length());
  for (size_t index = 0; index < aadText.length(); ++index) {
    aadBytesOut->push_back(static_cast<uint8_t>(aadText[index]));
  }
  return true;
}

/**
 * @brief AES-256-GCM で暗号ペイロードを復号する。
 * @param keyBytes 32byte 鍵。
 * @param ivBytes 12byte nonce。
 * @param cipherBytes 暗号文。
 * @param tagBytes 16byte GCM tag。
 * @param aadBytes AAD。
 * @param plainBytesOut 復号平文出力先。
 * @return 復号成功時true。
 */
bool decryptAes256GcmForAp(const std::vector<uint8_t>& keyBytes,
                           const std::vector<uint8_t>& ivBytes,
                           const std::vector<uint8_t>& cipherBytes,
                           const std::vector<uint8_t>& tagBytes,
                           const std::vector<uint8_t>& aadBytes,
                           std::vector<uint8_t>* plainBytesOut) {
  if (plainBytesOut == nullptr) {
    return false;
  }
  plainBytesOut->clear();
  if (keyBytes.size() != 32 || ivBytes.size() != 12 || tagBytes.size() != 16) {
    appLogError("decryptAes256GcmForAp failed. invalid size. key=%ld iv=%ld tag=%ld",
                static_cast<long>(keyBytes.size()),
                static_cast<long>(ivBytes.size()),
                static_cast<long>(tagBytes.size()));
    return false;
  }
  plainBytesOut->resize(cipherBytes.size());
  if (!cipherBytes.empty()) {
    memcpy(plainBytesOut->data(), cipherBytes.data(), cipherBytes.size());
  }
  mbedtls_gcm_context gcmContext;
  mbedtls_gcm_init(&gcmContext);
  int keyResult = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, keyBytes.data(), 256);
  if (keyResult != 0) {
    appLogError("decryptAes256GcmForAp failed. mbedtls_gcm_setkey result=%d", keyResult);
    mbedtls_gcm_free(&gcmContext);
    return false;
  }
  int decryptResult = mbedtls_gcm_auth_decrypt(&gcmContext,
                                               plainBytesOut->size(),
                                               ivBytes.data(),
                                               ivBytes.size(),
                                               aadBytes.empty() ? nullptr : aadBytes.data(),
                                               aadBytes.size(),
                                               tagBytes.data(),
                                               tagBytes.size(),
                                               cipherBytes.data(),
                                               plainBytesOut->data());
  mbedtls_gcm_free(&gcmContext);
  if (decryptResult != 0) {
    appLogError("decryptAes256GcmForAp failed. mbedtls_gcm_auth_decrypt result=%d", decryptResult);
    plainBytesOut->clear();
    return false;
  }
  return true;
}

/**
 * @brief Pairing transport の P-256 ECDH handshake を実行する。
 * @param clientPublicKeyBase64 Rust 側公開鍵。
 * @param sessionId セッションID。
 * @param bundleId bundle ID。
 * @param serverPublicKeyBase64Out ESP32 側公開鍵。
 * @param sharedSecretFingerprintOut 導出済みセッション鍵 fingerprint。
 * @return handshake 成功時true。
 */
bool performPairingTransportHandshakeForAp(const String& clientPublicKeyBase64,
                                           const String& sessionId,
                                           const String& bundleId,
                                           String* serverPublicKeyBase64Out,
                                           String* sharedSecretFingerprintOut) {
  if (serverPublicKeyBase64Out == nullptr || sharedSecretFingerprintOut == nullptr) {
    return false;
  }
  std::vector<uint8_t> clientPublicKeyBytes;
  if (!decodeBase64TextForAp(clientPublicKeyBase64, &clientPublicKeyBytes)) {
    appLogError("performPairingTransportHandshakeForAp failed. client public key decode failed.");
    return false;
  }
  if (clientPublicKeyBytes.empty()) {
    appLogError("performPairingTransportHandshakeForAp failed. client public key is empty.");
    return false;
  }

  mbedtls_ecdh_context ecdhContext;
  mbedtls_entropy_context entropyContext;
  mbedtls_ctr_drbg_context ctrDrbgContext;
  mbedtls_ecdh_init(&ecdhContext);
  mbedtls_entropy_init(&entropyContext);
  mbedtls_ctr_drbg_init(&ctrDrbgContext);
  const char* personalizationText = "maintenanceApPairingTransport";
  int seedResult = mbedtls_ctr_drbg_seed(&ctrDrbgContext,
                                         mbedtls_entropy_func,
                                         &entropyContext,
                                         reinterpret_cast<const unsigned char*>(personalizationText),
                                         strlen(personalizationText));
  if (seedResult != 0) {
    appLogError("performPairingTransportHandshakeForAp failed. ctr_drbg_seed result=%d", seedResult);
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }
  int loadGroupResult = mbedtls_ecp_group_load(&ecdhContext.grp, MBEDTLS_ECP_DP_SECP256R1);
  if (loadGroupResult != 0) {
    appLogError("performPairingTransportHandshakeForAp failed. group_load result=%d", loadGroupResult);
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }
  int generateKeyResult = mbedtls_ecdh_gen_public(&ecdhContext.grp,
                                                  &ecdhContext.d,
                                                  &ecdhContext.Q,
                                                  mbedtls_ctr_drbg_random,
                                                  &ctrDrbgContext);
  if (generateKeyResult != 0) {
    appLogError("performPairingTransportHandshakeForAp failed. gen_public result=%d", generateKeyResult);
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }
  int readPeerKeyResult =
      mbedtls_ecp_point_read_binary(&ecdhContext.grp, &ecdhContext.Qp, clientPublicKeyBytes.data(), clientPublicKeyBytes.size());
  if (readPeerKeyResult != 0) {
    appLogError("performPairingTransportHandshakeForAp failed. point_read_binary result=%d", readPeerKeyResult);
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }
  int computeSharedResult = mbedtls_ecdh_compute_shared(&ecdhContext.grp,
                                                        &ecdhContext.z,
                                                        &ecdhContext.Qp,
                                                        &ecdhContext.d,
                                                        mbedtls_ctr_drbg_random,
                                                        &ctrDrbgContext);
  if (computeSharedResult != 0) {
    appLogError("performPairingTransportHandshakeForAp failed. compute_shared result=%d", computeSharedResult);
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }

  std::vector<uint8_t> sharedSecretBytes(32, 0);
  int sharedWriteResult = mbedtls_mpi_write_binary(&ecdhContext.z, sharedSecretBytes.data(), sharedSecretBytes.size());
  if (sharedWriteResult != 0) {
    appLogError("performPairingTransportHandshakeForAp failed. shared secret write result=%d", sharedWriteResult);
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }

  size_t publicKeyLength = 0;
  std::vector<uint8_t> serverPublicKeyBytes(65, 0);
  int publicWriteResult = mbedtls_ecp_point_write_binary(&ecdhContext.grp,
                                                         &ecdhContext.Q,
                                                         MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                         &publicKeyLength,
                                                         serverPublicKeyBytes.data(),
                                                         serverPublicKeyBytes.size());
  if (publicWriteResult != 0) {
    appLogError("performPairingTransportHandshakeForAp failed. public key write result=%d", publicWriteResult);
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }
  serverPublicKeyBytes.resize(publicKeyLength);

  std::vector<uint8_t> sessionKeyBytes;
  if (!derivePairingTransportSessionKeyForAp(sharedSecretBytes, sessionId, bundleId, &sessionKeyBytes)) {
    appLogError("performPairingTransportHandshakeForAp failed. derive session key failed.");
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }
  String sessionKeyFingerprint;
  if (!computeSha256HexForBytesForAp(sessionKeyBytes, &sessionKeyFingerprint)) {
    appLogError("performPairingTransportHandshakeForAp failed. session key fingerprint failed.");
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }
  String serverPublicKeyBase64;
  if (!encodeBase64TextForAp(serverPublicKeyBytes, &serverPublicKeyBase64)) {
    appLogError("performPairingTransportHandshakeForAp failed. public key base64 encode failed.");
    mbedtls_ctr_drbg_free(&ctrDrbgContext);
    mbedtls_entropy_free(&entropyContext);
    mbedtls_ecdh_free(&ecdhContext);
    return false;
  }

  lastPairingTransportSessionKeyBytes = sessionKeyBytes;
  *serverPublicKeyBase64Out = serverPublicKeyBase64;
  *sharedSecretFingerprintOut = sessionKeyFingerprint;
  mbedtls_ctr_drbg_free(&ctrDrbgContext);
  mbedtls_entropy_free(&entropyContext);
  mbedtls_ecdh_free(&ecdhContext);
  return true;
}

/**
 * @brief ファイルへ tmp + rename で原子的に反映する。
 * @param targetPath 最終配置パス。
 * @param fileBytes 書込みバイト列。
 * @return 反映成功時true。
 */
bool writeFileAtomicallyForAp(const String& targetPath, const std::vector<uint8_t>& fileBytes) {
  int slashIndex = targetPath.lastIndexOf('/');
  String parentDirectoryPath = slashIndex > 0 ? targetPath.substring(0, slashIndex) : String("/");
  if (!ensureDirectoryPathExistsForAp(parentDirectoryPath)) {
    appLogError("writeFileAtomicallyForAp failed. ensureDirectoryPathExistsForAp failed. parent=%s path=%s",
                parentDirectoryPath.c_str(),
                targetPath.c_str());
    return false;
  }
  const String tempPath = targetPath + ".tmp";
  if (LittleFS.exists(tempPath)) {
    LittleFS.remove(tempPath);
  }
  File tempFile = LittleFS.open(tempPath, FILE_WRITE);
  if (!tempFile) {
    // [重要] ボード/FS実装差異で FILE_WRITE が失敗する場合に備え、従来指定へフォールバックする。
    tempFile = LittleFS.open(tempPath, "w");
  }
  if (!tempFile) {
    appLogError("writeFileAtomicallyForAp failed. temp open failed. tempPath=%s", tempPath.c_str());
    return false;
  }
  size_t writtenSize = 0;
  if (!fileBytes.empty()) {
    writtenSize = tempFile.write(fileBytes.data(), fileBytes.size());
  }
  tempFile.flush();
  tempFile.close();
  if (writtenSize != fileBytes.size()) {
    appLogError("writeFileAtomicallyForAp failed. write size mismatch. path=%s expected=%ld actual=%ld",
                tempPath.c_str(),
                static_cast<long>(fileBytes.size()),
                static_cast<long>(writtenSize));
    LittleFS.remove(tempPath);
    return false;
  }
  if (LittleFS.exists(targetPath)) {
    LittleFS.remove(targetPath);
  }
  if (!LittleFS.rename(tempPath, targetPath)) {
    appLogError("writeFileAtomicallyForAp failed. rename temp->target failed. temp=%s target=%s",
                tempPath.c_str(),
                targetPath.c_str());
    LittleFS.remove(tempPath);
    return false;
  }
  return true;
}

bool isAuthorized(maintenanceRole minimumRole) {
  const String tokenHeader = maintenanceWebServer.header("Authorization");
  String actualToken = tokenHeader;
  if (actualToken.startsWith("Bearer ")) {
    actualToken = actualToken.substring(7);
  }
  actualToken.trim();
  if (actualToken.length() == 0) {
    actualToken = maintenanceWebServer.header("X-AP-Token");
    actualToken.trim();
  }
  if (actualToken.length() == 0 || actualToken != activeToken) {
    return false;
  }
  return static_cast<uint8_t>(activeRole) >= static_cast<uint8_t>(minimumRole);
}

/**
 * @brief 製造系APIが現在のFWモードで許可されるかを確認する。
 * @param apiName ログ出力用API名。
 * @return 許可時true、拒否応答送信済み時false。
 * @details
 * - [厳守] 通常運用FWでは pairing / production 系APIを公開しない。
 * - [重要] 403 を返し、顧客環境で危険APIが残っていないことを明示する。
 */
bool ensureFactoryApiEnabled(const char* apiName) {
  if (firmwareMode::kFactoryApisEnabled) {
    return true;
  }
  const char* safeApiName = (apiName != nullptr) ? apiName : "(unknown)";
  appLogWarn("ensureFactoryApiEnabled rejected. api=%s firmwareOperationMode=%s serialOutputMode=%s",
             safeApiName,
             firmwareMode::kFirmwareOperationMode,
             firmwareMode::kSerialOutputMode);
  maintenanceWebServer.send(403,
                            "application/json",
                            "{\"result\":\"NG\",\"detail\":\"factory api disabled in normal firmware\"}");
  return false;
}

/**
 * @brief APモード中に `/images` または `/certs` へ1ファイルを局所更新するAPI。
 * @details
 * - [重要] 受信形式は JSON（`targetArea`/`path`/`dataBase64`/`expectedSha256`）。
 * - [厳守] `/images` と `/certs` 以外、`..` を含むパスは拒否する。
 * - [厳守] 実体反映は `tmp + rename` で行う。
 */
void handleManagedFileUpsertApi() {
  if (!isAuthorized(maintenanceRole::kMaintenance)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"unauthorized\"}");
    return;
  }
  const String requestBody = maintenanceWebServer.arg("plain");
  String targetArea = "images";
  String requestedPath;
  String dataBase64;
  String expectedSha256;
  parseBodyStringValue(requestBody, "targetArea", &targetArea);
  parseBodyStringValue(requestBody, "path", &requestedPath);
  parseBodyStringValue(requestBody, "dataBase64", &dataBase64);
  parseBodyStringValue(requestBody, "expectedSha256", &expectedSha256);
  if (targetArea.equalsIgnoreCase("certs") && !isAuthorized(maintenanceRole::kAdmin)) {
    maintenanceWebServer.send(403, "application/json", "{\"result\":\"NG\",\"detail\":\"admin role required for certs update\"}");
    return;
  }
  String normalizedPath;
  if (!normalizeManagedFilePathForAp(targetArea, requestedPath, &normalizedPath)) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"invalid targetArea/path\"}");
    return;
  }
  if (dataBase64.length() == 0) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"dataBase64 is required\"}");
    return;
  }
  if (!ensureLittleFsReadyForAp()) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"filesystem is unavailable\"}");
    return;
  }
  std::vector<uint8_t> fileBytes;
  if (!decodeBase64TextForAp(dataBase64, &fileBytes)) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"base64 decode failed\"}");
    return;
  }
  String actualSha256Hex;
  if (!computeSha256HexForBytesForAp(fileBytes, &actualSha256Hex)) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"sha256 calculation failed\"}");
    return;
  }
  String normalizedExpectedSha = expectedSha256;
  normalizedExpectedSha.trim();
  normalizedExpectedSha.toLowerCase();
  if (normalizedExpectedSha.length() > 0 && !actualSha256Hex.equalsIgnoreCase(normalizedExpectedSha)) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"sha256 mismatch\"}");
    return;
  }
  if (!writeFileAtomicallyForAp(normalizedPath, fileBytes)) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"file write failed\"}");
    return;
  }
  String responseText = "{\"result\":\"OK\",\"targetArea\":\"" + toJsonSafeText(targetArea) +
                        "\",\"path\":\"" + toJsonSafeText(normalizedPath) +
                        "\",\"size\":" + String(static_cast<long>(fileBytes.size())) +
                        ",\"sha256\":\"" + toJsonSafeText(actualSha256Hex) + "\"}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief APモード中に `/images` または `/certs` の1ファイルを削除するAPI。
 * @details
 * - [厳守] `/certs` 削除は admin ロール必須。
 */
void handleManagedFileDeleteApi() {
  if (!isAuthorized(maintenanceRole::kMaintenance)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"unauthorized\"}");
    return;
  }
  const String requestBody = maintenanceWebServer.arg("plain");
  String targetArea = "images";
  String requestedPath;
  parseBodyStringValue(requestBody, "targetArea", &targetArea);
  parseBodyStringValue(requestBody, "path", &requestedPath);
  if (targetArea.equalsIgnoreCase("certs") && !isAuthorized(maintenanceRole::kAdmin)) {
    maintenanceWebServer.send(403, "application/json", "{\"result\":\"NG\",\"detail\":\"admin role required for certs delete\"}");
    return;
  }
  String normalizedPath;
  if (!normalizeManagedFilePathForAp(targetArea, requestedPath, &normalizedPath)) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"invalid targetArea/path\"}");
    return;
  }
  if (!ensureLittleFsReadyForAp()) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"filesystem is unavailable\"}");
    return;
  }
  bool removed = true;
  if (LittleFS.exists(normalizedPath)) {
    removed = LittleFS.remove(normalizedPath);
  }
  if (!removed) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"file delete failed\"}");
    return;
  }
  String responseText = "{\"result\":\"OK\",\"targetArea\":\"" + toJsonSafeText(targetArea) +
                        "\",\"path\":\"" + toJsonSafeText(normalizedPath) + "\"}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

void handleHealthApi() {
  const String remoteIpText = maintenanceWebServer.client().remoteIP().toString();
  logMaintenanceApRuntimeSnapshot("handleHealthApi", remoteIpText.c_str());
  String responseText = "{\"result\":\"OK\",\"mode\":\"maintenance-ap\",\"ssid\":\"" + currentApSsid +
                        "\",\"firmwareOperationMode\":\"" + String(firmwareMode::kFirmwareOperationMode) +
                        "\",\"serialOutputMode\":\"" + String(firmwareMode::kSerialOutputMode) +
                        "\",\"factoryApisEnabled\":" + String(firmwareMode::kFactoryApisEnabled ? "true" : "false") + "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief Pairing 状態を返すAPI。
 * @details
 * - [重要] `runPairingSession()` の Rust 側 workflow が AP 到達後に参照する状態受け口。
 * - [進捗] 2026-03-16 時点では placeholder 実装として、対象デバイス識別子、`keyDevice` 有無、直近 session/bundle の枠のみ返す。
 * - [進捗][2026-03-21] `savedCurrentKeyVersion` と `previousKeyState` は Pairing / KeyRotation 直後の実保存結果を返す。
 */
void handlePairingStateApi() {
  if (!ensureFactoryApiEnabled("handlePairingStateApi")) {
    return;
  }
  if (!isAuthorized(maintenanceRole::kMaintenance)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"unauthorized\"}");
    return;
  }
  if (sensitiveDataServiceInstance == nullptr) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"sensitiveDataService is null\"}");
    return;
  }

  String keyDevice;
  if (!sensitiveDataServiceInstance->loadKeyDevice(&keyDevice)) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"load keyDevice failed\"}");
    return;
  }

  const String targetDeviceId =
      lastPairingTargetDeviceId.length() > 0 ? lastPairingTargetDeviceId : resolvePairingTargetDeviceId();
  String responseText;
  responseText.reserve(512);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"state\":\"" + toJsonSafeText(lastPairingState) + "\",";
  responseText += "\"targetDeviceId\":\"" + toJsonSafeText(targetDeviceId) + "\",";
  responseText += "\"sessionId\":\"" + toJsonSafeText(lastPairingSessionId) + "\",";
  responseText += "\"bundleId\":\"" + toJsonSafeText(lastPairingBundleId) + "\",";
  responseText += "\"publicId\":\"" + toJsonSafeText(lastPairingPublicId) + "\",";
  responseText += "\"keyVersion\":\"" + toJsonSafeText(lastPairingKeyVersion) + "\",";
  responseText += "\"nonce\":\"" + toJsonSafeText(lastPairingNonce) + "\",";
  responseText += "\"signature\":\"" + toJsonSafeText(lastPairingSignature) + "\",";
  responseText += "\"requestedSettingsSha256\":\"" + toJsonSafeText(lastPairingRequestedSettingsSha256) + "\",";
  responseText += "\"acceptedKeyAgreement\":\"" + toJsonSafeText(lastPairingAcceptedKeyAgreement) + "\",";
  responseText += "\"acceptedBundleProtection\":\"" + toJsonSafeText(lastPairingAcceptedBundleProtection) + "\",";
  responseText += "\"transportSharedSecretFingerprint\":\"" + toJsonSafeText(lastPairingTransportSharedSecretFingerprint) + "\",";
  responseText += "\"transportServerPublicKeyBase64\":\"" + toJsonSafeText(lastPairingTransportServerPublicKeyBase64) + "\",";
  responseText += "\"detail\":\"" + toJsonSafeText(lastPairingDetail) + "\",";
  responseText += "\"savedCurrentKeyVersion\":\"" + toJsonSafeText(lastPairingSavedCurrentKeyVersion) + "\",";
  responseText += "\"previousKeyState\":\"" + toJsonSafeText(lastPairingPreviousKeyState) + "\",";
  responseText += "\"keyDevicePresent\":" + String(keyDevice.length() > 0 ? "true" : "false");
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief Pairing workflow の session metadata を受理するAPI。
 * @details
 * - [重要] 現段階では秘密 payload 本文を受けず、`sessionId` / `bundleId` / `targetDeviceId` / `keyVersion` を受けて
 *   AP 側状態を `bundle_received` へ更新する placeholder 実装とする。
 * - [厳守] `runPairingSession()` の内部 helper 直公開を避けるため、bundle 本文や raw `k-device` は本APIで受け付けない。
 * - [将来対応] ECDH・署名検証・復号・NVS 保存本体を実装したら、本関数で metadata 受理から本適用処理へ置き換える。
 */
void handlePairingSessionApi() {
  if (!ensureFactoryApiEnabled("handlePairingSessionApi")) {
    return;
  }
  if (!isAuthorized(maintenanceRole::kAdmin)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"admin authorization required\"}");
    return;
  }

  const String requestBody = maintenanceWebServer.arg("plain");
  String targetDeviceId;
  String sessionId;
  String bundleId;
  String keyVersion;
  parseBodyStringValue(requestBody, "targetDeviceId", &targetDeviceId);
  parseBodyStringValue(requestBody, "sessionId", &sessionId);
  parseBodyStringValue(requestBody, "bundleId", &bundleId);
  parseBodyStringValue(requestBody, "keyVersion", &keyVersion);
  targetDeviceId.trim();
  sessionId.trim();
  bundleId.trim();
  keyVersion.trim();
  if (targetDeviceId.length() == 0 || sessionId.length() == 0 || bundleId.length() == 0 || keyVersion.length() == 0) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"targetDeviceId/sessionId/bundleId/keyVersion is required\"}");
    return;
  }

  lastPairingTargetDeviceId = targetDeviceId;
  lastPairingSessionId = sessionId;
  lastPairingBundleId = bundleId;
  lastPairingKeyVersion = keyVersion;
  lastPairingState = "bundle_received";
  lastPairingSavedCurrentKeyVersion = "";
  lastPairingPreviousKeyState = "none";
  lastPairingResult = "OK";
  lastPairingDetail = String("pairing session metadata accepted. sessionId=") + sessionId + " bundleId=" + bundleId;

  String responseText;
  responseText.reserve(256);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"state\":\"" + toJsonSafeText(lastPairingState) + "\",";
  responseText += "\"targetDeviceId\":\"" + toJsonSafeText(lastPairingTargetDeviceId) + "\",";
  responseText += "\"sessionId\":\"" + toJsonSafeText(lastPairingSessionId) + "\",";
  responseText += "\"bundleId\":\"" + toJsonSafeText(lastPairingBundleId) + "\",";
  responseText += "\"detail\":\"" + toJsonSafeText(lastPairingDetail) + "\"";
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief Pairing bundle summary を受理するAPI。
 * @details
 * - [重要] 現段階では `createPairingBundle` の平文本体を送らず、bundle を識別・監査する最小 summary のみ受ける。
 * - [厳守] `publicId` / `nonce` / `signature` / `requestedSettingsSha256` は受理状態の保持に使うだけで、
 *   この段階では署名検証・復号・NVS 保存を実行しない。
 * - [禁止] raw `k-device`、Wi-Fi/MQTT/OTA 認証情報、bundle 平文 JSON をこのAPIで受けない。
 * - [将来対応] ECDH と secure bundle transport 実装後は、本APIまたは後継APIで summary ではなく暗号化済み本体を受理する。
 */
void handlePairingBundleSummaryApi() {
  if (!ensureFactoryApiEnabled("handlePairingBundleSummaryApi")) {
    return;
  }
  if (!isAuthorized(maintenanceRole::kAdmin)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"admin authorization required\"}");
    return;
  }

  const String requestBody = maintenanceWebServer.arg("plain");
  String targetDeviceId;
  String sessionId;
  String bundleId;
  String publicId;
  String keyVersion;
  String nonce;
  String signature;
  String requestedSettingsSha256;
  parseBodyStringValue(requestBody, "targetDeviceId", &targetDeviceId);
  parseBodyStringValue(requestBody, "sessionId", &sessionId);
  parseBodyStringValue(requestBody, "bundleId", &bundleId);
  parseBodyStringValue(requestBody, "publicId", &publicId);
  parseBodyStringValue(requestBody, "keyVersion", &keyVersion);
  parseBodyStringValue(requestBody, "nonce", &nonce);
  parseBodyStringValue(requestBody, "signature", &signature);
  parseBodyStringValue(requestBody, "requestedSettingsSha256", &requestedSettingsSha256);
  targetDeviceId.trim();
  sessionId.trim();
  bundleId.trim();
  publicId.trim();
  keyVersion.trim();
  nonce.trim();
  signature.trim();
  requestedSettingsSha256.trim();
  if (targetDeviceId.length() == 0 || sessionId.length() == 0 || bundleId.length() == 0 || publicId.length() == 0 ||
      keyVersion.length() == 0 || nonce.length() == 0 || signature.length() == 0 || requestedSettingsSha256.length() == 0) {
    maintenanceWebServer.send(
        400,
        "application/json",
        "{\"result\":\"NG\",\"detail\":\"targetDeviceId/sessionId/bundleId/publicId/keyVersion/nonce/signature/requestedSettingsSha256 is required\"}");
    return;
  }

  if (lastPairingSessionId.length() > 0 && lastPairingSessionId != sessionId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing session mismatch\"}");
    return;
  }
  if (lastPairingBundleId.length() > 0 && lastPairingBundleId != bundleId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing bundle mismatch\"}");
    return;
  }

  lastPairingTargetDeviceId = targetDeviceId;
  lastPairingSessionId = sessionId;
  lastPairingBundleId = bundleId;
  lastPairingPublicId = publicId;
  lastPairingKeyVersion = keyVersion;
  lastPairingNonce = nonce;
  lastPairingSignature = signature;
  lastPairingRequestedSettingsSha256 = requestedSettingsSha256;
  lastPairingState = "bundle_staged";
  lastPairingResult = "OK";
  lastPairingDetail =
      String("pairing bundle summary accepted. sessionId=") + sessionId + " bundleId=" + bundleId + " keyVersion=" + keyVersion;

  String responseText;
  responseText.reserve(384);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"state\":\"" + toJsonSafeText(lastPairingState) + "\",";
  responseText += "\"targetDeviceId\":\"" + toJsonSafeText(lastPairingTargetDeviceId) + "\",";
  responseText += "\"sessionId\":\"" + toJsonSafeText(lastPairingSessionId) + "\",";
  responseText += "\"bundleId\":\"" + toJsonSafeText(lastPairingBundleId) + "\",";
  responseText += "\"publicId\":\"" + toJsonSafeText(lastPairingPublicId) + "\",";
  responseText += "\"keyVersion\":\"" + toJsonSafeText(lastPairingKeyVersion) + "\",";
  responseText += "\"requestedSettingsSha256\":\"" + toJsonSafeText(lastPairingRequestedSettingsSha256) + "\",";
  responseText += "\"detail\":\"" + toJsonSafeText(lastPairingDetail) + "\"";
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief Pairing secure transport の placeholder 交渉状態を受理するAPI。
 * @details
 * - [重要] 現段階では ECDH や AES-GCM 本体をまだ実行せず、どの保護方式で secure bundle を送る想定かだけを固定する。
 * - [厳守] このAPIで raw `k-device`、ECDH 共有秘密、暗号化済み bundle 本体を受けない。
 * - [厳守] `requestedKeyAgreement` / `requestedBundleProtection` は placeholder 交渉値として扱い、実暗号処理成功を意味しない。
 * - [将来対応] secure transport 実装後は、本APIの交渉結果に従って暗号化済み bundle を受ける後続APIへ接続する。
 */
void handlePairingTransportSessionApi() {
  if (!ensureFactoryApiEnabled("handlePairingTransportSessionApi")) {
    return;
  }
  if (!isAuthorized(maintenanceRole::kAdmin)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"admin authorization required\"}");
    return;
  }

  const String requestBody = maintenanceWebServer.arg("plain");
  String targetDeviceId;
  String sessionId;
  String bundleId;
  String requestedKeyAgreement;
  String requestedBundleProtection;
  parseBodyStringValue(requestBody, "targetDeviceId", &targetDeviceId);
  parseBodyStringValue(requestBody, "sessionId", &sessionId);
  parseBodyStringValue(requestBody, "bundleId", &bundleId);
  parseBodyStringValue(requestBody, "requestedKeyAgreement", &requestedKeyAgreement);
  parseBodyStringValue(requestBody, "requestedBundleProtection", &requestedBundleProtection);
  targetDeviceId.trim();
  sessionId.trim();
  bundleId.trim();
  requestedKeyAgreement.trim();
  requestedBundleProtection.trim();
  if (targetDeviceId.length() == 0 || sessionId.length() == 0 || bundleId.length() == 0 ||
      requestedKeyAgreement.length() == 0 || requestedBundleProtection.length() == 0) {
    maintenanceWebServer.send(
        400,
        "application/json",
        "{\"result\":\"NG\",\"detail\":\"targetDeviceId/sessionId/bundleId/requestedKeyAgreement/requestedBundleProtection is required\"}");
    return;
  }

  if (lastPairingTargetDeviceId.length() > 0 && lastPairingTargetDeviceId != targetDeviceId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing targetDeviceId mismatch\"}");
    return;
  }
  if (lastPairingSessionId.length() > 0 && lastPairingSessionId != sessionId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing session mismatch\"}");
    return;
  }
  if (lastPairingBundleId.length() > 0 && lastPairingBundleId != bundleId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing bundle mismatch\"}");
    return;
  }

  lastPairingTargetDeviceId = targetDeviceId;
  lastPairingSessionId = sessionId;
  lastPairingBundleId = bundleId;
  lastPairingAcceptedKeyAgreement = requestedKeyAgreement;
  lastPairingAcceptedBundleProtection = requestedBundleProtection;
  lastPairingTransportSharedSecretFingerprint = "";
  lastPairingTransportServerPublicKeyBase64 = "";
  lastPairingTransportSessionKeyBytes.clear();
  lastPairingState = "transport_prepared";
  lastPairingResult = "OK";
  lastPairingDetail = String("pairing transport placeholder prepared. sessionId=") + sessionId +
                      " bundleId=" + bundleId + " keyAgreement=" + requestedKeyAgreement +
                      " bundleProtection=" + requestedBundleProtection;

  String responseText;
  responseText.reserve(384);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"state\":\"" + toJsonSafeText(lastPairingState) + "\",";
  responseText += "\"targetDeviceId\":\"" + toJsonSafeText(lastPairingTargetDeviceId) + "\",";
  responseText += "\"sessionId\":\"" + toJsonSafeText(lastPairingSessionId) + "\",";
  responseText += "\"bundleId\":\"" + toJsonSafeText(lastPairingBundleId) + "\",";
  responseText += "\"acceptedKeyAgreement\":\"" + toJsonSafeText(lastPairingAcceptedKeyAgreement) + "\",";
  responseText += "\"acceptedBundleProtection\":\"" + toJsonSafeText(lastPairingAcceptedBundleProtection) + "\",";
  responseText += "\"detail\":\"" + toJsonSafeText(lastPairingDetail) + "\"";
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief Pairing secure transport の P-256 ECDH handshake を受けるAPI。
 * @details
 * - [重要] Rust 側公開鍵を受けて ESP32 側公開鍵を返し、双方で共有秘密から 32byte transport 鍵を導出する。
 * - [厳守] 導出した transport 鍵は AP プロセス内メモリにのみ保持し、NVS やレスポンスへ返さない。
 * - [禁止] `LocalServer` や REST 応答へ raw ECDH 共有秘密を返さない。
 * - [将来対応] 後続の encrypted bundle 受理APIで、本 handshake により導出した鍵を使って復号・検証する。
 */
void handlePairingTransportHandshakeApi() {
  if (!ensureFactoryApiEnabled("handlePairingTransportHandshakeApi")) {
    return;
  }
  if (!isAuthorized(maintenanceRole::kAdmin)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"admin authorization required\"}");
    return;
  }

  const String requestBody = maintenanceWebServer.arg("plain");
  String targetDeviceId;
  String sessionId;
  String bundleId;
  String clientPublicKeyBase64;
  parseBodyStringValue(requestBody, "targetDeviceId", &targetDeviceId);
  parseBodyStringValue(requestBody, "sessionId", &sessionId);
  parseBodyStringValue(requestBody, "bundleId", &bundleId);
  parseBodyStringValue(requestBody, "clientPublicKeyBase64", &clientPublicKeyBase64);
  targetDeviceId.trim();
  sessionId.trim();
  bundleId.trim();
  clientPublicKeyBase64.trim();
  if (targetDeviceId.length() == 0 || sessionId.length() == 0 || bundleId.length() == 0 || clientPublicKeyBase64.length() == 0) {
    maintenanceWebServer.send(
        400,
        "application/json",
        "{\"result\":\"NG\",\"detail\":\"targetDeviceId/sessionId/bundleId/clientPublicKeyBase64 is required\"}");
    return;
  }
  if (lastPairingTargetDeviceId.length() > 0 && lastPairingTargetDeviceId != targetDeviceId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing targetDeviceId mismatch\"}");
    return;
  }
  if (lastPairingSessionId.length() > 0 && lastPairingSessionId != sessionId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing session mismatch\"}");
    return;
  }
  if (lastPairingBundleId.length() > 0 && lastPairingBundleId != bundleId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing bundle mismatch\"}");
    return;
  }
  if (!lastPairingAcceptedKeyAgreement.equalsIgnoreCase("ecdh-p256-v1")) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing key agreement is not prepared\"}");
    return;
  }

  String serverPublicKeyBase64;
  String sharedSecretFingerprint;
  if (!performPairingTransportHandshakeForAp(clientPublicKeyBase64,
                                             sessionId,
                                             bundleId,
                                             &serverPublicKeyBase64,
                                             &sharedSecretFingerprint)) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"transport handshake failed\"}");
    return;
  }

  lastPairingTransportServerPublicKeyBase64 = serverPublicKeyBase64;
  lastPairingTransportSharedSecretFingerprint = sharedSecretFingerprint;
  lastPairingState = "transport_established";
  lastPairingResult = "OK";
  lastPairingDetail =
      String("pairing transport handshake established. sessionId=") + sessionId + " bundleId=" + bundleId;

  String responseText;
  responseText.reserve(512);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"state\":\"" + toJsonSafeText(lastPairingState) + "\",";
  responseText += "\"targetDeviceId\":\"" + toJsonSafeText(lastPairingTargetDeviceId) + "\",";
  responseText += "\"sessionId\":\"" + toJsonSafeText(lastPairingSessionId) + "\",";
  responseText += "\"bundleId\":\"" + toJsonSafeText(lastPairingBundleId) + "\",";
  responseText += "\"acceptedKeyAgreement\":\"" + toJsonSafeText(lastPairingAcceptedKeyAgreement) + "\",";
  responseText += "\"acceptedBundleProtection\":\"" + toJsonSafeText(lastPairingAcceptedBundleProtection) + "\",";
  responseText += "\"serverPublicKeyBase64\":\"" + toJsonSafeText(lastPairingTransportServerPublicKeyBase64) + "\",";
  responseText += "\"sharedSecretFingerprint\":\"" + toJsonSafeText(lastPairingTransportSharedSecretFingerprint) + "\",";
  responseText += "\"detail\":\"" + toJsonSafeText(lastPairingDetail) + "\"";
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief Pairing secure bundle（暗号化済み）を受理し、復号後にNVSへ反映するAPI。
 * @details
 * - [重要] Rust 側で AES-256-GCM 暗号化された payload を受け取り、AP で復号して `sensitiveDataService` へ保存する。
 * - [厳守] 復号対象の AAD は `pairing-secure-bundle-v1|target|session|bundle|keyVersion` で固定する。
 * - [厳守] raw transport session key / 復号済み平文はログに出力しない。
 * - [禁止] handshake 未完了状態での適用実行を許可しない。
 */
void handlePairingSecureBundleApplyApi() {
  if (!ensureFactoryApiEnabled("handlePairingSecureBundleApplyApi")) {
    return;
  }
  if (!isAuthorized(maintenanceRole::kAdmin)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"admin authorization required\"}");
    return;
  }
  if (sensitiveDataServiceInstance == nullptr) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"sensitiveDataService is null\"}");
    return;
  }

  const String requestBody = maintenanceWebServer.arg("plain");
  String targetDeviceId;
  String sessionId;
  String bundleId;
  String keyVersion;
  String ivBase64;
  String cipherBase64;
  String tagBase64;
  String payloadSha256;
  parseBodyStringValue(requestBody, "targetDeviceId", &targetDeviceId);
  parseBodyStringValue(requestBody, "sessionId", &sessionId);
  parseBodyStringValue(requestBody, "bundleId", &bundleId);
  parseBodyStringValue(requestBody, "keyVersion", &keyVersion);
  parseBodyStringValue(requestBody, "ivBase64", &ivBase64);
  parseBodyStringValue(requestBody, "cipherBase64", &cipherBase64);
  parseBodyStringValue(requestBody, "tagBase64", &tagBase64);
  parseBodyStringValue(requestBody, "payloadSha256", &payloadSha256);
  targetDeviceId.trim();
  sessionId.trim();
  bundleId.trim();
  keyVersion.trim();
  ivBase64.trim();
  cipherBase64.trim();
  tagBase64.trim();
  payloadSha256.trim();
  if (targetDeviceId.length() == 0 || sessionId.length() == 0 || bundleId.length() == 0 || keyVersion.length() == 0 ||
      ivBase64.length() == 0 || cipherBase64.length() == 0 || tagBase64.length() == 0 || payloadSha256.length() == 0) {
    maintenanceWebServer.send(
        400,
        "application/json",
        "{\"result\":\"NG\",\"detail\":\"targetDeviceId/sessionId/bundleId/keyVersion/ivBase64/cipherBase64/tagBase64/payloadSha256 is required\"}");
    return;
  }
  if (lastPairingState != "transport_established") {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing transport is not established\"}");
    return;
  }
  if (lastPairingTargetDeviceId.length() > 0 && lastPairingTargetDeviceId != targetDeviceId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing targetDeviceId mismatch\"}");
    return;
  }
  if (lastPairingSessionId.length() > 0 && lastPairingSessionId != sessionId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing session mismatch\"}");
    return;
  }
  if (lastPairingBundleId.length() > 0 && lastPairingBundleId != bundleId) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing bundle mismatch\"}");
    return;
  }
  if (!lastPairingAcceptedBundleProtection.equalsIgnoreCase("aes-256-gcm-v1")) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing bundle protection is not prepared\"}");
    return;
  }
  if (lastPairingTransportSessionKeyBytes.size() != 32) {
    maintenanceWebServer.send(409, "application/json", "{\"result\":\"NG\",\"detail\":\"pairing transport session key is not ready\"}");
    return;
  }

  std::vector<uint8_t> ivBytes;
  std::vector<uint8_t> cipherBytes;
  std::vector<uint8_t> tagBytes;
  if (!decodeBase64TextForAp(ivBase64, &ivBytes) || ivBytes.size() != 12) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"invalid ivBase64\"}");
    return;
  }
  if (!decodeBase64TextForAp(cipherBase64, &cipherBytes) || cipherBytes.empty()) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"invalid cipherBase64\"}");
    return;
  }
  if (!decodeBase64TextForAp(tagBase64, &tagBytes) || tagBytes.size() != 16) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"invalid tagBase64\"}");
    return;
  }

  std::vector<uint8_t> aadBytes;
  if (!createPairingSecureBundleAadForAp(targetDeviceId, sessionId, bundleId, keyVersion, &aadBytes)) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"create AAD failed\"}");
    return;
  }
  std::vector<uint8_t> plainPayloadBytes;
  if (!decryptAes256GcmForAp(lastPairingTransportSessionKeyBytes, ivBytes, cipherBytes, tagBytes, aadBytes, &plainPayloadBytes)) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"secure bundle decrypt failed\"}");
    return;
  }
  String actualPayloadSha256;
  if (!computeSha256HexForBytesForAp(plainPayloadBytes, &actualPayloadSha256)) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"payload sha256 calculation failed\"}");
    return;
  }
  if (!actualPayloadSha256.equalsIgnoreCase(payloadSha256)) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"payload sha256 mismatch\"}");
    return;
  }

  String plainPayloadJson;
  plainPayloadJson.reserve(plainPayloadBytes.size());
  for (size_t index = 0; index < plainPayloadBytes.size(); ++index) {
    plainPayloadJson += static_cast<char>(plainPayloadBytes[index]);
  }
  String payloadTargetDeviceId;
  String payloadSessionId;
  String payloadBundleId;
  String payloadKeyVersion;
  parseBodyStringValue(plainPayloadJson, "targetDeviceId", &payloadTargetDeviceId);
  parseBodyStringValue(plainPayloadJson, "sessionId", &payloadSessionId);
  parseBodyStringValue(plainPayloadJson, "bundleId", &payloadBundleId);
  parseBodyStringValue(plainPayloadJson, "keyVersion", &payloadKeyVersion);
  payloadTargetDeviceId.trim();
  payloadSessionId.trim();
  payloadBundleId.trim();
  payloadKeyVersion.trim();
  if (payloadTargetDeviceId != targetDeviceId || payloadSessionId != sessionId || payloadBundleId != bundleId || payloadKeyVersion != keyVersion) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"payload identity mismatch\"}");
    return;
  }

  String wifiSsid;
  String wifiPassword;
  String mqttHost;
  String mqttHostName;
  String mqttUsername;
  String mqttPassword;
  String otaHost;
  String otaHostName;
  String otaUsername;
  String otaPassword;
  String keyDeviceBase64;
  bool mqttTls = false;
  bool otaTls = false;
  long mqttPort = 0;
  long otaPort = 0;
  parseBodyStringValue(plainPayloadJson, "requestedSettings.wifi.ssid", &wifiSsid);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.wifi.password", &wifiPassword);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.mqtt.host", &mqttHost);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.mqtt.hostName", &mqttHostName);
  parseBodyLongValue(plainPayloadJson, "requestedSettings.mqtt.port", &mqttPort);
  parseBodyBoolValue(plainPayloadJson, "requestedSettings.mqtt.tls", &mqttTls);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.mqtt.username", &mqttUsername);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.mqtt.password", &mqttPassword);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.ota.host", &otaHost);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.ota.hostName", &otaHostName);
  parseBodyLongValue(plainPayloadJson, "requestedSettings.ota.port", &otaPort);
  parseBodyBoolValue(plainPayloadJson, "requestedSettings.ota.tls", &otaTls);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.ota.username", &otaUsername);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.ota.password", &otaPassword);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.credentials.keyDevice", &keyDeviceBase64);
  wifiSsid.trim();
  wifiPassword.trim();
  mqttHost.trim();
  mqttHostName.trim();
  mqttUsername.trim();
  mqttPassword.trim();
  otaHost.trim();
  otaHostName.trim();
  otaUsername.trim();
  otaPassword.trim();
  keyDeviceBase64.trim();
  if (wifiSsid.length() == 0 || wifiPassword.length() == 0 || mqttHost.length() == 0 || mqttUsername.length() == 0 || mqttPassword.length() == 0 ||
      otaHost.length() == 0 || otaUsername.length() == 0 || otaPassword.length() == 0 || keyDeviceBase64.length() == 0 ||
      mqttPort <= 0 || otaPort <= 0) {
    maintenanceWebServer.send(
        400,
        "application/json",
        "{\"result\":\"NG\",\"detail\":\"secure payload required fields are missing\"}");
    return;
  }

  String previousKeyDevice;
  const bool previousKeyLoadResult = sensitiveDataServiceInstance->loadKeyDevice(&previousKeyDevice);
  const String previousKeyState = (previousKeyLoadResult && previousKeyDevice.length() > 0) ? "grace" : "none";

  bool saveResult = true;
  saveResult = saveResult && sensitiveDataServiceInstance->saveWifiCredentials(wifiSsid, wifiPassword);
  saveResult = saveResult && sensitiveDataServiceInstance->saveMqttConfig(
                                 mqttHost,
                                 mqttHostName.length() > 0 ? mqttHostName : mqttHost,
                                 mqttUsername,
                                 mqttPassword,
                                 static_cast<int32_t>(mqttPort),
                                 mqttTls);
  saveResult = saveResult && sensitiveDataServiceInstance->saveOtaConfig(
                                 otaHost,
                                 otaHostName.length() > 0 ? otaHostName : otaHost,
                                 otaUsername,
                                 otaPassword,
                                 static_cast<int32_t>(otaPort),
                                 otaTls);
  saveResult = saveResult && sensitiveDataServiceInstance->savePairingKeySlots(keyDeviceBase64, keyVersion);

  String serverHost;
  String serverHostName;
  long serverPort = 0;
  bool serverTls = false;
  parseBodyStringValue(plainPayloadJson, "requestedSettings.server.host", &serverHost);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.server.hostName", &serverHostName);
  parseBodyLongValue(plainPayloadJson, "requestedSettings.server.port", &serverPort);
  parseBodyBoolValue(plainPayloadJson, "requestedSettings.server.tls", &serverTls);
  serverHost.trim();
  serverHostName.trim();
  if (serverHost.length() > 0 && serverPort > 0) {
    String currentServerUrl;
    String currentServerUrlName;
    String currentServerUser;
    String currentServerPass;
    int32_t currentServerPort = 443;
    bool currentServerTls = true;
    if (!sensitiveDataServiceInstance->loadServerConfig(&currentServerUrl,
                                                        &currentServerUrlName,
                                                        &currentServerUser,
                                                        &currentServerPass,
                                                        &currentServerPort,
                                                        &currentServerTls)) {
      saveResult = false;
    } else {
      saveResult = saveResult && sensitiveDataServiceInstance->saveServerConfig(
                                     serverHost,
                                     serverHostName.length() > 0 ? serverHostName : serverHost,
                                     currentServerUser,
                                     currentServerPass,
                                     static_cast<int32_t>(serverPort),
                                     serverTls);
    }
  }

  String ntpHost;
  String ntpHostName;
  long ntpPort = 0;
  bool ntpTls = false;
  parseBodyStringValue(plainPayloadJson, "requestedSettings.ntp.host", &ntpHost);
  parseBodyStringValue(plainPayloadJson, "requestedSettings.ntp.hostName", &ntpHostName);
  parseBodyLongValue(plainPayloadJson, "requestedSettings.ntp.port", &ntpPort);
  parseBodyBoolValue(plainPayloadJson, "requestedSettings.ntp.tls", &ntpTls);
  ntpHost.trim();
  ntpHostName.trim();
  if (ntpHost.length() > 0 && ntpPort > 0) {
    saveResult = saveResult && sensitiveDataServiceInstance->saveTimeServerConfig(
                                   ntpHost,
                                   ntpHostName.length() > 0 ? ntpHostName : ntpHost,
                                   static_cast<int32_t>(ntpPort),
                                   ntpTls);
  }

  if (!saveResult) {
    lastPairingResult = "NG";
    lastPairingState = "failed";
    lastPairingDetail = String("pairing secure bundle apply failed. sessionId=") + sessionId + " bundleId=" + bundleId;
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"secure bundle persistence failed\"}");
    return;
  }

  lastPairingTargetDeviceId = targetDeviceId;
  lastPairingSessionId = sessionId;
  lastPairingBundleId = bundleId;
  lastPairingKeyVersion = keyVersion;
  lastPairingSavedCurrentKeyVersion = keyVersion;
  lastPairingPreviousKeyState = previousKeyState;
  lastPairingState = "applied";
  lastPairingResult = "OK";
  lastPairingDetail = String("pairing secure bundle applied. sessionId=") + sessionId + " bundleId=" + bundleId;
  lastPairingTransportSessionKeyBytes.clear();

  String responseText;
  responseText.reserve(512);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"state\":\"" + toJsonSafeText(lastPairingState) + "\",";
  responseText += "\"targetDeviceId\":\"" + toJsonSafeText(lastPairingTargetDeviceId) + "\",";
  responseText += "\"sessionId\":\"" + toJsonSafeText(lastPairingSessionId) + "\",";
  responseText += "\"bundleId\":\"" + toJsonSafeText(lastPairingBundleId) + "\",";
  responseText += "\"savedCurrentKeyVersion\":\"" + toJsonSafeText(lastPairingSavedCurrentKeyVersion) + "\",";
  responseText += "\"previousKeyState\":\"" + toJsonSafeText(lastPairingPreviousKeyState) + "\",";
  responseText += "\"detail\":\"" + toJsonSafeText(lastPairingDetail) + "\"";
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief Production secure flow の直近状態を返すAPI。
 * @details
 * - [重要] 不可逆処理はまだ実行せず、precheck 収集結果と観測値のみ返す。
 * - [厳守] `mfg` ロール認証済みの場合のみ参照を許可する。
 */
void handleProductionStateApi() {
  if (!ensureFactoryApiEnabled("handleProductionStateApi")) {
    return;
  }
  if (!isAuthorized(maintenanceRole::kMfg)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"mfg authorization required\"}");
    return;
  }

  String responseText;
  responseText.reserve(512);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"runId\":\"" + toJsonSafeText(lastProductionRunId) + "\",";
  responseText += "\"state\":\"" + toJsonSafeText(lastProductionState) + "\",";
  responseText += "\"resultLabel\":\"" + toJsonSafeText(lastProductionResult) + "\",";
  responseText += "\"targetDeviceId\":\"" + toJsonSafeText(resolvePairingTargetDeviceId()) + "\",";
  responseText += "\"observedFirmwareVersion\":\"" + toJsonSafeText(lastProductionObservedFirmwareVersion) + "\",";
  responseText += "\"observedMac\":\"" + toJsonSafeText(lastProductionObservedMac) + "\",";
  responseText += "\"measuredFreeHeapBytes\":" + String(static_cast<long>(lastProductionObservedFreeHeapBytes)) + ",";
  responseText += "\"measuredMinStackMarginBytes\":" + String(static_cast<long>(lastProductionObservedStackMarginBytes)) + ",";
  responseText += "\"detail\":\"" + toJsonSafeText(lastProductionDetail) + "\"";
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief Production secure flow 用の事前チェック観測値を返すAPI。
 * @details
 * - [重要] 現段階では不可逆処理を実行せず、firmware/MAC/heap/stack の観測値だけを返す。
 * - [厳守] 閾値比較が必要な項目は、要求で与えられた期待値がある場合のみ OK/NG を返す。
 * - [禁止] eFuse 実値や秘密鍵素材をレスポンスへ含めない。
 */
void handleProductionPrecheckApi() {
  if (!ensureFactoryApiEnabled("handleProductionPrecheckApi")) {
    return;
  }
  if (!isAuthorized(maintenanceRole::kMfg)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"mfg authorization required\"}");
    return;
  }
  if (sensitiveDataServiceInstance == nullptr) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"sensitiveDataService is null\"}");
    return;
  }

  const String requestBody = maintenanceWebServer.arg("plain");
  String runId;
  String targetDeviceId;
  String expectedFirmwareVersion;
  String expectedMac;
  long minimumFreeHeapBytes = 0;
  long minimumStackMarginBytes = 0;
  const bool hasMinimumFreeHeapBytes = parseBodyLongValue(requestBody, "minimumFreeHeapBytes", &minimumFreeHeapBytes);
  const bool hasMinimumStackMarginBytes = parseBodyLongValue(requestBody, "minimumStackMarginBytes", &minimumStackMarginBytes);
  parseBodyStringValue(requestBody, "runId", &runId);
  parseBodyStringValue(requestBody, "targetDeviceId", &targetDeviceId);
  parseBodyStringValue(requestBody, "expectedFirmwareVersion", &expectedFirmwareVersion);
  parseBodyStringValue(requestBody, "expectedMac", &expectedMac);
  runId.trim();
  targetDeviceId.trim();
  expectedFirmwareVersion.trim();
  expectedMac.trim();
  if ((hasMinimumFreeHeapBytes && minimumFreeHeapBytes < 0) || (hasMinimumStackMarginBytes && minimumStackMarginBytes < 0)) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"minimumFreeHeapBytes/minimumStackMarginBytes must be non-negative\"}");
    return;
  }

  const String observedTargetDeviceId = resolvePairingTargetDeviceId();
  const String observedCompactMac = resolveCompactMacFromApSsid();
  const String observedMac = formatCompactMacAsColonSeparated(observedCompactMac);
  const String observedFirmwareVersion = String(appVersion::kFirmwareVersion);
  const uint32_t measuredFreeHeapBytes = ESP.getFreeHeap();
  const UBaseType_t stackHighWaterMarkWords = uxTaskGetStackHighWaterMark(nullptr);
  const uint32_t measuredMinStackMarginBytes = static_cast<uint32_t>(stackHighWaterMarkWords * sizeof(StackType_t));
  String keyDeviceBase64;
  const bool loadKeyDeviceResult = sensitiveDataServiceInstance->loadKeyDevice(&keyDeviceBase64);
  const bool keyDevicePresent = loadKeyDeviceResult && keyDeviceBase64.length() > 0;

  const bool hasTargetDeviceId = targetDeviceId.length() > 0;
  const bool hasExpectedFirmwareVersion = expectedFirmwareVersion.length() > 0;
  const bool hasExpectedMac = expectedMac.length() > 0;
  const bool macMatched = hasExpectedMac
                              ? normalizeMacTextForComparison(expectedMac) == normalizeMacTextForComparison(observedMac)
                              : false;
  bool targetDeviceMatched = false;
  bool hasTargetDeviceMatched = false;
  if (hasTargetDeviceId || hasExpectedMac) {
    hasTargetDeviceMatched = true;
    targetDeviceMatched = true;
    if (hasTargetDeviceId) {
      targetDeviceMatched = targetDeviceMatched && targetDeviceId == observedTargetDeviceId;
    }
    if (hasExpectedMac) {
      targetDeviceMatched = targetDeviceMatched && macMatched;
    }
  }
  const bool firmwareVersionApproved = hasExpectedFirmwareVersion ? expectedFirmwareVersion == observedFirmwareVersion : false;
  const bool heapMarginOk =
      hasMinimumFreeHeapBytes ? measuredFreeHeapBytes >= static_cast<uint32_t>(minimumFreeHeapBytes) : false;
  const bool stackMarginOk =
      hasMinimumStackMarginBytes ? measuredMinStackMarginBytes >= static_cast<uint32_t>(minimumStackMarginBytes) : false;

  lastProductionRunId = runId;
  lastProductionState = "precheck_collected";
  lastProductionResult = "OK";
  lastProductionObservedFirmwareVersion = observedFirmwareVersion;
  lastProductionObservedMac = observedMac;
  lastProductionObservedFreeHeapBytes = measuredFreeHeapBytes;
  lastProductionObservedStackMarginBytes = measuredMinStackMarginBytes;
  lastProductionDetail = String("production precheck collected. targetDeviceId=") + observedTargetDeviceId +
                         " firmwareVersion=" + observedFirmwareVersion +
                         " keyDevicePresent=" + (keyDevicePresent ? "true" : "false");

  String responseText;
  responseText.reserve(1024);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"runId\":\"" + toJsonSafeText(runId) + "\",";
  responseText += "\"state\":\"" + toJsonSafeText(lastProductionState) + "\",";
  responseText += "\"targetDeviceId\":\"" + toJsonSafeText(observedTargetDeviceId) + "\",";
  responseText += "\"observedFirmwareVersion\":\"" + toJsonSafeText(observedFirmwareVersion) + "\",";
  responseText += "\"observedMac\":\"" + toJsonSafeText(observedMac) + "\",";
  responseText += "\"keyDevicePresent\":" + String(keyDevicePresent ? "true" : "false") + ",";
  responseText += "\"measuredFreeHeapBytes\":" + String(static_cast<long>(measuredFreeHeapBytes)) + ",";
  responseText += "\"measuredMinStackMarginBytes\":" + String(static_cast<long>(measuredMinStackMarginBytes)) + ",";
  if (hasTargetDeviceMatched) {
    responseText += "\"targetDeviceMatched\":" + String(targetDeviceMatched ? "true" : "false") + ",";
  }
  if (hasExpectedFirmwareVersion) {
    responseText += "\"firmwareVersionApproved\":" + String(firmwareVersionApproved ? "true" : "false") + ",";
  }
  if (hasMinimumStackMarginBytes) {
    responseText += "\"stackMarginOk\":" + String(stackMarginOk ? "true" : "false") + ",";
  }
  if (hasMinimumFreeHeapBytes) {
    responseText += "\"heapMarginOk\":" + String(heapMarginOk ? "true" : "false") + ",";
  }
  responseText += "\"detail\":\"" + toJsonSafeText(lastProductionDetail) + "\"";
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

void handleLoginApi() {
  const String remoteIpText = maintenanceWebServer.client().remoteIP().toString();
  const String requestBody = maintenanceWebServer.arg("plain");
  String username;
  String password;
  parseBodyStringValue(requestBody, "username", &username);
  parseBodyStringValue(requestBody, "password", &password);
  username.trim();
  password.trim();

  if (requestBody.length() == 0) {
    appLogError("handleLoginApi failed. request body is empty. remoteIp=%s requestBodyLength=%ld apSsid=%s",
                remoteIpText.c_str(),
                static_cast<long>(requestBody.length()),
                currentApSsid.c_str());
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"authentication failed\"}");
    return;
  }

  // [重要] 認証失敗時の切り分け用に、受信元IPと候補ロールを残す。
  // PowerShell 側の本文破損と認証不一致を分けて追跡しやすくする。
  String matchedRoleText = "unknown";
  if (username == mfgRoleCredential.username) {
    matchedRoleText = "mfg";
  } else if (username == adminRoleCredential.username) {
    matchedRoleText = "admin";
  } else if (username == maintenanceRoleCredential.username) {
    matchedRoleText = "maintenance";
  } else if (username == userRoleCredential.username) {
    matchedRoleText = "user";
  }

  const maintenanceRole role = resolveRoleByCredentials(username, password);
  if (role == maintenanceRole::kNone) {
    appLogWarn("handleLoginApi failed. remoteIp=%s requestBodyLength=%ld username=%s usernameLength=%ld passwordLength=%ld matchedRole=%s apSsid=%s",
               remoteIpText.c_str(),
               static_cast<long>(requestBody.length()),
               username.c_str(),
               static_cast<long>(username.length()),
               static_cast<long>(password.length()),
               matchedRoleText.c_str(),
               currentApSsid.c_str());
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"authentication failed\"}");
    return;
  }

  activeRole = role;
  activeToken = String("ap-token-") + String(millis()) + "-" + String(static_cast<unsigned long>(esp_random()));
  const String roleText = toRoleText(role);
  appLogWarn("handleLoginApi success. remoteIp=%s requestBodyLength=%ld username=%s role=%s apSsid=%s",
             remoteIpText.c_str(),
             static_cast<long>(requestBody.length()),
             username.c_str(),
             roleText.c_str(),
             currentApSsid.c_str());
  const String responseText = String("{\"result\":\"OK\",\"token\":\"") + activeToken + "\",\"role\":\"" + toRoleText(role) + "\"}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

/**
 * @brief APロールパスワードを変更するAPI。
 * @details
 * - [重要] `admin` は `user/maintenance/admin` の変更を許可し、`mfg` の変更は `mfg` ロールのみ許可する。
 * - [厳守] 変更には対象ロールの `currentPassword` 一致を必須とする。
 * - [厳守] 保存成功時のみNVSへ永続化し、平文パスワードはログへ出力しない。
 */
void handleAuthPasswordChangeApi() {
  if (!isAuthorized(maintenanceRole::kAdmin)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"unauthorized\"}");
    return;
  }
  const String requestBody = maintenanceWebServer.arg("plain");
  String roleText;
  String currentPassword;
  String newPassword;
  String reason;
  parseBodyStringValue(requestBody, "role", &roleText);
  parseBodyStringValue(requestBody, "currentPassword", &currentPassword);
  parseBodyStringValue(requestBody, "newPassword", &newPassword);
  parseBodyStringValue(requestBody, "reason", &reason);
  roleText.trim();
  currentPassword.trim();
  newPassword.trim();
  reason.trim();
  const maintenanceRole targetRole = parseRoleText(roleText);
  if (targetRole == maintenanceRole::kNone) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"invalid role\"}");
    return;
  }
  if (targetRole == maintenanceRole::kMfg && !isAuthorized(maintenanceRole::kMfg)) {
    maintenanceWebServer.send(403, "application/json", "{\"result\":\"NG\",\"detail\":\"mfg role required for mfg password change\"}");
    return;
  }
  if (currentPassword.length() == 0 || newPassword.length() == 0) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"currentPassword/newPassword is required\"}");
    return;
  }
  if (newPassword.length() < 8) {
    maintenanceWebServer.send(400, "application/json", "{\"result\":\"NG\",\"detail\":\"newPassword must be at least 8 characters\"}");
    return;
  }
  roleCredential* targetCredential = getCredentialByRole(targetRole);
  if (targetCredential == nullptr) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"target role credential is null\"}");
    return;
  }
  if (targetCredential->password != currentPassword) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"currentPassword mismatch\"}");
    return;
  }
  const String previousPassword = targetCredential->password;
  targetCredential->password = newPassword;
  if (!saveRolePasswordToPreferences(targetRole, newPassword)) {
    targetCredential->password = previousPassword;
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"password persistence failed\"}");
    return;
  }
  appLogWarn("audit.apPasswordChanged role=%s changedBy=%s reason=%s",
             toRoleText(targetRole).c_str(),
             toRoleText(activeRole).c_str(),
             reason.length() > 0 ? reason.c_str() : "(empty)");
  String responseText = "{\"result\":\"OK\",\"role\":\"" + toRoleText(targetRole) + "\"}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

void handleNetworkSettingsApi() {
  if (!isAuthorized(maintenanceRole::kMaintenance)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"unauthorized\"}");
    return;
  }
  if (sensitiveDataServiceInstance == nullptr) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"sensitiveDataService is null\"}");
    return;
  }

  const String requestBody = maintenanceWebServer.arg("plain");
  String wifiSsid;
  String wifiPass;
  String mqttUrl;
  String mqttUrlName;
  String mqttUser;
  String mqttPass;
  String mqttTlsCaCertPem;
  String mqttTlsCertIssueNo;
  String mqttTlsCertSetAt;
  String serverUrl;
  String serverUrlName;
  String serverUser;
  String serverPass;
  String otaUrl;
  String otaUrlName;
  String otaUser;
  String otaPass;
  String timeServerUrl;
  String timeServerUrlName;
  String keyDevice;
  long mqttPort = 8883;
  long serverPort = 443;
  long otaPort = 443;
  long timeServerPort = 123;
  bool mqttTls = true;
  bool serverTls = true;
  bool otaTls = true;
  bool timeServerTls = false;
  parseBodyStringValue(requestBody, "wifiSsid", &wifiSsid);
  parseBodyStringValue(requestBody, "wifiPass", &wifiPass);
  parseBodyStringValue(requestBody, "mqttUrl", &mqttUrl);
  parseBodyStringValue(requestBody, "mqttUrlName", &mqttUrlName);
  parseBodyStringValue(requestBody, "mqttUser", &mqttUser);
  parseBodyStringValue(requestBody, "mqttPass", &mqttPass);
  parseBodyStringValue(requestBody, "mqttTlsCaCertPem", &mqttTlsCaCertPem);
  parseBodyStringValue(requestBody, "mqttTlsCertIssueNo", &mqttTlsCertIssueNo);
  parseBodyStringValue(requestBody, "mqttTlsCertSetAt", &mqttTlsCertSetAt);
  parseBodyStringValue(requestBody, "serverUrl", &serverUrl);
  parseBodyStringValue(requestBody, "serverUrlName", &serverUrlName);
  parseBodyStringValue(requestBody, "serverUser", &serverUser);
  parseBodyStringValue(requestBody, "serverPass", &serverPass);
  parseBodyStringValue(requestBody, "otaUrl", &otaUrl);
  parseBodyStringValue(requestBody, "otaUrlName", &otaUrlName);
  parseBodyStringValue(requestBody, "otaUser", &otaUser);
  parseBodyStringValue(requestBody, "otaPass", &otaPass);
  parseBodyStringValue(requestBody, "timeServerUrl", &timeServerUrl);
  parseBodyStringValue(requestBody, "timeServerUrlName", &timeServerUrlName);
  parseBodyLongValue(requestBody, "mqttPort", &mqttPort);
  parseBodyLongValue(requestBody, "serverPort", &serverPort);
  parseBodyLongValue(requestBody, "otaPort", &otaPort);
  parseBodyLongValue(requestBody, "timeServerPort", &timeServerPort);
  parseBodyBoolValue(requestBody, "mqttTls", &mqttTls);
  parseBodyBoolValue(requestBody, "serverTls", &serverTls);
  parseBodyBoolValue(requestBody, "otaTls", &otaTls);
  parseBodyBoolValue(requestBody, "timeServerTls", &timeServerTls);
  parseBodyStringValue(requestBody, "keyDevice", &keyDevice);

  bool saveResult = true;
  if (wifiSsid.length() > 0) {
    saveResult = saveResult && sensitiveDataServiceInstance->saveWifiCredentials(wifiSsid, wifiPass);
  }
  if (mqttUrl.length() > 0 || mqttUser.length() > 0 || mqttPass.length() > 0) {
    saveResult = saveResult && sensitiveDataServiceInstance->saveMqttConfig(mqttUrl, mqttUrlName, mqttUser, mqttPass, static_cast<int32_t>(mqttPort), mqttTls);
  }
  if (mqttTlsCaCertPem.length() > 0 || mqttTlsCertIssueNo.length() > 0 || mqttTlsCertSetAt.length() > 0) {
    saveResult = saveResult && sensitiveDataServiceInstance->saveMqttTlsCertificate(mqttTlsCaCertPem, mqttTlsCertIssueNo, mqttTlsCertSetAt);
  }
  if (serverUrl.length() > 0 || serverUser.length() > 0 || serverPass.length() > 0) {
    saveResult = saveResult && sensitiveDataServiceInstance->saveServerConfig(serverUrl, serverUrlName, serverUser, serverPass, static_cast<int32_t>(serverPort), serverTls);
  }
  if (otaUrl.length() > 0 || otaUser.length() > 0 || otaPass.length() > 0) {
    saveResult = saveResult && sensitiveDataServiceInstance->saveOtaConfig(otaUrl, otaUrlName, otaUser, otaPass, static_cast<int32_t>(otaPort), otaTls);
  }
  if (timeServerUrl.length() > 0) {
    saveResult = saveResult && sensitiveDataServiceInstance->saveTimeServerConfig(timeServerUrl, timeServerUrlName, static_cast<int32_t>(timeServerPort), timeServerTls);
  }
  if (keyDevice.length() > 0) {
    if (!isAuthorized(maintenanceRole::kAdmin)) {
      maintenanceWebServer.send(403, "application/json", "{\"result\":\"NG\",\"detail\":\"admin role required for keyDevice\"}");
      return;
    }
    saveResult = saveResult && sensitiveDataServiceInstance->saveKeyDevice(keyDevice);
  }

  if (!saveResult) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"save failed\"}");
    return;
  }
  maintenanceWebServer.send(200, "application/json", "{\"result\":\"OK\",\"rebootRequired\":true}");
}

void handleNetworkSettingsGetApi() {
  if (!isAuthorized(maintenanceRole::kMaintenance)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"unauthorized\"}");
    return;
  }
  if (sensitiveDataServiceInstance == nullptr) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"sensitiveDataService is null\"}");
    return;
  }
  String wifiSsid;
  String wifiPass;
  String mqttUrl;
  String mqttUrlName;
  String mqttUser;
  String mqttPass;
  int32_t mqttPort = 8883;
  bool mqttTls = true;
  String mqttTlsCaCertPem;
  String mqttTlsCertIssueNo;
  String mqttTlsCertSetAt;
  String serverUrl;
  String serverUrlName;
  String serverUser;
  String serverPass;
  int32_t serverPort = 443;
  bool serverTls = true;
  String otaUrl;
  String otaUrlName;
  String otaUser;
  String otaPass;
  int32_t otaPort = 443;
  bool otaTls = true;
  String timeServerUrl;
  String timeServerUrlName;
  int32_t timeServerPort = 123;
  bool timeServerTls = false;
  String keyDevice;

  const bool loadResult =
      sensitiveDataServiceInstance->loadWifiCredentials(&wifiSsid, &wifiPass) &&
      sensitiveDataServiceInstance->loadMqttConfig(&mqttUrl, &mqttUrlName, &mqttUser, &mqttPass, &mqttPort, &mqttTls) &&
      sensitiveDataServiceInstance->loadMqttTlsCertificate(&mqttTlsCaCertPem, &mqttTlsCertIssueNo, &mqttTlsCertSetAt) &&
      sensitiveDataServiceInstance->loadServerConfig(&serverUrl, &serverUrlName, &serverUser, &serverPass, &serverPort, &serverTls) &&
      sensitiveDataServiceInstance->loadOtaConfig(&otaUrl, &otaUrlName, &otaUser, &otaPass, &otaPort, &otaTls) &&
      sensitiveDataServiceInstance->loadTimeServerConfig(&timeServerUrl, &timeServerUrlName, &timeServerPort, &timeServerTls) &&
      sensitiveDataServiceInstance->loadKeyDevice(&keyDevice);
  if (!loadResult) {
    maintenanceWebServer.send(500, "application/json", "{\"result\":\"NG\",\"detail\":\"load failed\"}");
    return;
  }

  String responseText;
  responseText.reserve(1024);
  responseText += "{";
  responseText += "\"result\":\"OK\",";
  responseText += "\"wifiSsid\":\"" + toJsonSafeText(wifiSsid) + "\",";
  responseText += "\"wifiPass\":\"" + toJsonSafeText(wifiPass) + "\",";
  responseText += "\"mqttUrl\":\"" + toJsonSafeText(mqttUrl) + "\",";
  responseText += "\"mqttUrlName\":\"" + toJsonSafeText(mqttUrlName) + "\",";
  responseText += "\"mqttUser\":\"" + toJsonSafeText(mqttUser) + "\",";
  responseText += "\"mqttPass\":\"" + toJsonSafeText(mqttPass) + "\",";
  responseText += "\"mqttPort\":" + String(static_cast<long>(mqttPort)) + ",";
  responseText += "\"mqttTls\":" + String(mqttTls ? "true" : "false") + ",";
  responseText += "\"mqttTlsCertExists\":" + String(mqttTlsCaCertPem.length() > 0 ? "true" : "false") + ",";
  responseText += "\"mqttTlsCertIssueNo\":\"" + toJsonSafeText(mqttTlsCertIssueNo) + "\",";
  responseText += "\"mqttTlsCertSetAt\":\"" + toJsonSafeText(mqttTlsCertSetAt) + "\",";
  responseText += "\"serverUrl\":\"" + toJsonSafeText(serverUrl) + "\",";
  responseText += "\"serverUrlName\":\"" + toJsonSafeText(serverUrlName) + "\",";
  responseText += "\"serverUser\":\"" + toJsonSafeText(serverUser) + "\",";
  responseText += "\"serverPass\":\"" + toJsonSafeText(serverPass) + "\",";
  responseText += "\"serverPort\":" + String(static_cast<long>(serverPort)) + ",";
  responseText += "\"serverTls\":" + String(serverTls ? "true" : "false") + ",";
  responseText += "\"otaUrl\":\"" + toJsonSafeText(otaUrl) + "\",";
  responseText += "\"otaUrlName\":\"" + toJsonSafeText(otaUrlName) + "\",";
  responseText += "\"otaUser\":\"" + toJsonSafeText(otaUser) + "\",";
  responseText += "\"otaPass\":\"" + toJsonSafeText(otaPass) + "\",";
  responseText += "\"otaPort\":" + String(static_cast<long>(otaPort)) + ",";
  responseText += "\"otaTls\":" + String(otaTls ? "true" : "false") + ",";
  responseText += "\"timeServerUrl\":\"" + toJsonSafeText(timeServerUrl) + "\",";
  responseText += "\"timeServerUrlName\":\"" + toJsonSafeText(timeServerUrlName) + "\",";
  responseText += "\"timeServerPort\":" + String(static_cast<long>(timeServerPort)) + ",";
  responseText += "\"timeServerTls\":" + String(timeServerTls ? "true" : "false") + ",";
  responseText += "\"keyDevice\":\"" + toJsonSafeText(keyDevice) + "\"";
  responseText += "}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

void handleRebootApi() {
  if (!isAuthorized(maintenanceRole::kMaintenance)) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"unauthorized\"}");
    return;
  }
  rebootScheduled = true;
  rebootScheduledAtMs = millis();
  appLogWarn("handleRebootApi accepted. reboot scheduled. delayMs=%lu activeRole=%s tokenSet=%d apSsid=%s",
             static_cast<unsigned long>(rebootDelayMs),
             toRoleText(activeRole).c_str(),
             static_cast<int>(activeToken.length() > 0),
             currentApSsid.c_str());
  maintenanceWebServer.send(200, "application/json", "{\"result\":\"OK\",\"detail\":\"reboot scheduled\"}");
}

void handleRootPage() {
  const String htmlText =
      "<html><head><meta charset='UTF-8'><title>Maintenance AP</title></head>"
      "<body><h1>Maintenance AP</h1><p>Use LocalServer admin API.</p><p>firmwareOperationMode=" +
      String(firmwareMode::kFirmwareOperationMode) + " / factoryApisEnabled=" +
      String(firmwareMode::kFactoryApisEnabled ? "true" : "false") + "</p></body></html>";
  maintenanceWebServer.send(200, "text/html", htmlText);
}

}  // namespace

namespace maintenanceApServer {

bool start(const String& apSsid, sensitiveDataService* sensitiveDataServiceOut) {
  if (isServerStarted) {
    return true;
  }
  if (sensitiveDataServiceOut == nullptr) {
    appLogError("maintenanceApServer::start failed. sensitiveDataServiceOut is null.");
    return false;
  }
  sensitiveDataServiceInstance = sensitiveDataServiceOut;
  currentApSsid = apSsid;
  loadRolePasswordsFromPreferences();

  maintenanceWebServer.on("/", HTTP_GET, handleRootPage);
  maintenanceWebServer.on("/api/health", HTTP_GET, handleHealthApi);
  maintenanceWebServer.on("/api/auth/login", HTTP_POST, handleLoginApi);
  maintenanceWebServer.on("/api/auth/password/change", HTTP_POST, handleAuthPasswordChangeApi);
  maintenanceWebServer.on("/api/pairing/session", HTTP_POST, handlePairingSessionApi);
  maintenanceWebServer.on("/api/pairing/bundle-summary", HTTP_POST, handlePairingBundleSummaryApi);
  maintenanceWebServer.on("/api/pairing/transport-session", HTTP_POST, handlePairingTransportSessionApi);
  maintenanceWebServer.on("/api/pairing/transport-handshake", HTTP_POST, handlePairingTransportHandshakeApi);
  maintenanceWebServer.on("/api/pairing/secure-bundle", HTTP_POST, handlePairingSecureBundleApplyApi);
  maintenanceWebServer.on("/api/production/precheck", HTTP_POST, handleProductionPrecheckApi);
  maintenanceWebServer.on("/api/production/state", HTTP_GET, handleProductionStateApi);
  maintenanceWebServer.on("/api/settings/network", HTTP_GET, handleNetworkSettingsGetApi);
  maintenanceWebServer.on("/api/settings/network", HTTP_POST, handleNetworkSettingsApi);
  maintenanceWebServer.on("/api/pairing/state", HTTP_GET, handlePairingStateApi);
  maintenanceWebServer.on("/api/files/upsert", HTTP_POST, handleManagedFileUpsertApi);
  maintenanceWebServer.on("/api/files/delete", HTTP_POST, handleManagedFileDeleteApi);
  maintenanceWebServer.on("/api/system/reboot", HTTP_POST, handleRebootApi);
  maintenanceWebServer.begin();
  isServerStarted = true;
  logMaintenanceApRuntimeSnapshot("maintenanceApServer::start");
  appLogWarn("maintenanceApServer::start success. firmwareOperationMode=%s serialOutputMode=%s factoryApisEnabled=%d",
             firmwareMode::kFirmwareOperationMode,
             firmwareMode::kSerialOutputMode,
             static_cast<int>(firmwareMode::kFactoryApisEnabled));
  return true;
}

void loopOnce() {
  if (!isServerStarted) {
    return;
  }
  maintenanceWebServer.handleClient();
  if (rebootScheduled) {
    uint32_t elapsedMs = millis() - rebootScheduledAtMs;
    if (elapsedMs >= rebootDelayMs) {
      appLogWarn("maintenanceApServer::loopOnce executing reboot. elapsedMs=%lu",
                 static_cast<unsigned long>(elapsedMs));
      delay(50);
      ESP.restart();
    }
  }
}

}  // namespace maintenanceApServer

