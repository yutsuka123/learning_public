/**
 * @file maintenanceApServer.h
 * @brief APメンテナンスモード用HTTPサーバー。
 * @details
 * - [重要] APモード中に LocalServer から設定投入を受け付ける。
 * - [厳守] 設定更新APIはログイン認証必須とする。
 * - [禁止] 未認証で Wi-Fi/MQTT/k-device 更新を許可しない。
 */

#pragma once

#include <Arduino.h>

class sensitiveDataService;

namespace maintenanceApServer {

/**
 * @brief APメンテナンスHTTPサーバーを開始する。
 * @param apSsid AP名（表示用）。
 * @param sensitiveDataServiceOut 設定保存サービス。
 * @return 開始成功時true、失敗時false。
 */
bool start(const String& apSsid, sensitiveDataService* sensitiveDataServiceOut);

/**
 * @brief APメンテナンスHTTPサーバーのイベント処理を1回実行する。
 */
void loopOnce();

}  // namespace maintenanceApServer

