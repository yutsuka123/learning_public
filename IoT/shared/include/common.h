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
 * @brief MQTT関連定義
 */
namespace mqtt {
    /** @brief トピックプレフィックス: コマンド (Cloud -> Device) */
    constexpr const char* kTopicPrefixCmd = "cmd/esp32lab/";
    /** @brief トピックプレフィックス: レスポンス (Device -> Cloud) */
    constexpr const char* kTopicPrefixRes = "res/esp32lab/";
    /** @brief トピックプレフィックス: 通知 (Mutual) */
    constexpr const char* kTopicPrefixNotice = "notice/esp32lab/";

    /** @brief コマンド名: 設定 */
    constexpr const char* kCmdSet = "set";
    /** @brief コマンド名: 取得 */
    constexpr const char* kCmdGet = "get";
    /** @brief コマンド名: 実行 */
    constexpr const char* kCmdCall = "call";
    /** @brief コマンド名: 状態通知 */
    constexpr const char* kCmdStatus = "status";
    /** @brief コマンド名: ネットワーク設定 */
    constexpr const char* kCmdNetwork = "network";

    /** @brief JSONフィールドキー定義 */
    namespace jsonKey {
        /** @brief [推奨] すべてのコマンドで共通利用するエンベロープキー。 */
        namespace common {
            constexpr const char* kVersion = "v";
            constexpr const char* kDeviceId = "deviceId";
            constexpr const char* kMacAddr = "macAddr";
            constexpr const char* kId = "id";
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kOperation = "op";
            constexpr const char* kArgs = "args";
            constexpr const char* kResult = "result";
            constexpr const char* kDetail = "detail";
        }

        /** @brief [推奨] networkコマンド専用キー。 */
        namespace network {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kWifiSsid = "wifiSSID";
            constexpr const char* kWifiPass = "wifiPass";
            constexpr const char* kMqttUrl = "mqttUrl";
            constexpr const char* kMqttUser = "mqttUser";
            constexpr const char* kMqttPass = "mqttPass";
            constexpr const char* kMqttTls = "mqttTls";
            constexpr const char* kMqttPort = "mqttPort";
            constexpr const char* kApply = "apply";
            constexpr const char* kReboot = "reboot";
        }

        /** @brief setコマンド専用キー。 */
        namespace set {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDeviceId = "deviceId";
            constexpr const char* kMacAddr = "macAddr";
            constexpr const char* kId = "id";
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kCommand = "set";
            constexpr const char* kSub = "sub";
            constexpr const char* kArgs = "args";
            constexpr const char* kDetail = "detail";
        }
        /** @brief getコマンド専用キー。 */
        namespace get {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDeviceId = "deviceId";
            constexpr const char* kMacAddr = "macAddr";
            constexpr const char* kId = "id";
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kCommand = "get";
            constexpr const char* kSub = "sub";
            constexpr const char* kArgs = "args";
            constexpr const char* kDetail = "detail";
        }
        /** @brief callコマンド専用キー。 */
        namespace call {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDeviceId = "deviceId";
            constexpr const char* kMacAddr = "macAddr";
            constexpr const char* kId = "id";
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kCommand = "call";
            constexpr const char* kSub = "sub";
            constexpr const char* kArgs = "args";
            constexpr const char* kDetail = "detail";
        }
        /** @brief statusコマンド専用キー。 */
        namespace status {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDeviceId = "deviceId";
            constexpr const char* kMacAddr = "macAddr";
            constexpr const char* kId = "id";
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kCommand = "status";
            constexpr const char* kSub = "sub";
            constexpr const char* kOnline = "offline";
            constexpr const char* kStartUpTime = "startUpTime";
            constexpr const char* kDeviceTime = "deviceTime";
            constexpr const char* kFirmwareVersion = "firmwareVersion";
            constexpr const char* kWifiSignalLevel = "-50";
            constexpr const char* kArgs = "args";
            constexpr const char* kDetail = "detail";
        }
        /**
         * @brief [旧仕様] 既存参照互換のためのエイリアス。
         * @details 移行中は残す。新規実装では common / network を直接参照すること。
         */
        constexpr const char* kVersion = common::kVersion;
        constexpr const char* kDeviceId = common::kDeviceId;
        constexpr const char* kMacAddr = common::kMacAddr;
        constexpr const char* kId = common::kId;
        constexpr const char* kTimestamp = common::kTimestamp;
        constexpr const char* kOperation = common::kOperation;
        constexpr const char* kArgs = common::kArgs;
        constexpr const char* kResult = common::kResult;
        constexpr const char* kDetail = common::kDetail;
        constexpr const char* kWifiSsid = network::kWifiSsid;
        constexpr const char* kWifiPass = network::kWifiPass;
        constexpr const char* kMqttUrl = network::kMqttUrl;
        constexpr const char* kMqttUser = network::kMqttUser;
        constexpr const char* kMqttPass = network::kMqttPass;
        constexpr const char* kMqttTls = network::kMqttTls;
        constexpr const char* kMqttPort = network::kMqttPort;
        constexpr const char* kApply = network::kApply;
        constexpr const char* kReboot = network::kReboot;
    }
}

/**
 * @brief 共通コマンド識別子。
 * @details 値はログ・監査で扱いやすいよう文字列と1対1で管理する。
 */
enum class commandType : uint16_t {
  kUnknown = 0,
  kDeviceBootNotify = 1001,
  kLedSet = 1002,
  kNetworkSet = 1003,
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
    case commandType::kNetworkSet:
      return "network";
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

/**
 * @brief AP設定関連定義
 */
namespace apConfig {
    /** @brief メンテナンスモードAP名プレフィックス (AP-esp32lab-<MAC>) */
    constexpr const char* kMaintApPrefix = "AP-esp32lab-";
    /** @brief メンテナンスモードAPパスワード */
    constexpr const char* kMaintApPass = "pass-esp32";
    
    /** @brief 設定用外部AP名 */
    constexpr const char* kSettingApName = "AP-esp32lab-setting";
    /** @brief 設定用外部APパスワード */
    constexpr const char* kSettingApPass = "pass-esp32";
}

}  // namespace iotCommon
