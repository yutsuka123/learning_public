/**
 * @file sensitiveDataService.h
 * @brief 機密データ（Wi-Fi/MQTT/サーバー設定）をNVSへ保存・読込するサービス定義。
 * @details
 * - [重要] 本クラスは NVS（Preferences）を主保存先として扱う。
 * - [重要] 旧 `LittleFS:/sensitiveData.json` は初期化時にNVSへ移行する。
 * - [厳守] TLS認証有効/無効とポートはペアで管理し、初期値は「TLSあり・8883」を採用する。
 * - [禁止] 認証情報の生値をログへ出力しない（マスクなし出力禁止）。
 * - [制限] 本クラスは単純な排他なし実装。複数タスクから同時書込する場合は上位で排他する。
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * @brief 機密データ保存サービス。
 */
class sensitiveDataService {
 public:
  /**
   * @brief サービス初期化。NVS初期化とデフォルトJSON生成、旧保存形式の移行を行う。
   * @return 初期化成功時true、失敗時false。
   */
  bool initialize();

  /**
   * @brief Wi-Fi SSID/パスワードを保存する。
   * @param wifiSsid Wi-Fi SSID。
   * @param wifiPass Wi-Fi パスワード。
   * @return 保存成功時true、失敗時false。
   */
  bool saveWifiCredentials(const String& wifiSsid, const String& wifiPass);

  /**
   * @brief Wi-Fi SSID/パスワードを読み込む。
   * @param wifiSsidOut 読込先SSIDポインタ（null不可）。
   * @param wifiPassOut 読込先パスワードポインタ（null不可）。
   * @return 読込成功時true、失敗時false。
   */
  bool loadWifiCredentials(String* wifiSsidOut, String* wifiPassOut);

  /**
   * @brief MQTT接続設定を保存する。
   * @param mqttUrl MQTTブローカーURLまたはホスト。
   * @param mqttUser MQTTユーザー名。
   * @param mqttPass MQTTパスワード。
   * @param mqttPort MQTTポート番号。
   * @param mqttTls MQTTでTLSを使用するか。
   * @return 保存成功時true、失敗時false。
   */
  bool saveMqttConfig(const String& mqttUrl,
                      const String& mqttUrlName,
                      const String& mqttUser,
                      const String& mqttPass,
                      int32_t mqttPort,
                      bool mqttTls);

  /**
   * @brief MQTT接続設定を読み込む。
   * @param mqttUrlOut 読込先URLポインタ（null不可）。
   * @param mqttUserOut 読込先ユーザー名ポインタ（null不可）。
   * @param mqttPassOut 読込先パスワードポインタ（null不可）。
   * @param mqttPortOut 読込先ポート番号ポインタ（null不可）。
   * @param mqttTlsOut 読込先TLS有効フラグポインタ（null不可）。
   * @return 読込成功時true、失敗時false。
   */
  bool loadMqttConfig(String* mqttUrlOut,
                      String* mqttUrlNameOut,
                      String* mqttUserOut,
                      String* mqttPassOut,
                      int32_t* mqttPortOut,
                      bool* mqttTlsOut);

  /**
   * @brief MQTT TLS CA証明書とメタ情報を保存する。
   * @param mqttTlsCaCertPem CA証明書PEM本文。
   * @param certIssueNo 証明書の発行No。
   * @param certSetAt 設定日時(ISO8601想定)。
   * @return 保存成功時true、失敗時false。
   */
  bool saveMqttTlsCertificate(const String& mqttTlsCaCertPem,
                              const String& certIssueNo,
                              const String& certSetAt);

  /**
   * @brief MQTT TLS CA証明書とメタ情報を読み込む。
   * @param mqttTlsCaCertPemOut 読込先CA証明書PEMポインタ（null不可）。
   * @param certIssueNoOut 読込先発行Noポインタ（null不可）。
   * @param certSetAtOut 読込先設定日時ポインタ（null不可）。
   * @return 読込成功時true、失敗時false。
   */
  bool loadMqttTlsCertificate(String* mqttTlsCaCertPemOut,
                              String* certIssueNoOut,
                              String* certSetAtOut);

  /**
   * @brief LittleFS上の MQTT TLS CA 証明書実体から、NVSメタ情報（issueNo/setAt/sha256/active）を再同期する。
   * @details
   * - [重要] `mqtt-ca.pem` が存在する場合は SHA-256 を再計算して `active=true` とする。
   * - [重要] `mqtt-ca.pem` が存在しない場合は SHA-256 を空文字にし `active=false` とする。
   * - [厳守] 証明書本文はNVSへ保存せず、LittleFS実体を正とする。
   * @param certIssueNo 証明書の発行No（監査用）。
   * @param certSetAt 設定日時（ISO8601想定）。
   * @return 更新成功時true、失敗時false。
   */
  bool syncMqttTlsCertificateMetadataFromLittleFs(const String& certIssueNo,
                                                  const String& certSetAt);

