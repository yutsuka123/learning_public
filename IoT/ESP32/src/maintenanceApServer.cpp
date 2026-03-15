/**
 * @file maintenanceApServer.cpp
 * @brief APメンテナンスモード用HTTPサーバー実装。
 * @details
 * - [重要] LocalServer からのログイン、ネットワーク設定投入、再起動要求を処理する。
 * - [厳守] ログイン成功後のトークンが一致する場合のみ設定更新を許可する。
 * - [禁止] 未認証・権限不足で `k-device` 更新を許可しない。
 */

#include "../header/maintenanceApServer.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <vector>

#include "jsonService.h"
#include "log.h"
#include "sensitiveData.h"
#include "sensitiveDataService.h"

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
  String responseText = "{\"result\":\"OK\",\"mode\":\"maintenance-ap\",\"ssid\":\"" + currentApSsid + "\"}";
  maintenanceWebServer.send(200, "application/json", responseText);
}

void handleLoginApi() {
  const String requestBody = maintenanceWebServer.arg("plain");
  String username;
  String password;
  parseBodyStringValue(requestBody, "username", &username);
  parseBodyStringValue(requestBody, "password", &password);
  username.trim();
  password.trim();
  const maintenanceRole role = resolveRoleByCredentials(username, password);
  if (role == maintenanceRole::kNone) {
    maintenanceWebServer.send(401, "application/json", "{\"result\":\"NG\",\"detail\":\"authentication failed\"}");
    return;
  }

  activeRole = role;
  activeToken = String("ap-token-") + String(millis()) + "-" + String(static_cast<unsigned long>(esp_random()));
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
  appLogWarn("handleRebootApi accepted. reboot scheduled. delayMs=%lu",
             static_cast<unsigned long>(rebootDelayMs));
  maintenanceWebServer.send(200, "application/json", "{\"result\":\"OK\",\"detail\":\"reboot scheduled\"}");
}

void handleRootPage() {
  const String htmlText =
      "<html><head><meta charset='UTF-8'><title>Maintenance AP</title></head>"
      "<body><h1>Maintenance AP</h1><p>Use LocalServer admin API.</p></body></html>";
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
  maintenanceWebServer.on("/api/settings/network", HTTP_GET, handleNetworkSettingsGetApi);
  maintenanceWebServer.on("/api/settings/network", HTTP_POST, handleNetworkSettingsApi);
  maintenanceWebServer.on("/api/files/upsert", HTTP_POST, handleManagedFileUpsertApi);
  maintenanceWebServer.on("/api/files/delete", HTTP_POST, handleManagedFileDeleteApi);
  maintenanceWebServer.on("/api/system/reboot", HTTP_POST, handleRebootApi);
  maintenanceWebServer.begin();
  isServerStarted = true;
  appLogWarn("maintenanceApServer::start success. AP maintenance API enabled.");
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

