/**
 * @file firmwareMode.h
 * @brief 通常運用FWと診断用FWの動作差分を定義する。
 * @details
 * - [重要] シリアル出力制約と製造系API公開可否をビルド時定数で固定する。
 * - [厳守] 通常運用FWでは高詳細ログと製造系APIを既定で無効化する。
 * - [厳守] 診断用FWでは問題解析に必要な範囲だけ高詳細ログと製造系APIを有効化する。
 * - [禁止] 実行中コマンドで通常運用FWと診断用FWのモードを切り替えない。
 */

#pragma once

#ifndef APP_ENABLE_DIAGNOSTIC_LOG
#define APP_ENABLE_DIAGNOSTIC_LOG 1
#endif

#ifndef APP_ENABLE_FACTORY_APIS
#define APP_ENABLE_FACTORY_APIS 1
#endif

namespace firmwareMode {

/**
 * @brief 高詳細シリアルログを有効化するかどうか。
 * @type bool
 * @details
 * - `true`: 診断用FWとして DEBUG/INFO を利用できる。
 * - `false`: 通常運用FWとして WARN/ERROR を中心に運用する。
 */
constexpr bool kDiagnosticLogEnabled = (APP_ENABLE_DIAGNOSTIC_LOG != 0);

/**
 * @brief 製造・ペアリング系APIを有効化するかどうか。
 * @type bool
 * @details
 * - `true`: 診断用FWまたは工場向けFWとして pairing / production API を公開する。
 * - `false`: 通常運用FWとして危険な製造系APIを公開しない。
 */
constexpr bool kFactoryApisEnabled = (APP_ENABLE_FACTORY_APIS != 0);

/**
 * @brief ファームウェア運用モード名。
 * @type const char*
 */
constexpr const char* kFirmwareOperationMode = kDiagnosticLogEnabled ? "diagnostic" : "normal";

/**
 * @brief シリアル出力モード名。
 * @type const char*
 */
constexpr const char* kSerialOutputMode = kDiagnosticLogEnabled ? "diagnostic" : "restricted";

}  // namespace firmwareMode