  /**
   * @brief サーバー設定を保存する。
   */
  bool saveServerConfig(const String& serverUrl,
                        const String& serverUrlName,
                        const String& serverUser,
                        const String& serverPass,
                        int32_t serverPort,
                        bool serverTls);

  /**
   * @brief サーバー設定を読み込む。
   */
  bool loadServerConfig(String* serverUrlOut,
                        String* serverUrlNameOut,
                        String* serverUserOut,
                        String* serverPassOut,
                        int32_t* serverPortOut,
                        bool* serverTlsOut);

  /**
   * @brief OTA設定を保存する。
   */
  bool saveOtaConfig(const String& otaUrl,
                     const String& otaUrlName,
                     const String& otaUser,
                     const String& otaPass,
                     int32_t otaPort,
                     bool otaTls);

  /**
   * @brief OTA設定を読み込む。
   */
  bool loadOtaConfig(String* otaUrlOut,
                     String* otaUrlNameOut,
                     String* otaUserOut,
                     String* otaPassOut,
                     int32_t* otaPortOut,
                     bool* otaTlsOut);

  /**
   * @brief タイムサーバー設定を保存する。
   */
  bool saveTimeServerConfig(const String& timeServerUrl,
                            const String& timeServerUrlName,
                            int32_t timeServerPort,
                            bool timeServerTls);

  /**
   * @brief タイムサーバー設定を読み込む。
   * @param timeServerUrlOut 読込先URLポインタ（null不可）。
   * @param timeServerPortOut 読込先ポート番号ポインタ（null不可）。
   * @param timeServerTlsOut 読込先TLS有効フラグポインタ（null不可）。
   * @return 読込成功時true、失敗時false。
   */
  bool loadTimeServerConfig(String* timeServerUrlOut,
                            int32_t* timeServerPortOut,
                            bool* timeServerTlsOut);

  /**
   * @brief タイムサーバー設定（URL名付き）を読み込む。
   */
  bool loadTimeServerConfig(String* timeServerUrlOut,
                            String* timeServerUrlNameOut,
                            int32_t* timeServerPortOut,
                            bool* timeServerTlsOut);

  /**
   * @brief k-device（Base64）を保存する。
   * @param keyDeviceBase64 Base64文字列のk-device。
   * @return 保存成功時true、失敗時false。
   */
  bool saveKeyDevice(const String& keyDeviceBase64);

  /**
   * @brief k-device（Base64）を読み込む。
   * @param keyDeviceBase64Out 読込先ポインタ（null不可）。
   * @return 読込成功時true、失敗時false。
   */
  bool loadKeyDevice(String* keyDeviceBase64Out);

  /**
   * @brief Pairing / KeyRotation 用の current / previous 鍵スロットを更新する。
   * @details
   * - [重要] 既存 current がある場合は previous へ退避し、新しい current を保存する。
   * - [重要] 退避済み previous がある場合でも、直近の current を previous として上書きする。
   * - [厳守] 直後状態は「旧鍵あり=grace / 旧鍵なし=none」で保存する。
   * @param nextKeyDeviceBase64 新 current にする k-device。
   * @param nextKeyVersion 新 current にする keyVersion。
   * @return 保存成功時true、失敗時false。
   */
  bool savePairingKeySlots(const String& nextKeyDeviceBase64, const String& nextKeyVersion);

 private:
  /**
   * @brief 設定データが存在しない場合にデフォルトJSONを生成する。
   * @return 成功時true、失敗時false。
   */
  bool ensureDefaultFileExists();

  /**
   * @brief JSONテキストをNVSから読み込む。
   * @param jsonTextOut 読込先文字列ポインタ（null不可）。
   * @param functionName 呼び出し元関数名（ログ用途）。
   * @return 成功時true、失敗時false。
   */
  bool readJsonText(String* jsonTextOut, const char* functionName);

  /**
   * @brief JSONテキストをNVSへ書き込む（上書き）。
   * @param jsonText 書込JSON文字列。
   * @param functionName 呼び出し元関数名（ログ用途）。
   * @return 成功時true、失敗時false。
   */
  bool writeJsonText(const String& jsonText, const char* functionName);

  /**
   * @brief 旧 LittleFS 保存形式の JSON テキストを読み込む。
   * @param jsonTextOut 読込先文字列ポインタ（null不可）。
   * @param functionName 呼び出し元関数名（ログ用途）。
   * @return 旧形式が存在して読込成功した場合true、それ以外はfalse。
   */
  bool readLegacyJsonText(String* jsonTextOut, const char* functionName);
};
