/**
 * @file sensitiveDataService.h
 * @brief 機密データ（Wi-Fi/MQTT設定）をJSONファイルへ保存・読込するサービス定義。
 * @details
 * - [重要] 本クラスは LittleFS 上の `/sensitiveData.json` を唯一の保存先として扱う。
 * - [厳守] TLS認証有効/無効とポートはペアで管理し、初期値は「TLSなし・8883」を採用する。
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
   * @brief サービス初期化。LittleFS初期化とデフォルトJSON生成を行う。
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
                      String* mqttUserOut,
                      String* mqttPassOut,
                      int32_t* mqttPortOut,
                      bool* mqttTlsOut);

 private:
  /**
   * @brief 設定ファイルが存在しない場合にデフォルトJSONを生成する。
   * @return 成功時true、失敗時false。
   */
  bool ensureDefaultFileExists();

  /**
   * @brief JSONテキストをファイルから読み込む。
   * @param jsonTextOut 読込先文字列ポインタ（null不可）。
   * @param functionName 呼び出し元関数名（ログ用途）。
   * @return 成功時true、失敗時false。
   */
  bool readJsonText(String* jsonTextOut, const char* functionName);

  /**
   * @brief JSONテキストをファイルへ書き込む（上書き）。
   * @param jsonText 書込JSON文字列。
   * @param functionName 呼び出し元関数名（ログ用途）。
   * @return 成功時true、失敗時false。
   */
  bool writeJsonText(const String& jsonText, const char* functionName);
};
