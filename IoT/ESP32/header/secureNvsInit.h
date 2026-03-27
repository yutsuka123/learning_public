/**
 * @file secureNvsInit.h
 * @brief NVS Encryption (HMAC方式) の暗号化初期化を行うモジュール。
 * @details
 * - [重要][2026-03-26] 工程C（本番セキュア化出荷準備試験計画書.md 5.6c）の実装。
 * - [重要] sdkconfig で CONFIG_NVS_ENCRYPTION=y が有効な場合、Preferences::begin() より
 *   前に nvs_flash_secure_init() を呼ばなければ NVS namespace が開けない。
 * - [厳守][2026-03-26] 以下の3パターンすべてで正常起動すること:
 *   パターンA: CONFIG_NVS_ENCRYPTION=n → nvs_flash_init()（平文）で起動
 *   パターンB: CONFIG_NVS_ENCRYPTION=y + HMAC鍵未投入 → nvs_flash_init()（平文フォールバック）で起動
 *   パターンC: CONFIG_NVS_ENCRYPTION=y + HMAC鍵投入済み → nvs_flash_secure_init()（暗号化）で起動
 * - [厳守] この関数は setup() の最初期（filesystemModule.initialize() の後、
 *   sensitiveDataModule.initialize() の前）に1回だけ呼ぶこと。
 */

#ifndef SECURE_NVS_INIT_H
#define SECURE_NVS_INIT_H

namespace secureNvsInit {

/**
 * @brief NVS の初期化を実行する（暗号化/平文を自動判定）。
 * @details
 * パターンA（CONFIG_NVS_ENCRYPTION 無効）:
 *   nvs_flash_init() で平文 NVS を初期化して true を返す。
 *
 * パターンB（CONFIG_NVS_ENCRYPTION 有効 + eFuse HMAC鍵なし）:
 *   HMAC 鍵導出に失敗するため nvs_flash_init() にフォールバックして true を返す。
 *
 * パターンC（CONFIG_NVS_ENCRYPTION 有効 + eFuse HMAC鍵あり）:
 *   nvs_flash_secure_init() で暗号化 NVS を初期化して true を返す。
 *
 * いずれのパターンでも NVS が利用可能な状態にして true を返す。
 * NVS 初期化に完全に失敗した場合のみ false を返すが、起動は停止しない想定。
 *
 * @return NVS 初期化成功時 true。
 */
bool initializeSecureNvs();

}  // namespace secureNvsInit

#endif  // SECURE_NVS_INIT_H
