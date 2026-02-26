/**
 * @file httpProtocol.h
 * @brief HTTPS APIのエンドポイントとJSON本文定義。
 * @details
 * - HTTPS経由で利用するAPIパスと本文形式を集約する。
 * - 本モジュールはパス生成と本文生成/簡易解析を担当する。
 * - 通信実行（HTTPクライアント送受信）は別モジュールが担当する。
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "common.h"

namespace httpProtocol {

/**
 * @brief HTTPS APIコマンド種別。
 */
enum class httpApiCommandType : uint16_t {
  kUnknown = 0,
  kHealth = 2001,
  kOtaPrepare = 2201,
  kOtaStart = 2202,
  kOtaProgress = 2203,
};

/**
 * @brief OTA開始要求の本文モデル。
 */
struct otaStartRequestModel {
  String transactionId;
  String firmwareVersion;
  String firmwareUrl;
  String firmwareSha256;
};

/**
 * @brief ヘルスチェックAPIパスを生成する。
 * @return APIパス。
 */
String buildPathHealth();

/**
 * @brief OTA準備APIパスを生成する。
 * @param publicId 公開用デバイスID。
 * @return APIパス。
 */
String buildPathOtaPrepare(const String& publicId);

/**
 * @brief OTA開始APIパスを生成する。
 * @param publicId 公開用デバイスID。
 * @return APIパス。
 */
String buildPathOtaStart(const String& publicId);

/**
 * @brief OTA進捗通知APIパスを生成する。
 * @param publicId 公開用デバイスID。
 * @return APIパス。
 */
String buildPathOtaProgress(const String& publicId);

/**
 * @brief OTA開始要求JSON本文を生成する。
 * @param model OTA開始要求モデル。
 * @return JSON文字列。
 */
String buildOtaStartRequestPayload(const otaStartRequestModel& model);

/**
 * @brief OTA開始要求JSON本文を解析する。
 * @param payload JSON文字列。
 * @param outModel 解析結果出力先。
 * @return 解析成功時true。
 */
bool parseOtaStartRequestPayload(const String& payload, otaStartRequestModel* outModel);

/**
 * @brief HTTPレスポンスJSONを生成する。
 * @param isSuccess 処理結果。
 * @param command APIコマンド。
 * @param message 補足メッセージ。
 * @return JSON文字列。
 */
String buildStandardResponsePayload(
    bool isSuccess, httpApiCommandType command, const String& message);

/**
 * @brief httpApiCommandType を文字列化する。
 * @param command APIコマンド。
 * @return コマンド名文字列。
 */
const char* toCommandName(httpApiCommandType command);

}  // namespace httpProtocol
