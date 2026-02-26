/**
 * @file common.h
 * @brief IoTシステム全体で共有する基本定義。
 * @details
 * - 本ファイルはESP/Local/Cloudで共通利用する前提の定義のみを配置する。
 * - 機密値（鍵、パスワード、証明書、個体識別子の生値）は定義しない。
 * - プロトコル名、バージョン、一般的なコマンド名など公開可能情報のみ扱う。
 */

#pragma once

#include <stdint.h>

namespace iotCommon {

/**
 * @brief プロトコルバージョン文字列型。
 */
using protocolVersionType = const char*;

/**
 * @brief 現在のIoTアプリケーションプロトコルバージョン。
 */
constexpr protocolVersionType kProtocolVersion = "1.0.0";

/**
 * @brief 共通で利用する通信チャネル種別。
 */
enum class transportChannelType : uint8_t {
  kMqtt = 1,
  kHttps = 2,
};

/**
 * @brief 共通コマンド識別子。
 * @details 値はログ・監査で扱いやすいよう文字列と1対1で管理する。
 */
enum class commandType : uint16_t {
  kUnknown = 0,
  kDeviceBootNotify = 1001,
  kLedSet = 1002,
  kWifiConfigUpdate = 1101,
  kWifiConfigConfirm = 1102,
  kOtaPrepare = 1201,
  kOtaStart = 1202,
  kOtaProgress = 1203,
};

/**
 * @brief デバイス稼働状態。
 */
enum class deviceRuntimeStateType : uint8_t {
  kInit = 0,
  kNormal = 1,
  kRecoveryAp = 2,
  kOta = 3,
  kError = 255,
};

/**
 * @brief commandType を文字列化する。
 * @param command コマンド識別子。
 * @return コマンド名文字列。
 */
inline const char* toCommandName(commandType command) {
  switch (command) {
    case commandType::kDeviceBootNotify:
      return "deviceBootNotify";
    case commandType::kLedSet:
      return "ledSet";
    case commandType::kWifiConfigUpdate:
      return "wifiConfigUpdate";
    case commandType::kWifiConfigConfirm:
      return "wifiConfigConfirm";
    case commandType::kOtaPrepare:
      return "otaPrepare";
    case commandType::kOtaStart:
      return "otaStart";
    case commandType::kOtaProgress:
      return "otaProgress";
    default:
      return "unknown";
  }
}

/**
 * @brief deviceRuntimeStateType を文字列化する。
 * @param state デバイス状態。
 * @return 状態名文字列。
 */
inline const char* toDeviceRuntimeStateName(deviceRuntimeStateType state) {
  switch (state) {
    case deviceRuntimeStateType::kInit:
      return "init";
    case deviceRuntimeStateType::kNormal:
      return "normal";
    case deviceRuntimeStateType::kRecoveryAp:
      return "recoveryAp";
    case deviceRuntimeStateType::kOta:
      return "ota";
    case deviceRuntimeStateType::kError:
      return "error";
    default:
      return "unknown";
  }
}

}  // namespace iotCommon
