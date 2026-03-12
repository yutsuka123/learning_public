/**
 * @file otaRollback.h
 * @brief OTA rollback 試験と起動確定処理を管理する。
 * @details
 * - [重要] rollback 有効時は、新面起動後に `mark app valid` を実行して確定する。
 * - [厳守] 試験専用の未確定起動失敗モードは通常運用で無効のままにする。
 * - [禁止] 本モジュールを使ってセキュリティ検証を恒久的に無効化しない。
 */

#pragma once

namespace otaRollback {

/**
 * @brief 起動直後に rollback 状態を初期化する。
 * @details
 * - [重要] OTA適用直後かどうかを判定し、試験用再起動シナリオを制御する。
 * - [重要] 通常運用ではこの関数だけでは確定しない。`confirmCurrentAppIfNeeded()` が必要。
 * @return 処理継続可能ならtrue。
 */
bool initializeOnBoot();

/**
 * @brief 現在起動中アプリを有効確定する。
 * @details
 * - [厳守] 通常運用では起動安定後に必ず呼び出す。
 * - [重要] rollback が無効な環境では安全に no-op で成功扱いにする。
 * @return 確定成功時true。
 */
bool confirmCurrentAppIfNeeded();

/**
 * @brief 試験専用の「未確定起動失敗」再現を有効にする。
 * @details
 * - [重要] 次回OTA後の初回起動で確定せず再起動させ、rollback復帰を誘発する。
 * - [禁止] 通常運用で常時有効にしない。
 * @return 有効化成功時true。
 */
bool enableRollbackFailureTestMode();

/**
 * @brief 試験専用の「未確定起動失敗」再現を無効にする。
 * @return 無効化成功時true。
 */
bool disableRollbackFailureTestMode();

/**
 * @brief 未確定起動失敗再現モードの現在値を返す。
 * @return 有効ならtrue。
 */
bool isRollbackFailureTestModeEnabled();

/**
 * @brief 起動直後に試験専用の強制再起動を実行したかを返す。
 * @return 実行済みならtrue。
 */
bool hasTriggeredFailureRestartThisBoot();

}  // namespace otaRollback
