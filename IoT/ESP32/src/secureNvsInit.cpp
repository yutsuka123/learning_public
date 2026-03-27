/**
 * @file secureNvsInit.cpp
 * @brief NVS Encryption (HMAC方式) の暗号化初期化実装。
 * @details
 * - [重要][2026-03-26] 工程C（本番セキュア化出荷準備試験計画書.md 5.6c）の実装。
 * - [修正理由][2026-03-26] sdkconfig で CONFIG_NVS_ENCRYPTION=y + HMAC_EFUSE_KEY_ID=2 を
 *   有効化済みの FW が、eFuse BLOCK_KEY2 に HMAC 鍵投入後も Preferences::begin() で
 *   nvs_open failed: NOT_FOUND を返す問題の対策。
 *   原因: Arduino の Preferences は内部で nvs_flash_init()（平文）を呼ぶが、
 *   NVS Encryption 有効時は先に nvs_flash_secure_init() を呼ぶ必要がある。
 *   この関数を setup() 冒頭で呼ぶことで、以降の Preferences::begin() が暗号化 NVS を
 *   透過的に利用できるようになる。
 *
 * - [厳守][2026-03-26] 以下の3パターンすべてで正常起動すること:
 *   パターンA: CONFIG_NVS_ENCRYPTION=n（セキュア化前のビルド）→ 平文NVSで起動
 *   パターンB: CONFIG_NVS_ENCRYPTION=y + eFuse HMAC鍵未投入 → 平文NVSフォールバックで起動
 *   パターンC: CONFIG_NVS_ENCRYPTION=y + eFuse HMAC鍵投入済み → 暗号化NVSで起動
 *   どのパターンでも return false で起動を停止させない設計とする。
 */

#include "../header/secureNvsInit.h"

#include <nvs_flash.h>
#include <esp_err.h>

#include "log.h"

#ifdef CONFIG_NVS_ENCRYPTION
#include <nvs_sec_provider.h>
#endif

namespace secureNvsInit {

/**
 * @brief nvs_flash_init()（平文初期化）を呼ぶ共通ヘルパー。
 * @details NO_FREE_PAGES / NEW_VERSION_FOUND の場合は erase して再初期化する。
 * @param functionName 呼び出し元関数名（ログ用）。
 * @return 成功時 true。
 */
static bool fallbackPlaintextNvsInit(const char* functionName) {
  esp_err_t initResult = nvs_flash_init();
  if (initResult == ESP_ERR_NVS_NO_FREE_PAGES || initResult == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    appLogWarn("%s: plaintext NVS needs erase. erasing and re-initializing. reason=0x%x",
               functionName, static_cast<unsigned>(initResult));
    nvs_flash_erase();
    initResult = nvs_flash_init();
  }
  if (initResult != ESP_OK) {
    appLogError("%s: nvs_flash_init (plaintext fallback) failed. error=0x%x (%s)",
                functionName,
                static_cast<unsigned>(initResult),
                esp_err_to_name(initResult));
    return false;
  }
  appLogInfo("%s: plaintext NVS initialized successfully (fallback).", functionName);
  return true;
}

bool initializeSecureNvs() {
  constexpr const char* functionName = "secureNvsInit::initializeSecureNvs";

#ifdef CONFIG_NVS_ENCRYPTION
  appLogInfo("%s: NVS Encryption enabled in sdkconfig. attempting secure NVS init (HMAC method).", functionName);

  nvs_sec_cfg_t securityConfig;
  nvs_sec_scheme_t* securityScheme = nullptr;

  esp_err_t schemeResult = nvs_sec_provider_register_hmac(NULL, &securityScheme);
  if (schemeResult != ESP_OK) {
    // [パターンB相当] HMAC プロバイダ登録自体が失敗。平文フォールバックで起動継続。
    appLogWarn("%s: nvs_sec_provider_register_hmac failed (0x%x: %s). fallback to plaintext NVS.",
              functionName,
              static_cast<unsigned>(schemeResult),
              esp_err_to_name(schemeResult));
    return fallbackPlaintextNvsInit(functionName);
  }

  esp_err_t cfgResult = nvs_flash_read_security_cfg_v2(securityScheme, &securityConfig);
  if (cfgResult == ESP_ERR_NVS_SEC_HMAC_KEY_NOT_FOUND) {
    // [パターンB] HMAC 鍵が eFuse に未投入。平文フォールバックで起動継続。
    appLogWarn("%s: HMAC key not found in eFuse. fallback to plaintext NVS.", functionName);
    return fallbackPlaintextNvsInit(functionName);
  }
  if (cfgResult != ESP_OK) {
    // 鍵導出の予期しないエラー。平文フォールバックで起動継続。
    appLogWarn("%s: nvs_flash_read_security_cfg_v2 failed (0x%x: %s). fallback to plaintext NVS.",
              functionName,
              static_cast<unsigned>(cfgResult),
              esp_err_to_name(cfgResult));
    return fallbackPlaintextNvsInit(functionName);
  }

  // [パターンC] HMAC 鍵あり。暗号化 NVS を初期化する。
  esp_err_t initResult = nvs_flash_secure_init(&securityConfig);
  if (initResult == ESP_ERR_NVS_NO_FREE_PAGES || initResult == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    appLogWarn("%s: secure NVS partition needs erase. erasing and re-initializing. reason=0x%x",
               functionName, static_cast<unsigned>(initResult));
    esp_err_t eraseResult = nvs_flash_erase();
    if (eraseResult != ESP_OK) {
      appLogWarn("%s: nvs_flash_erase failed (0x%x). fallback to plaintext NVS.",
                functionName, static_cast<unsigned>(eraseResult));
      return fallbackPlaintextNvsInit(functionName);
    }
    initResult = nvs_flash_secure_init(&securityConfig);
  }

  if (initResult != ESP_OK) {
    // 暗号化初期化に最終的に失敗。平文フォールバックで起動継続。
    appLogWarn("%s: nvs_flash_secure_init failed (0x%x: %s). fallback to plaintext NVS.",
              functionName,
              static_cast<unsigned>(initResult),
              esp_err_to_name(initResult));
    return fallbackPlaintextNvsInit(functionName);
  }

  appLogInfo("%s: secure NVS initialized successfully (HMAC method, patternC).", functionName);
  return true;

#else
  // [パターンA] NVS Encryption が sdkconfig で無効。平文 NVS で初期化。
  appLogInfo("%s: NVS Encryption not enabled in sdkconfig (patternA). initializing plaintext NVS.", functionName);
  return fallbackPlaintextNvsInit(functionName);
#endif
}

}  // namespace secureNvsInit
