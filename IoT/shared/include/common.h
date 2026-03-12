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

    /**
     * @brief レスポンス結果コード文字列。
     * @details
     * - [重要] 要求を受けた側が `jsonKey::*::kRes` へ設定する。
     * - [厳守] `kNg` の場合は `detail` に異常理由を記載する。
     */
    namespace responseResult {
        constexpr const char* kOk = "OK";
        constexpr const char* kNg = "NG";
        constexpr const char* kBusy = "BUSY";
    }

    /**
     * @brief `kSub` に設定するサブコマンド値定義（文字列定数）。
     * @details
     * - [重要] `kSub` は本定義を使用し、ハードコード文字列を避ける。
     * - [禁止] `gpioSet` / `gpioGet` は現時点で新規利用禁止（セキュリティ方針）。
     */
    namespace subCommand {
        namespace set {
            constexpr const char* kRelay = "relay";
            constexpr const char* kLedOn = "led_ON";
            constexpr const char* kLedOff = "led_OFF";
            constexpr const char* kLedBlink = "led_Blink";
            constexpr const char* kGpioHigh = "gpio_H";
            constexpr const char* kGpioLow = "gpio_L";
            // [旧仕様] 互換のため受信許容。新規送信は禁止。
            constexpr const char* kGpioHighLegacy = "giio_H";
            constexpr const char* kGpioLowLegacy = "giio_L";
        }
        namespace get {
            constexpr const char* kTrh = "trh";
            constexpr const char* kRelay = "relay";
            constexpr const char* kLed = "led";
            constexpr const char* kButton = "button";
            constexpr const char* kGpio = "gpio";
            constexpr const char* kLog = "log";
            // [旧仕様] 互換のため受信許容。新規送信は禁止。
            constexpr const char* kButtonLegacy = "Botton";
            constexpr const char* kGpioLegacy = "giio";
        }
        namespace call {
            constexpr const char* kStatus = "status";
            constexpr const char* kRestart = "restart";
            constexpr const char* kMaintenance = "maintenance";
            constexpr const char* kOtaStart = "otaStart";
            // [重要] 7025 未確定起動失敗再現用の試験コマンド。
            constexpr const char* kRollbackTestEnable = "rollbackTestEnable";
            constexpr const char* kRollbackTestDisable = "rollbackTestDisable";
            // [旧仕様] 互換のため受信許容。新規送信は禁止。
            constexpr const char* kMaintenanceLegacy = "mentenance";
        }
        namespace notice {
            constexpr const char* kStatus = "status";
            constexpr const char* kOtaProgress = "otaProgress";
            constexpr const char* kTrh = "trh";
            constexpr const char* kFileSyncStatus = "fileSyncStatus";
        }
        namespace status {
            constexpr const char* kStartUp = "start-up";
            constexpr const char* kWill = "Will";
            constexpr const char* kReConnect = "reconnect";
            constexpr const char* kButton = "button";
            constexpr const char* kReply = "reply";
            constexpr const char* kRestartButton = "restart(button)";
            constexpr const char* kRestartAbort = "restart(abort)";
            constexpr const char* kRestartCall = "restart(call)";
        }
    }

    /** @brief JSONフィールドキー定義 */
    namespace jsonKey {
        /** @brief [推奨] networkコマンド専用キー。 */
        namespace network {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDstId = "DstID"; // 発信先ID
            constexpr const char* kSrcId = "SrcID"; // 発信元ID
            constexpr const char* kKind = "Request";/**返信ならResponse、通知ならNotice */
            constexpr const char* kRes = "Res"; // 返答用。値は responseResult::kOk / kNg / kBusy を使用。
            constexpr const char* kId = "id";//要求番号/返答時は同じNOを使用する。西暦年月日時分秒ミリ秒+通し番号5桁例00001を使用する。
            constexpr const char* kWifiSsid = "wifiSSID";
            constexpr const char* kWifiPass = "wifiPass";
            constexpr const char* kMqttUrl = "mqttUrl";
            constexpr const char* kMqttUrlName = "mqttUrlName";//Ipではなく名前がある場合記載する
            constexpr const char* kMqttUser = "mqttUser";
            constexpr const char* kMqttPass = "mqttPass";
            constexpr const char* kMqttTls = "mqttTls";
            constexpr const char* kMqttPort = "mqttPort";
            constexpr const char* kServer = "server";
            constexpr const char* kServerUrl = "serverUrl";
            constexpr const char* kServerUrlName = "serverUrlName";//Ipではなく名前がある場合記載する
            constexpr const char* kServerUser = "serverUser";
            constexpr const char* kServerPass = "serverPass";
            constexpr const char* kServerPort = "serverPort";
            constexpr const char* kServerTls = "serverTls";
            constexpr const char* kOta = "ota";
            constexpr const char* kOtaUrl = "otaUrl";
            constexpr const char* kOtaUrlName = "otaUrlName";//Ipではなく名前がある場合記載する
            constexpr const char* kOtaUser = "otaUser";
            constexpr const char* kOtaPass = "otaPass";
            constexpr const char* kOtaPort = "otaPort";
            constexpr const char* kOtaTls = "otaTls";
            constexpr const char* kTimeServer = "timeServer";
            constexpr const char* kTimeServerUrl = "timeServerUrl";
            constexpr const char* kTimeServerUrlName = "timeServerUrlName";//Ipではなく名前がある場合記載する
            constexpr const char* kTimeServerPort = "timeServerPort";
            constexpr const char* kTimeServerTls = "timeServerTls";
            constexpr const char* kApply = "apply";
            constexpr const char* kReboot = "reboot";
            constexpr const char* kKeyDevice = "keyDevice";//Base64エンコードされたk-deviceの値

        }

        /** @brief setコマンド専用キー。 */
        namespace set {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDstId = "DstID"; // 発信先ID
            constexpr const char* kSrcId = "SrcID"; // 発信元ID
            constexpr const char* kKind = "Request";/**返信ならResponse、通知ならNotice */
            constexpr const char* kRes = "Res"; // 返答用。値は responseResult::kOk / kNg / kBusy を使用。
            constexpr const char* kMacAddr = "macAddr";
            constexpr const char* kId = "id";//要求番号/返答時は同じNOを使用する。西暦年月日時分秒ミリ秒+通し番号5桁例00001を使用する。
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kCommand = "set";
            constexpr const char* kSub = "sub";
            constexpr const char* kArgs = "args";
            constexpr const char* kDetail = "detail";
        }
        /** @brief getコマンド専用キー。 */
        namespace get {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDstId = "DstID"; // 発信先ID
            constexpr const char* kSrcId = "SrcID"; // 発信元ID
            constexpr const char* kKind = "Request";/**返信ならResponse、通知ならNotice */
            constexpr const char* kRes = "Res"; // 返答用。値は responseResult::kOk / kNg / kBusy を使用。
            constexpr const char* kMacAddr = "macAddr";
            constexpr const char* kId = "id";//要求番号/返答時は同じNOを使用する。西暦年月日時分秒ミリ秒+通し番号5桁例00001を使用する。
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kCommand = "get";
            constexpr const char* kSub = "sub";
            constexpr const char* kArgs = "args";
            constexpr const char* kDetail = "detail";
        }
        /** @brief callコマンド専用キー。 */
        namespace call {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDstId = "DstID"; // 発信先ID
            constexpr const char* kSrcId = "SrcID"; // 発信元ID
            constexpr const char* kKind = "Request";/**返信ならResponse、通知ならNotice */
            constexpr const char* kRes = "Res"; // 返答用。値は responseResult::kOk / kNg / kBusy を使用。
            constexpr const char* kMacAddr = "macAddr";
            constexpr const char* kId = "id";//要求番号/返答時は同じNOを使用する。西暦年月日時分秒ミリ秒+通し番号5桁例00001を使用する。
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kCommand = "call";
            constexpr const char* kSub = "sub";
            constexpr const char* kArgs = "args";
            constexpr const char* kDetail = "detail";
        }
        /** @brief statusコマンド専用キー。 */
        namespace status {
            constexpr const char* kVersion = "v"; // 共通キー
            constexpr const char* kDstId = "DstID"; // 発信先ID
            constexpr const char* kSrcId = "SrcID"; // 発信元ID
            constexpr const char* kKind = "Request";/**返信ならResponse、通知ならNotice */
            constexpr const char* kRes = "Res"; // 返答用。値は responseResult::kOk / kNg / kBusy を使用。
            constexpr const char* kMacAddr = "macAddr"; // [重要] ESP32のeFuse由来Base MAC（変更不可）
            constexpr const char* kMacAddrNetwork = "macAddrNetwork"; // [重要] ネットワークアダプタMAC（ESP32は通常Wi-Fi MAC）
            constexpr const char* kId = "id";//要求番号/返答時は同じNOを使用する。西暦年月日時分秒ミリ秒+通し番号5桁例00001を使用する。
            constexpr const char* kTimestamp = "ts";
            constexpr const char* kCommand = "status";
            constexpr const char* kFWVersion = "fwVersion";
            constexpr const char* kFWWrittenAt = "fwWrittenAt";
            constexpr const char* kSub = "sub";
            constexpr const char* kOnlineState = "onlineState";
            constexpr const char* kStartUpTime = "startUpTime";
            constexpr const char* kFirmwareVersion = "firmwareVersion";
            constexpr const char* kFirmwareWrittenAt = "firmwareWrittenAt";
            constexpr const char* kWifiSignalLevel = "wifiSignalLevel";
            constexpr const char* kIpAddress = "ipAddress";
            constexpr const char* kWifiSsid = "wifiSsid";
            // [重要] 7015/7025 試験で A/B 切替を確認するための一時項目。
            constexpr const char* kRunningPartition = "runningPartition";
            constexpr const char* kBootPartition = "bootPartition";
            constexpr const char* kNextUpdatePartition = "nextUpdatePartition";
            constexpr const char* kDetail = "detail";
        }
        /**
         * @brief [旧仕様] 既存参照互換のためのエイリアス。
         * @details 移行中は残す。新規実装では network を直接参照すること。
         */
        constexpr const char* kVersion = "v";
        constexpr const char* kDeviceId = "deviceId";
        constexpr const char* kMacAddr = "macAddr";
        constexpr const char* kId = "id";
        constexpr const char* kTimestamp = "ts";
        constexpr const char* kOperation = "op";
        constexpr const char* kArgs = "args";
        constexpr const char* kResult = "result";
        constexpr const char* kDetail = "detail";
        constexpr const char* kWifiSsid = network::kWifiSsid;
        constexpr const char* kWifiPass = network::kWifiPass;
        constexpr const char* kMqttUrl = network::kMqttUrl;
        constexpr const char* kMqttUser = network::kMqttUser;
        constexpr const char* kMqttPass = network::kMqttPass;
        constexpr const char* kMqttTls = network::kMqttTls;
        constexpr const char* kMqttPort = network::kMqttPort;
        constexpr const char* kServer = network::kServer;
        constexpr const char* kServerUrl = network::kServerUrl;
        constexpr const char* kServerUser = network::kServerUser;
        constexpr const char* kServerPass = network::kServerPass;
        constexpr const char* kServerPort = network::kServerPort;
        constexpr const char* kServerTls = network::kServerTls;
        constexpr const char* kOta = network::kOta;
        constexpr const char* kOtaUrl = network::kOtaUrl;
        constexpr const char* kOtaUser = network::kOtaUser;
        constexpr const char* kOtaPass = network::kOtaPass;
        constexpr const char* kOtaPort = network::kOtaPort;
        constexpr const char* kOtaTls = network::kOtaTls;
        constexpr const char* kTimeServer = network::kTimeServer;
        constexpr const char* kTimeServerUrl = network::kTimeServerUrl;
        constexpr const char* kTimeServerPort = network::kTimeServerPort;
        constexpr const char* kTimeServerTls = network::kTimeServerTls;
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
