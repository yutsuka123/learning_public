/**
 * @file secureNvsInit.h
 * @brief NVS Encryption (HMAC方式) の暗号化初期化を行うモジュール。
 * @details
 * - [重要][2026-03-26] 工程C（本番セキュア化出荷準備試験計画書.md 5.6c）の実装。
 * - [重要] Preferences::begin() より前に NVS 初期化を完了させる。
 * - [重要][2026-04-04] `CONFIG_NVS_ENCRYPTION` の C プリプロセッサ見え方に依存せず、
 *   secure NVS 初期化を先に試し、build 方針に応じて fallback 可否を切り替える。
 * - [厳守][2026-03-26] 以下の3パターンすべてで正常起動すること:
 *   パターンA: CONFIG_NVS_ENCRYPTION=n → nvs_flash_init()（平文）で起動
 *   パターンB: CONFIG_NVS_ENCRYPTION=y + HMAC鍵未投入 → nvs_flash_init()（平文フォールバック）で起動
 *   パターンC: CONFIG_NVS_ENCRYPTION=y + HMAC鍵投入済み → nvs_flash_secure_init()（暗号化）で起動
 * - [重要][2026-04-04] build 方針:
 *   `APP_NVS_TRY_SECURE_INIT_FIRST=1` のとき secure 初期化を先に試す。
 *   `APP_NVS_ALLOW_PLAINTEXT_FALLBACK=1` のとき secure 失敗時に平文 fallback を許可する。
 *   `APP_NVS_ALLOW_PLAINTEXT_FALLBACK=0` のとき secure 失敗を `NG` として起動継続しない。
 * - [厳守] この関数は setup() の最初期（filesystemModule.initialize() の後、
 *   sensitiveDataModule.initialize() の前）に1回だけ呼ぶこと。
 */

#ifndef SECURE_NVS_INIT_H
#define SECURE_NVS_INIT_H

namespace secureNvsInit {

/**
 * @brief NVS の初期化を実行する（暗号化/平文を自動判定）。
 * @details
 * パターンA（secure 初期化を使わず平文経路へ入る build/runtime 条件）:
 *   nvs_flash_init() で平文 NVS を初期化して true を返す。
 *
 * パターンB（secure 初期化を試したが HMAC 鍵などの条件不足）:
 *   HMAC 鍵導出に失敗するため nvs_flash_init() にフォールバックして true を返す。
 *
 * パターンC（secure 初期化成功）:
 *   nvs_flash_secure_init() で暗号化 NVS を初期化して true を返す。
 *
 * いずれのパターンでも NVS が利用可能な状態にして true を返す。
 * ただし build 方針で plaintext fallback が禁止されている場合、
 * secure 初期化に失敗した時点で false を返す。
 *
 * @return NVS 初期化成功時 true。
 */
bool initializeSecureNvs();

/**
 * @brief secure NVS 初期化失敗の持続ログを必要時のみ再出力する。
 * @details
 * - [重要][2026-04-04] `esp32s3_secure_final` で secure NVS 初期化に失敗した場合、
 *   setup() は安全停止するため、起動後にログを見逃さないよう 10 秒ごとに同じ要点を再通知する。
 * - [重要] 失敗が未発生のときは何もしない。
 * - [推奨] Arduino `loop()` のような常駐軽量経路から呼び出す。
 */
void emitPersistentFailureLogIfNeeded();

}  // namespace secureNvsInit

#endif  // SECURE_NVS_INIT_H
