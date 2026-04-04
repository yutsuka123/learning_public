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
 *   開発ビルドでは起動継続を優先し、最終セキュアビルドでは secure 失敗時に停止する。
 * - [重要][2026-04-04] `CONFIG_NVS_ENCRYPTION` マクロの見え方に依存せず、
 *   secure NVS 初期化関数そのものを先に試す。
 * - [重要][2026-04-04] build 方針により plaintext fallback 可否を切り替える。
 *   開発ビルド: fallback 許可 / 最終セキュアビルド: fallback 禁止。
 */

#include "../header/secureNvsInit.h"

#include <esp_err.h>
#include <nvs_flash.h>

#include "log.h"

#ifndef APP_NVS_TRY_SECURE_INIT_FIRST
#define APP_NVS_TRY_SECURE_INIT_FIRST 1
#endif

#ifndef APP_NVS_ALLOW_PLAINTEXT_FALLBACK
#define APP_NVS_ALLOW_PLAINTEXT_FALLBACK 1
#endif

#if defined(__has_include)
#if __has_include(<nvs_sec_provider.h>)
#define APP_HAS_NVS_SEC_PROVIDER 1
#include <nvs_sec_provider.h>
#else
#define APP_HAS_NVS_SEC_PROVIDER 0
#endif
#else
#define APP_HAS_NVS_SEC_PROVIDER 0
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

/**
 * @brief secure NVS 初期化失敗時に、build 方針に応じて fallback または停止を決める。
 * @param functionName 呼び出し元関数名（ログ用）。
 * @return 平文 fallback を実施した場合はその結果。禁止時は false。
 */
static bool finalizePlaintextFallbackDecision(const char* functionName) {
#if APP_NVS_ALLOW_PLAINTEXT_FALLBACK
  appLogWarn("%s: plaintext fallback is allowed by build policy. continuing with non-secure NVS path.",
             functionName);
  return fallbackPlaintextNvsInit(functionName);
#else
  appLogError("%s: plaintext fallback is prohibited by build policy. startup must stop for this build.",
              functionName);
  return false;
#endif
}

bool initializeSecureNvs() {
  constexpr const char* functionName = "secureNvsInit::initializeSecureNvs";

  appLogInfo("%s: build policy secureInitFirst=%d, plaintextFallbackAllowed=%d, secureProviderAvailable=%d",
             functionName,
             APP_NVS_TRY_SECURE_INIT_FIRST,
             APP_NVS_ALLOW_PLAINTEXT_FALLBACK,
             APP_HAS_NVS_SEC_PROVIDER);

#if APP_NVS_TRY_SECURE_INIT_FIRST == 0
  appLogWarn("%s: secure init is disabled by build policy. using plaintext path (patternA).", functionName);
  return finalizePlaintextFallbackDecision(functionName);
#elif APP_HAS_NVS_SEC_PROVIDER == 0
  appLogWarn("%s: nvs_sec_provider.h is unavailable in this build environment. using plaintext path (patternA).",
             functionName);
  return finalizePlaintextFallbackDecision(functionName);
#else
  appLogInfo("%s: attempting secure NVS init first (HMAC method, patternC candidate).", functionName);

  nvs_sec_cfg_t securityConfig;
  nvs_sec_scheme_t* securityScheme = nullptr;

  esp_err_t schemeResult = nvs_sec_provider_register_hmac(NULL, &securityScheme);
  if (schemeResult != ESP_OK) {
    appLogWarn("%s: nvs_sec_provider_register_hmac failed (0x%x: %s). fallback to plaintext NVS.",
               functionName,
               static_cast<unsigned>(schemeResult),
               esp_err_to_name(schemeResult));
    return finalizePlaintextFallbackDecision(functionName);
  }

  esp_err_t cfgResult = nvs_flash_read_security_cfg_v2(securityScheme, &securityConfig);
  if (cfgResult == ESP_ERR_NVS_SEC_HMAC_KEY_NOT_FOUND) {
    appLogWarn("%s: HMAC key not found in eFuse. secure path is unavailable for this device state (patternB).",
               functionName);
    return finalizePlaintextFallbackDecision(functionName);
  }
  if (cfgResult != ESP_OK) {
    appLogWarn("%s: nvs_flash_read_security_cfg_v2 failed (0x%x: %s). fallback to plaintext NVS.",
               functionName,
               static_cast<unsigned>(cfgResult),
               esp_err_to_name(cfgResult));
    return finalizePlaintextFallbackDecision(functionName);
  }

  esp_err_t initResult = nvs_flash_secure_init(&securityConfig);
  if (initResult == ESP_ERR_NVS_NO_FREE_PAGES || initResult == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    appLogWarn("%s: secure NVS partition needs erase. erasing and re-initializing. reason=0x%x",
               functionName, static_cast<unsigned>(initResult));
    esp_err_t eraseResult = nvs_flash_erase();
    if (eraseResult != ESP_OK) {
      appLogWarn("%s: nvs_flash_erase failed (0x%x). fallback to plaintext NVS.",
                 functionName, static_cast<unsigned>(eraseResult));
      return finalizePlaintextFallbackDecision(functionName);
    }
    initResult = nvs_flash_secure_init(&securityConfig);
  }

  if (initResult != ESP_OK) {
    appLogWarn("%s: nvs_flash_secure_init failed (0x%x: %s). fallback to plaintext NVS.",
               functionName,
               static_cast<unsigned>(initResult),
               esp_err_to_name(initResult));
    return finalizePlaintextFallbackDecision(functionName);
  }

  appLogInfo("%s: secure NVS initialized successfully (HMAC method, patternC).", functionName);
  return true;
#endif
}

}  // namespace secureNvsInit
