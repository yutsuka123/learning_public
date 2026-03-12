/**
 * @file maintenanceMode.h
 * @brief メンテナンス(AP)モード遷移要求の永続化ヘルパー。
 * @details
 * - [重要] MQTT `call/maintenance` 受信時に「次回起動でAPモードへ入る」要求をNVSへ保存する。
 * - [厳守] 起動時は要求フラグを消費（読み出し後クリア）し、意図しない再突入を防ぐ。
 * - [制限] 本ヘルパーはフラグ保存のみを担当し、AP Web UIや認証処理は別モジュールで実装する。
 */

#pragma once

namespace maintenanceMode {

/**
 * @brief 次回起動でメンテナンスモードへ入る要求を保存する。
 * @return 保存成功時true、失敗時false。
 */
bool requestMaintenanceModeOnNextBoot();

/**
 * @brief メンテナンスモード要求を1回だけ消費する。
 * @return 要求が存在して消費できた場合true、未要求または失敗時false。
 */
bool consumeMaintenanceModeRequest();

}  // namespace maintenanceMode

