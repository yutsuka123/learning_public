/**
 * @file mqtt.cpp
 * @brief MQTT機能のタスク実装。
 * @details
 * - [重要] mainTaskから受け取った設定でブローカー接続し、online状態をpublishする。
 * - [厳守] MQTT接続前にブローカー到達確認（TCPプローブ）を実施する。
 * - [厳守] MQTT認証はユーザー名/パスワード必須とし、未設定時は接続しない。
 * - [重要] TLS有効時は `SENSITIVE_MQTT_TLS_CA_CERT` を設定し、証明書検証を有効化する。
 */

#include "mqtt.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <vector>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>

#include "common.h"
#include "firmwareInfo.h"
#include "i2c.h"
#include "interTaskMessage.h"
#include "jsonService.h"
#include "mqttPayloadSecurity.h"
#include "mqttMessages.h"
#include "led.h"
#include "log.h"
#include "maintenanceMode.h"
#include "ota.h"
#include "otaRollback.h"
#include "sensitiveData.h"
#include "sensitiveDataService.h"
#include "util.h"
#include "version.h"

namespace {
/**
 * @brief OTA進捗フェーズが終端（done/error）か判定する。
 * @param phaseText フェーズ文字列。
 * @return 終端フェーズならtrue。
 */
bool isTerminalOtaPhase(const char* phaseText) {
  if (phaseText == nullptr) {
    return false;
  }
  String normalizedPhase = String(phaseText);
  normalizedPhase.trim();
  return normalizedPhase.equalsIgnoreCase("done") || normalizedPhase.equalsIgnoreCase("error");
}

/**
 * @brief MQTT向けTLSクライアント。
 * @details
 * - [重要] IP直指定で接続する場合でも、証明書検証はDNSホスト名で実施する。
 * - [重要] DNS障害時の暫定フォールバック（IP接続）でもTLS検証を維持する。
 * - [禁止] 証明書検証の無効化（setInsecure）は使用しない。
 */
class MqttTlsClient : public WiFiClientSecure {
 public:
  /**
   * @brief IP接続時に使うTLS検証コンテキストを設定する。
   * @param tlsHostName 証明書検証に使うDNSホスト名。
   * @param tlsCaCertificate ルートCA証明書PEM。
   */
  void setTlsValidationContext(const char* tlsHostName, const char* tlsCaCertificate) {
    tlsHostName_ = tlsHostName;
    tlsCaCertificate_ = tlsCaCertificate;
  }

  /**
   * @brief IPアドレス向けconnectを上書きする。
   * @details
   * - [重要] PubSubClientはIP接続時にこの関数を呼ぶ。
   * - [重要] TLS検証文脈（host + CA）が有効なら、その文脈でconnectする。
   * @param ip 接続先IPアドレス。
   * @param port 接続先ポート番号。
   * @return 接続成功時1、失敗時0。
   */
  int connect(IPAddress ip, uint16_t port) override {
    bool hostAvailable = (tlsHostName_ != nullptr && strlen(tlsHostName_) > 0);
    bool caAvailable = (tlsCaCertificate_ != nullptr && strlen(tlsCaCertificate_) > 0);
    if (!hostAvailable || !caAvailable) {
      appLogWarn("MqttTlsClient::connect fallback to default verification path. ip=%s port=%ld hostAvailable=%d caAvailable=%d",
                 ip.toString().c_str(),
                 static_cast<long>(port),
                 hostAvailable ? 1 : 0,
                 caAvailable ? 1 : 0);
      return WiFiClientSecure::connect(ip, port);
    }

    // [重要] DNS解決失敗時のIP接続でも、証明書照合はDNS名（tlsHostName_）で行う。
    return WiFiClientSecure::connect(ip, port, tlsHostName_, tlsCaCertificate_, nullptr, nullptr);
  }

  /**
   * @brief DNS失敗時に使うフォールバックIPを設定する。
   * @param fallbackIp フォールバック接続先IP。
   */
  void setFallbackEndpointIp(IPAddress fallbackIp) {
    fallbackEndpointIp_ = fallbackIp;
    fallbackEndpointEnabled_ = (fallbackEndpointIp_ != IPAddress(0, 0, 0, 0));
  }

  /**
   * @brief ホスト名接続。失敗時はフォールバックIP接続を試す。
   * @param host 接続先ホスト名。
   * @param port 接続先ポート番号。
   * @param timeout タイムアウト（ms）。
   * @return 接続成功時1、失敗時0。
   */
  int connect(const char* host, uint16_t port, int32_t timeout) override {
    int directConnectResult = WiFiClientSecure::connect(host, port, timeout);
    if (directConnectResult == 1) {
      return directConnectResult;
    }
    bool canUseFallback = fallbackEndpointEnabled_ &&
                          host != nullptr &&
                          tlsHostName_ != nullptr &&
                          strlen(tlsHostName_) > 0 &&
                          tlsCaCertificate_ != nullptr &&
                          strlen(tlsCaCertificate_) > 0 &&
                          String(host).equalsIgnoreCase(String(tlsHostName_));
    if (!canUseFallback) {
      return directConnectResult;
    }
    appLogWarn("MqttTlsClient::connect fallback to configured IP. host=%s fallbackIp=%s port=%ld",
               host,
               fallbackEndpointIp_.toString().c_str(),
               static_cast<long>(port));
    return WiFiClientSecure::connect(fallbackEndpointIp_, port, tlsHostName_, tlsCaCertificate_, nullptr, nullptr);
  }

  /**
   * @brief ホスト名接続（timeout省略版）。
   * @param host 接続先ホスト名。
   * @param port 接続先ポート番号。
   * @return 接続成功時1、失敗時0。
   */
  int connect(const char* host, uint16_t port) override {
    return connect(host, port, 5000);
  }

 private:
  /** @brief IP接続時の証明書照合ホスト名。 */
  const char* tlsHostName_ = nullptr;
  /** @brief IP接続時のCA証明書。 */
  const char* tlsCaCertificate_ = nullptr;
  /** @brief DNS失敗時のフォールバックIP。 */
  IPAddress fallbackEndpointIp_ = IPAddress(0, 0, 0, 0);
  /** @brief フォールバックIP有効フラグ。 */
  bool fallbackEndpointEnabled_ = false;
};

/** @brief mqttTask用スタック領域。PSRAM優先で確保し、失敗時は内部RAMへフォールバックする。 */
StackType_t* mqttTaskStackBuffer = nullptr;
/** @brief mqttTask用の静的タスク制御ブロック。 */
StaticTask_t mqttTaskControlBlock;
/** @brief MQTT通信用の下位TCPクライアント。 */
WiFiClient mqttNetworkClient;
/** @brief MQTT(TLS)通信用の下位TCPクライアント。 */
MqttTlsClient mqttTlsNetworkClient;
/** @brief PubSubClient本体。mqttNetworkClientを介して通信する。 */
PubSubClient mqttClient(mqttNetworkClient);

/** @brief MQTT接続先ホスト名/IP文字列バッファ。 */
char mqttHost[64] = {0};
/** @brief MQTT接続ユーザー名バッファ。 */
char mqttUser[64] = {0};
/** @brief MQTT接続パスワードバッファ。 */
char mqttPass[64] = {0};
/** @brief MQTT接続先ポート番号。 */
int32_t mqttPort = 1883;
/** @brief TLS利用フラグ（未実装）。 */
bool mqttTls = false;
/** @brief MQTT初期化が成功済みかどうか。 */
bool isMqttInitialized = false;
/** @brief status online状態値。 */
constexpr const char* statusValueOnline = "Online";
/** @brief UTC同期済みとみなす最小エポックミリ秒(2021-01-01T00:00:00.000Z)。 */
constexpr int64_t minimumValidUtcEpochMillis = 1609459200000LL;
/** @brief mainTaskEntry開始時CPU時刻(ms)。publish要求時にmainTaskから受け取る。 */
uint32_t mainTaskStartupCpuMillis = 0;
/** @brief 送信者/受信者名として利用するデバイス識別子。 */
String deviceNodeName = "";
/** @brief pingBrokerHostで解決済みのMQTT接続先IP。 */
IPAddress mqttResolvedBrokerIpAddress;
/** @brief 解決済みMQTT接続先IPの有効フラグ。 */
bool mqttResolvedBrokerIpValid = false;
/** @brief MQTTパケットバッファサイズ（暗号化ペイロード肥大化対策）。 */
constexpr uint16_t mqttPacketBufferSizeBytes = 4096;
/** @brief MQTT payload暗号化モード（0:平文,1:互換,2:暗号必須）。 */
#ifndef MQTT_PAYLOAD_SECURITY_MODE
// [厳守] 本番既定は strict(2)。明示的に上書きしない限り平文受理しない。
#define MQTT_PAYLOAD_SECURITY_MODE 2
#endif
constexpr mqttPayloadSecurity::payloadSecurityMode mqttPayloadSecurityModeValue =
    static_cast<mqttPayloadSecurity::payloadSecurityMode>(MQTT_PAYLOAD_SECURITY_MODE);
sensitiveDataService mqttSensitiveDataService;
bool mqttSensitiveDataInitialized = false;
String mqttTlsCaCertRuntime;

/**
 * @brief MQTT経由のk-device保存/読込に必要なsensitiveDataService初期化を保証する。
 * @return 初期化成功時true。
 */
bool ensureMqttSensitiveDataReady() {
  if (mqttSensitiveDataInitialized) {
    return true;
  }
  mqttSensitiveDataInitialized = mqttSensitiveDataService.initialize();
  if (!mqttSensitiveDataInitialized) {
    appLogError("ensureMqttSensitiveDataReady failed. sensitiveDataService::initialize returned false.");
  }
  return mqttSensitiveDataInitialized;
}

/**
 * @brief Base64文字列をバイト列へ復号する。
 * @param inputBase64 入力文字列。
 * @param outputBufferOut 出力先。
 * @return 成功時true。
 */
bool decodeBase64Text(const String& inputBase64, std::vector<uint8_t>* outputBufferOut) {
  if (outputBufferOut == nullptr) {
    appLogError("decodeBase64Text failed. outputBufferOut is null.");
    return false;
  }
  outputBufferOut->clear();
  size_t outputLength = 0;
  int32_t firstResult = mbedtls_base64_decode(nullptr,
                                              0,
                                              &outputLength,
                                              reinterpret_cast<const unsigned char*>(inputBase64.c_str()),
                                              inputBase64.length());
  if (firstResult != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && firstResult != 0) {
    appLogError("decodeBase64Text failed. probe decode result=%ld inputLength=%ld",
                static_cast<long>(firstResult),
                static_cast<long>(inputBase64.length()));
    return false;
  }
  outputBufferOut->resize(outputLength);
  if (outputLength == 0) {
    return true;
  }
  int32_t decodeResult = mbedtls_base64_decode(outputBufferOut->data(),
                                               outputBufferOut->size(),
                                               &outputLength,
                                               reinterpret_cast<const unsigned char*>(inputBase64.c_str()),
                                               inputBase64.length());
  if (decodeResult != 0) {
    appLogError("decodeBase64Text failed. decode result=%ld inputLength=%ld", static_cast<long>(decodeResult), static_cast<long>(inputBase64.length()));
    return false;
  }
  outputBufferOut->resize(outputLength);
  return true;
}

bool loadKDeviceBytes(std::vector<uint8_t>* keyBytesOut);
bool createCurrentUtcIso8601Text(String* utcIso8601Out);
bool publishTrhNotice(const String& destinationId,
                      const String& requestId,
                      const i2cEnvironmentSnapshot& snapshot,
                      bool isSuccess,
                      const char* detailText);

/**
 * @brief HMAC-SHA256を計算する。
 * @param keyBytes 鍵バイト列。
 * @param messageText 対象メッセージ。
 * @param macBytesOut 出力先（32バイト想定）。
 * @return 成功時true。
 */
bool computeHmacSha256(const std::vector<uint8_t>& keyBytes,
                       const String& messageText,
                       std::vector<uint8_t>* macBytesOut) {
  if (macBytesOut == nullptr) {
    appLogError("computeHmacSha256 failed. macBytesOut is null.");
    return false;
  }
  const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (mdInfo == nullptr) {
    appLogError("computeHmacSha256 failed. mbedtls_md_info_from_type returned null.");
    return false;
  }
  macBytesOut->assign(32, 0);
  int hmacResult = mbedtls_md_hmac(mdInfo,
                                   keyBytes.data(),
                                   keyBytes.size(),
                                   reinterpret_cast<const unsigned char*>(messageText.c_str()),
                                   messageText.length(),
                                   macBytesOut->data());
  if (hmacResult != 0) {
    appLogError("computeHmacSha256 failed. mbedtls_md_hmac result=%ld keyLength=%ld messageLength=%ld",
                static_cast<long>(hmacResult),
                static_cast<long>(keyBytes.size()),
                static_cast<long>(messageText.length()));
    return false;
  }
  return true;
}

/**
 * @brief 署名検証用の正規化payload文字列を作成する。
 * @details
 * - [厳守] `signature` フィールドを除外したJSON文字列をHMAC入力とする。
 * @param payloadText 入力payload。
 * @param normalizedPayloadOut 出力先（null不可）。
 * @param signatureBase64Out 署名Base64出力先（null不可）。
 * @param signatureAlgorithmOut 署名方式出力先（null不可）。
 * @return 成功時true。
 */
bool buildSignedPayloadForVerification(const String& payloadText,
                                       String* normalizedPayloadOut,
                                       String* signatureBase64Out,
                                       String* signatureAlgorithmOut) {
  if (normalizedPayloadOut == nullptr || signatureBase64Out == nullptr || signatureAlgorithmOut == nullptr) {
    appLogError("buildSignedPayloadForVerification failed. output parameter is null.");
    return false;
  }
  cJSON* rootObject = cJSON_Parse(payloadText.c_str());
  if (rootObject == nullptr || !cJSON_IsObject(rootObject)) {
    appLogError("buildSignedPayloadForVerification failed. payload parse error.");
    cJSON_Delete(rootObject);
    return false;
  }
  cJSON* signatureItem = cJSON_GetObjectItemCaseSensitive(rootObject, "signature");
  cJSON* sigAlgItem = cJSON_GetObjectItemCaseSensitive(rootObject, "sigAlg");
  if (!cJSON_IsString(signatureItem)) {
    appLogError("buildSignedPayloadForVerification failed. signature is missing.");
    cJSON_Delete(rootObject);
    return false;
  }
  *signatureBase64Out = String(signatureItem->valuestring);
  *signatureAlgorithmOut = cJSON_IsString(sigAlgItem) ? String(sigAlgItem->valuestring) : String("");
  cJSON_DeleteItemFromObjectCaseSensitive(rootObject, "signature");
  char* normalizedJsonText = cJSON_PrintUnformatted(rootObject);
  cJSON_Delete(rootObject);
  if (normalizedJsonText == nullptr) {
    appLogError("buildSignedPayloadForVerification failed. cJSON_PrintUnformatted returned null.");
    return false;
  }
  *normalizedPayloadOut = String(normalizedJsonText);
  cJSON_free(normalizedJsonText);
  return true;
}

/**
 * @brief fileSync系コマンドの署名検証を行う。
 * @details
 * - [厳守] `signature` 未指定または検証失敗時は拒否する。
 * - [重要] 現行は `HMAC-SHA256`（`k-device` 鍵）を正規方式として検証する。
 * @param normalizedSubName サブコマンド。
 * @param payloadText 入力payload。
 * @return 検証成功時true。
 */
bool verifyFileSyncCommandSignature(const String& normalizedSubName, const String& payloadText) {
  if (!(normalizedSubName.equalsIgnoreCase("fileSyncPlan") ||
        normalizedSubName.equalsIgnoreCase("fileSyncChunk") ||
        normalizedSubName.equalsIgnoreCase("fileSyncCommit") ||
        normalizedSubName.equalsIgnoreCase("imagePackageApply"))) {
    return true;
  }

  String normalizedPayload;
  String signatureBase64;
  String signatureAlgorithm;
  if (!buildSignedPayloadForVerification(payloadText, &normalizedPayload, &signatureBase64, &signatureAlgorithm)) {
    appLogError("verifyFileSyncCommandSignature failed. signature field is invalid. sub=%s", normalizedSubName.c_str());
    return false;
  }
  if (!(signatureAlgorithm.equalsIgnoreCase("HMAC-SHA256") || signatureAlgorithm.length() == 0)) {
    appLogError("verifyFileSyncCommandSignature failed. unsupported sigAlg=%s sub=%s",
                signatureAlgorithm.c_str(),
                normalizedSubName.c_str());
    return false;
  }

  std::vector<uint8_t> expectedMacBytes;
  if (!decodeBase64Text(signatureBase64, &expectedMacBytes)) {
    appLogError("verifyFileSyncCommandSignature failed. signature base64 decode error. sub=%s", normalizedSubName.c_str());
    return false;
  }
  std::vector<uint8_t> keyBytes;
  if (!loadKDeviceBytes(&keyBytes)) {
    appLogError("verifyFileSyncCommandSignature failed. no valid k-device. sub=%s", normalizedSubName.c_str());
    return false;
  }
  std::vector<uint8_t> actualMacBytes;
  if (!computeHmacSha256(keyBytes, normalizedPayload, &actualMacBytes)) {
    appLogError("verifyFileSyncCommandSignature failed. computeHmacSha256 error. sub=%s", normalizedSubName.c_str());
    return false;
  }
  if (actualMacBytes.size() != expectedMacBytes.size()) {
    appLogError("verifyFileSyncCommandSignature failed. signature length mismatch. expected=%ld actual=%ld sub=%s",
                static_cast<long>(expectedMacBytes.size()),
                static_cast<long>(actualMacBytes.size()),
                normalizedSubName.c_str());
    return false;
  }
  for (size_t index = 0; index < actualMacBytes.size(); ++index) {
    if (actualMacBytes[index] != expectedMacBytes[index]) {
      appLogError("verifyFileSyncCommandSignature failed. signature mismatch. sub=%s index=%ld",
                  normalizedSubName.c_str(),
                  static_cast<long>(index));
      return false;
    }
  }
  return true;
}

/**
 * @brief バイト列をBase64文字列へ変換する。
 * @param inputBuffer 入力バッファ。
 * @param outputBase64Out 出力先。
 * @return 成功時true。
 */
bool encodeBase64Text(const std::vector<uint8_t>& inputBuffer, String* outputBase64Out) {
  if (outputBase64Out == nullptr) {
    appLogError("encodeBase64Text failed. outputBase64Out is null.");
    return false;
  }
  size_t requiredLength = 0;
  int32_t probeResult = mbedtls_base64_encode(nullptr, 0, &requiredLength, inputBuffer.data(), inputBuffer.size());
  if (probeResult != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && probeResult != 0) {
    appLogError("encodeBase64Text failed. probe result=%ld inputLength=%ld",
                static_cast<long>(probeResult),
                static_cast<long>(inputBuffer.size()));
    return false;
  }
  std::vector<uint8_t> encodedBuffer(requiredLength + 1, 0);
  size_t writtenLength = 0;
  int32_t encodeResult = mbedtls_base64_encode(encodedBuffer.data(),
                                               encodedBuffer.size(),
                                               &writtenLength,
                                               inputBuffer.data(),
                                               inputBuffer.size());
  if (encodeResult != 0) {
    appLogError("encodeBase64Text failed. encode result=%ld inputLength=%ld", static_cast<long>(encodeResult), static_cast<long>(inputBuffer.size()));
    return false;
  }
  *outputBase64Out = String(reinterpret_cast<const char*>(encodedBuffer.data()), writtenLength);
  return true;
}

/**
 * @brief 保存済みk-deviceを読込み、暗号化鍵(32byte)へ変換する。
 * @param keyBytesOut 出力先。
 * @return 成功時true。
 */
bool loadKDeviceBytes(std::vector<uint8_t>* keyBytesOut) {
  if (keyBytesOut == nullptr) {
    appLogError("loadKDeviceBytes failed. keyBytesOut is null.");
    return false;
  }
  if (!ensureMqttSensitiveDataReady()) {
    return false;
  }
  String keyDeviceBase64;
  if (!mqttSensitiveDataService.loadKeyDevice(&keyDeviceBase64)) {
    appLogError("loadKDeviceBytes failed. loadKeyDevice returned false.");
    return false;
  }
  if (!decodeBase64Text(keyDeviceBase64, keyBytesOut)) {
    appLogError("loadKDeviceBytes failed. invalid base64.");
    return false;
  }
  if (keyBytesOut->size() != 32) {
    appLogError("loadKDeviceBytes failed. key size must be 32. actual=%ld", static_cast<long>(keyBytesOut->size()));
    return false;
  }
  return true;
}

/**
 * @brief 受信payloadを暗号化運用モードに従って復号する。
 * @param topicName 受信トピック（ログ用途）。
 * @param incomingPayloadText 受信payload。
 * @param effectivePayloadTextOut 復号後またはそのままのpayload。
 * @param wasEncryptedOut 暗号化エンベロープだったかどうか。
 * @return 処理成功時true。
 */
bool resolveIncomingPayloadText(const char* topicName,
                                const String& incomingPayloadText,
                                String* effectivePayloadTextOut,
                                bool* wasEncryptedOut) {
  if (effectivePayloadTextOut == nullptr || wasEncryptedOut == nullptr) {
    appLogError("resolveIncomingPayloadText failed. output parameter is null.");
    return false;
  }
  *effectivePayloadTextOut = incomingPayloadText;
  *wasEncryptedOut = false;
  if (mqttPayloadSecurityModeValue == mqttPayloadSecurity::payloadSecurityMode::kPlain) {
    return true;
  }

  std::vector<uint8_t> keyBytes;
  if (!loadKDeviceBytes(&keyBytes)) {
    if (mqttPayloadSecurityModeValue == mqttPayloadSecurity::payloadSecurityMode::kCompat) {
      appLogWarn("resolveIncomingPayloadText: k-device unavailable. fallback to plain. topic=%s",
                 topicName == nullptr ? "(null)" : topicName);
      return true;
    }
    appLogError("resolveIncomingPayloadText failed. k-device required but unavailable. topic=%s",
                topicName == nullptr ? "(null)" : topicName);
    return false;
  }

  bool isEncryptedEnvelope = false;
  String decryptedPayloadText;
  if (!mqttPayloadSecurity::decodeEncryptedEnvelopeIfPresent(keyBytes, incomingPayloadText, &isEncryptedEnvelope, &decryptedPayloadText)) {
    appLogError("resolveIncomingPayloadText failed. envelope decode error. topic=%s",
                topicName == nullptr ? "(null)" : topicName);
    return false;
  }
  *wasEncryptedOut = isEncryptedEnvelope;
  if (!isEncryptedEnvelope) {
    if (mqttPayloadSecurityModeValue == mqttPayloadSecurity::payloadSecurityMode::kStrict) {
      appLogError("resolveIncomingPayloadText failed. encrypted payload is required. topic=%s",
                  topicName == nullptr ? "(null)" : topicName);
      return false;
    }
    return true;
  }
  *effectivePayloadTextOut = decryptedPayloadText;
  return true;
}

/**
 * @brief 送信payloadを暗号化運用モードに従ってエンコードする。
 * @param topicText 送信トピック（ログ用途）。
 * @param plainPayloadText 平文payload。
 * @param outgoingPayloadTextOut 実際にpublishするpayload。
 * @return 成功時true。
 */
bool resolveOutgoingPayloadText(const String& topicText,
                                const String& plainPayloadText,
                                String* outgoingPayloadTextOut) {
  if (outgoingPayloadTextOut == nullptr) {
    appLogError("resolveOutgoingPayloadText failed. outgoingPayloadTextOut is null.");
    return false;
  }
  *outgoingPayloadTextOut = plainPayloadText;
  if (mqttPayloadSecurityModeValue == mqttPayloadSecurity::payloadSecurityMode::kPlain) {
    return true;
  }

  std::vector<uint8_t> keyBytes;
  if (!loadKDeviceBytes(&keyBytes)) {
    if (mqttPayloadSecurityModeValue == mqttPayloadSecurity::payloadSecurityMode::kCompat) {
      appLogWarn("resolveOutgoingPayloadText: k-device unavailable. fallback to plain. topic=%s", topicText.c_str());
      return true;
    }
    appLogError("resolveOutgoingPayloadText failed. k-device required but unavailable. topic=%s", topicText.c_str());
    return false;
  }

  String encryptedEnvelopeText;
  if (!mqttPayloadSecurity::encodeEncryptedEnvelope(keyBytes, plainPayloadText, &encryptedEnvelopeText)) {
    if (mqttPayloadSecurityModeValue == mqttPayloadSecurity::payloadSecurityMode::kCompat) {
      appLogWarn("resolveOutgoingPayloadText: envelope encode failed. fallback to plain. topic=%s", topicText.c_str());
      return true;
    }
    appLogError("resolveOutgoingPayloadText failed. envelope encode failed. topic=%s", topicText.c_str());
    return false;
  }
  *outgoingPayloadTextOut = encryptedEnvelopeText;
  return true;
}

/**
 * @brief AES-256-GCMで復号する。
 */
bool decryptAesGcm(const std::vector<uint8_t>& keyBytes,
                   const std::vector<uint8_t>& ivBytes,
                   const std::vector<uint8_t>& cipherBytes,
                   const std::vector<uint8_t>& tagBytes,
                   String* plainTextOut) {
  if (plainTextOut == nullptr) {
    appLogError("decryptAesGcm failed. plainTextOut is null.");
    return false;
  }
  std::vector<uint8_t> plainBuffer(cipherBytes.size(), 0);
  mbedtls_gcm_context gcmContext;
  mbedtls_gcm_init(&gcmContext);
  int32_t keyResult = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, keyBytes.data(), 256);
  if (keyResult != 0) {
    appLogError("decryptAesGcm failed. setkey result=%ld", static_cast<long>(keyResult));
    mbedtls_gcm_free(&gcmContext);
    return false;
  }
  int32_t decryptResult = mbedtls_gcm_auth_decrypt(&gcmContext,
                                                   cipherBytes.size(),
                                                   ivBytes.data(),
                                                   ivBytes.size(),
                                                   nullptr,
                                                   0,
                                                   tagBytes.data(),
                                                   tagBytes.size(),
                                                   cipherBytes.data(),
                                                   plainBuffer.data());
  mbedtls_gcm_free(&gcmContext);
  if (decryptResult != 0) {
    appLogError("decryptAesGcm failed. auth_decrypt result=%ld", static_cast<long>(decryptResult));
    return false;
  }
  *plainTextOut = String(reinterpret_cast<const char*>(plainBuffer.data()), plainBuffer.size());
  return true;
}

/**
 * @brief AES-256-GCMで暗号化する。
 */
bool encryptAesGcm(const std::vector<uint8_t>& keyBytes,
                   const String& plainText,
                   std::vector<uint8_t>* ivBytesOut,
                   std::vector<uint8_t>* cipherBytesOut,
                   std::vector<uint8_t>* tagBytesOut) {
  if (ivBytesOut == nullptr || cipherBytesOut == nullptr || tagBytesOut == nullptr) {
    appLogError("encryptAesGcm failed. output parameter is null.");
    return false;
  }
  ivBytesOut->assign(12, 0);
  esp_fill_random(ivBytesOut->data(), ivBytesOut->size());
  cipherBytesOut->assign(plainText.length(), 0);
  tagBytesOut->assign(16, 0);
  mbedtls_gcm_context gcmContext;
  mbedtls_gcm_init(&gcmContext);
  int32_t keyResult = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, keyBytes.data(), 256);
  if (keyResult != 0) {
    appLogError("encryptAesGcm failed. setkey result=%ld", static_cast<long>(keyResult));
    mbedtls_gcm_free(&gcmContext);
    return false;
  }
  int32_t encryptResult = mbedtls_gcm_crypt_and_tag(&gcmContext,
                                                    MBEDTLS_GCM_ENCRYPT,
                                                    plainText.length(),
                                                    ivBytesOut->data(),
                                                    ivBytesOut->size(),
                                                    nullptr,
                                                    0,
                                                    reinterpret_cast<const unsigned char*>(plainText.c_str()),
                                                    cipherBytesOut->data(),
                                                    tagBytesOut->size(),
                                                    tagBytesOut->data());
  mbedtls_gcm_free(&gcmContext);
  if (encryptResult != 0) {
    appLogError("encryptAesGcm failed. crypt_and_tag result=%ld", static_cast<long>(encryptResult));
    return false;
  }
  return true;
}


/**
 * @brief ESP32のeFuse由来Base MACを区切りなし16進文字列へ変換する。
 * @param compactMacTextOut 出力先文字列ポインタ。
 * @return 変換成功時true、失敗時false。
 */
bool createCompactMacAddressText(String* compactMacTextOut) {
  if (compactMacTextOut == nullptr) {
    appLogError("createCompactMacAddressText failed. compactMacTextOut is null.");
    return false;
  }
  uint64_t efuseMac = ESP.getEfuseMac();
  char compactMacBuffer[13] = {};
  int writtenLength = snprintf(compactMacBuffer,
                               sizeof(compactMacBuffer),
                               "%02X%02X%02X%02X%02X%02X",
                               static_cast<unsigned>((efuseMac >> 40) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 32) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 24) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 16) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 8) & 0xFF),
                               static_cast<unsigned>(efuseMac & 0xFF));
  if (writtenLength <= 0 || writtenLength >= static_cast<int>(sizeof(compactMacBuffer))) {
    appLogError("createCompactMacAddressText failed. snprintf overflow. writtenLength=%d", writtenLength);
    return false;
  }
  *compactMacTextOut = String(compactMacBuffer);
  return true;
}

/**
 * @brief ESP32のeFuse由来Base MACをコロン区切り16進文字列へ変換する。
 * @param macTextOut 出力先文字列ポインタ。
 * @return 変換成功時true、失敗時false。
 */
bool createEfuseMacAddressText(String* macTextOut) {
  if (macTextOut == nullptr) {
    appLogError("createEfuseMacAddressText failed. macTextOut is null.");
    return false;
  }
  uint64_t efuseMac = ESP.getEfuseMac();
  char macBuffer[18] = {};
  int writtenLength = snprintf(macBuffer,
                               sizeof(macBuffer),
                               "%02X:%02X:%02X:%02X:%02X:%02X",
                               static_cast<unsigned>((efuseMac >> 40) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 32) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 24) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 16) & 0xFF),
                               static_cast<unsigned>((efuseMac >> 8) & 0xFF),
                               static_cast<unsigned>(efuseMac & 0xFF));
  if (writtenLength <= 0 || writtenLength >= static_cast<int>(sizeof(macBuffer))) {
    appLogError("createEfuseMacAddressText failed. snprintf overflow. writtenLength=%d", writtenLength);
    return false;
  }
  *macTextOut = String(macBuffer);
  return true;
}

/**
 * @brief UTCエポックミリ秒をISO8601(ミリ秒付き)文字列へ変換する。
 * @param utcEpochMillis UTCエポックミリ秒。
 * @param iso8601TextOut 変換結果の出力先。
 * @return 変換成功時true、失敗時false。
 */
bool formatUtcIso8601FromEpochMillis(int64_t utcEpochMillis, String* iso8601TextOut) {
  if (iso8601TextOut == nullptr) {
    appLogError("formatUtcIso8601FromEpochMillis failed. iso8601TextOut is null.");
    return false;
  }
  if (utcEpochMillis < 0) {
    utcEpochMillis = 0;
  }
  const time_t epochSeconds = static_cast<time_t>(utcEpochMillis / 1000LL);
  const int64_t millisecondPart = utcEpochMillis % 1000LL;
  struct tm utcTimeInfo {};
  if (gmtime_r(&epochSeconds, &utcTimeInfo) == nullptr) {
    appLogError("formatUtcIso8601FromEpochMillis failed. gmtime_r returned null. utcEpochMillis=%lld",
                static_cast<long long>(utcEpochMillis));
    return false;
  }
  char dateTimeBuffer[32] = {};
  size_t dateTimeLength = strftime(dateTimeBuffer, sizeof(dateTimeBuffer), "%Y-%m-%dT%H:%M:%S", &utcTimeInfo);
  if (dateTimeLength == 0) {
    appLogError("formatUtcIso8601FromEpochMillis failed. strftime returned 0. utcEpochMillis=%lld",
                static_cast<long long>(utcEpochMillis));
    return false;
  }
  char iso8601Buffer[40] = {};
  int printLength = snprintf(iso8601Buffer,
                             sizeof(iso8601Buffer),
                             "%s.%03lldZ",
                             dateTimeBuffer,
                             static_cast<long long>(millisecondPart));
  if (printLength <= 0 || printLength >= static_cast<int>(sizeof(iso8601Buffer))) {
    appLogError("formatUtcIso8601FromEpochMillis failed. snprintf overflow. printLength=%d", printLength);
    return false;
  }
  *iso8601TextOut = String(iso8601Buffer);
  return true;
}

/**
 * @brief Willフォールバック用の時刻文字列を計算する。
 * @details UTCが未同期の場合は空文字を返す。
 * @param startupCpuMillis mainTask開始時CPU時刻(ms)。
 * @param currentTsOut tsフィールド出力先。
 * @param startupTsOut startUpTimeフィールド出力先。
 * @return UTCが有効で両方計算できた場合true、未同期または失敗時false。
 */
bool buildWillFallbackTimeText(uint32_t startupCpuMillis, String* currentTsOut, String* startupTsOut) {
  if (currentTsOut == nullptr || startupTsOut == nullptr) {
    appLogError("buildWillFallbackTimeText failed. output parameter is null. currentTsOut=%p startupTsOut=%p",
                currentTsOut,
                startupTsOut);
    return false;
  }
  *currentTsOut = "";
  *startupTsOut = "";

  struct timeval currentTimeValue {};
  int getTimeResult = gettimeofday(&currentTimeValue, nullptr);
  if (getTimeResult != 0) {
    appLogWarn("buildWillFallbackTimeText skipped. gettimeofday failed. result=%d", getTimeResult);
    return false;
  }
  const int64_t currentUtcEpochMillis = static_cast<int64_t>(currentTimeValue.tv_sec) * 1000LL +
                                        static_cast<int64_t>(currentTimeValue.tv_usec) / 1000LL;
  if (currentUtcEpochMillis < minimumValidUtcEpochMillis) {
    appLogInfo("buildWillFallbackTimeText skipped. utc is not synchronized. epochMillis=%lld",
               static_cast<long long>(currentUtcEpochMillis));
    return false;
  }
  uint32_t currentCpuMillis = millis();
  uint32_t elapsedCpuMillis = (currentCpuMillis >= startupCpuMillis) ? (currentCpuMillis - startupCpuMillis) : 0;
  int64_t startupUtcEpochMillis = currentUtcEpochMillis - static_cast<int64_t>(elapsedCpuMillis);
  if (startupUtcEpochMillis < 0) {
    startupUtcEpochMillis = 0;
  }

  String currentTsText;
  String startupTsText;
  if (!formatUtcIso8601FromEpochMillis(currentUtcEpochMillis, &currentTsText)) {
    return false;
  }
  if (!formatUtcIso8601FromEpochMillis(startupUtcEpochMillis, &startupTsText)) {
    return false;
  }
  *currentTsOut = currentTsText;
  *startupTsOut = startupTsText;
  return true;
}

/**
 * @brief MQTT通信で使用するトランスポートクライアントを設定する。
 * @param tlsEnabled TLS有効ならtrue。
 * @return 設定成功時true、失敗時false。
 */
bool configureMqttTransportClient(bool tlsEnabled) {
  if (tlsEnabled) {
    // [厳守] 証明書検証を無効化しない。保存済み証明書があればそれを優先し、なければヘッダー定義へフォールバックする。
    if (!ensureMqttSensitiveDataReady()) {
      appLogError("configureMqttTransportClient failed. ensureMqttSensitiveDataReady returned false.");
      return false;
    }
    String certIssueNo;
    String certSetAt;
    String storedMqttTlsCaCert;
    if (!mqttSensitiveDataService.loadMqttTlsCertificate(&storedMqttTlsCaCert, &certIssueNo, &certSetAt)) {
      appLogError("configureMqttTransportClient failed. loadMqttTlsCertificate returned false.");
      return false;
    }
    if (storedMqttTlsCaCert.length() > 0) {
      mqttTlsCaCertRuntime = storedMqttTlsCaCert;
      appLogInfo("configureMqttTransportClient: use stored MQTT TLS CA cert. issueNo=%s setAt=%s",
                 certIssueNo.c_str(),
                 certSetAt.c_str());
    } else {
#if !defined(SENSITIVE_MQTT_TLS_CA_CERT)
      appLogError("configureMqttTransportClient failed. SENSITIVE_MQTT_TLS_CA_CERT is not defined.");
      return false;
#else
      if (strlen(SENSITIVE_MQTT_TLS_CA_CERT) == 0) {
        appLogError("configureMqttTransportClient failed. no stored cert and SENSITIVE_MQTT_TLS_CA_CERT is empty.");
        return false;
      }
      mqttTlsCaCertRuntime = String(SENSITIVE_MQTT_TLS_CA_CERT);
      appLogWarn("configureMqttTransportClient: fallback to header MQTT TLS CA cert.");
#endif
    }
    mqttTlsNetworkClient.setCACert(mqttTlsCaCertRuntime.c_str());
    mqttTlsNetworkClient.setTlsValidationContext(mqttHost, mqttTlsCaCertRuntime.c_str());
    mqttClient.setClient(mqttTlsNetworkClient);
    appLogInfo("configureMqttTransportClient: TLS transport selected. tlsHost=%s", mqttHost);
    return true;
  }
  mqttClient.setClient(mqttNetworkClient);
  appLogInfo("configureMqttTransportClient: plain TCP transport selected.");
  return true;
}

/**
 * @brief デバイス名（トピックの末尾識別子）を解決する。
 * @param deviceNameTextOut 出力先（null不可）。
 * @return 解決成功時true、失敗時false。
 */
bool resolveDeviceNodeName(String* deviceNameTextOut) {
  if (deviceNameTextOut == nullptr) {
    appLogError("resolveDeviceNodeName failed. deviceNameTextOut is null.");
    return false;
  }
  // [重要] デバイス名未設定時の標準識別子として IoT_<BaseMacNoColon> を使用する。
  String compactMacText;
  if (!createCompactMacAddressText(&compactMacText)) {
    return false;
  }
  *deviceNameTextOut = String("IoT_") + compactMacText;
  return true;
}

/**
 * @brief 共通トピックを生成する（esp32lab/<kind>/<sub>/<name>）。
 * @param kind 種別（notice/set/get/call/network）。
 * @param sub サブコマンド（status等）。
 * @param endpointName 発信者名または受信者名。
 * @param topicTextOut 生成先（null不可）。
 * @return 成功時true、失敗時false。
 */
bool createTopicText(const char* kind, const char* sub, const char* endpointName, String* topicTextOut) {
  if (topicTextOut == nullptr) {
    appLogError("createTopicText failed. topicTextOut is null.");
    return false;
  }
  if (kind == nullptr || sub == nullptr || endpointName == nullptr ||
      strlen(kind) == 0 || strlen(sub) == 0 || strlen(endpointName) == 0) {
    appLogError("createTopicText failed. invalid parameter. kind=%p sub=%p endpointName=%p",
                kind,
                sub,
                endpointName);
    return false;
  }

  char topicBuffer[160];
  int writtenLength = snprintf(topicBuffer,
                               sizeof(topicBuffer),
                               "esp32lab/%s/%s/%s",
                               kind,
                               sub,
                               endpointName);
  if (writtenLength <= 0 || writtenLength >= static_cast<int>(sizeof(topicBuffer))) {
    appLogError("createTopicText failed. snprintf overflow. writtenLength=%ld",
                static_cast<long>(writtenLength));
    return false;
  }
  *topicTextOut = String(topicBuffer);
  return true;
}

/**
 * @brief 旧仕様サブコマンドを現行名称へ正規化する。
 * @param rawSubName 受信サブコマンド。
 * @return 正規化後サブコマンド。
 */
String normalizeSubCommand(const String& rawSubName) {
  if (rawSubName.equalsIgnoreCase(iotCommon::mqtt::subCommand::call::kMaintenanceLegacy)) {
    return String(iotCommon::mqtt::subCommand::call::kMaintenance);
  }
  if (rawSubName.equalsIgnoreCase(iotCommon::mqtt::subCommand::get::kButtonLegacy)) {
    return String(iotCommon::mqtt::subCommand::get::kButton);
  }
  if (rawSubName.equalsIgnoreCase(iotCommon::mqtt::subCommand::get::kGpioLegacy)) {
    return String(iotCommon::mqtt::subCommand::get::kGpio);
  }
  if (rawSubName.equalsIgnoreCase(iotCommon::mqtt::subCommand::set::kGpioHighLegacy)) {
    return String(iotCommon::mqtt::subCommand::set::kGpioHigh);
  }
  if (rawSubName.equalsIgnoreCase(iotCommon::mqtt::subCommand::set::kGpioLowLegacy)) {
    return String(iotCommon::mqtt::subCommand::set::kGpioLow);
  }
  return rawSubName;
}

struct fileSyncPlannedFile {
  String path;
  String action;
  String expectedSha256;
  int32_t expectedSize = 0;
};

struct fileSyncSessionState {
  bool active = false;
  String sessionId;
  String targetArea;
  String deleteMode;
  std::vector<fileSyncPlannedFile> plannedFiles;
};

fileSyncSessionState currentFileSyncSession;

struct imagePackageApplyRequest {
  String sessionId;
  String packageUrl;
  String packageSha256;
  String destinationDir;
  bool overwrite = false;
};

struct imagePackageDownloadResult {
  bool success = false;
  String errorCode;
  String detail;
};

void setImagePackageDownloadFailure(imagePackageDownloadResult* resultOut, const char* errorCode, const String& detail) {
  if (resultOut == nullptr) {
    return;
  }
  resultOut->success = false;
  resultOut->errorCode = errorCode != nullptr ? String(errorCode) : String("");
  resultOut->detail = detail;
}

bool publishFileSyncStatusNotice(const String& destinationId,
                                 const String& sessionId,
                                 const String& targetArea,
                                 const char* phase,
                                 const char* result,
                                 const char* detail,
                                 const char* errorCode) {
  if (!mqttClient.connected()) {
    appLogWarn("publishFileSyncStatusNotice skipped. mqtt is not connected.");
    return false;
  }
  if (deviceNodeName.length() <= 0 && !resolveDeviceNodeName(&deviceNodeName)) {
    appLogError("publishFileSyncStatusNotice failed. resolveDeviceNodeName returned false.");
    return false;
  }
  String topicText;
  if (!createTopicText("notice", "fileSyncStatus", deviceNodeName.c_str(), &topicText)) {
    appLogError("publishFileSyncStatusNotice failed. createTopicText returned false.");
    return false;
  }
  const String messageId = String(deviceNodeName) + "-" + millis();
  String payloadText;
  jsonService payloadJsonService;
  jsonKeyValueItem itemList[] = {
      {"v", jsonValueType::kString, iotCommon::kProtocolVersion, 0, 0, false},
      {"DstID", jsonValueType::kString, destinationId.length() > 0 ? destinationId.c_str() : "all", 0, 0, false},
      {"SrcID", jsonValueType::kString, deviceNodeName.c_str(), 0, 0, false},
      {"Request", jsonValueType::kString, "Notice", 0, 0, false},
      {"id", jsonValueType::kString, messageId.c_str(), 0, 0, false},
      {"sub", jsonValueType::kString, "fileSyncStatus", 0, 0, false},
      {"sessionId", jsonValueType::kString, sessionId.c_str(), 0, 0, false},
      {"targetArea", jsonValueType::kString, targetArea.c_str(), 0, 0, false},
      {"phase", jsonValueType::kString, phase == nullptr ? "" : phase, 0, 0, false},
      {"result", jsonValueType::kString, result == nullptr ? "" : result, 0, 0, false},
      {"detail", jsonValueType::kString, detail == nullptr ? "" : detail, 0, 0, false},
      {"errorCode", jsonValueType::kString, errorCode == nullptr ? "" : errorCode, 0, 0, false},
  };
  if (!payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]))) {
    appLogError("publishFileSyncStatusNotice failed. setValuesByPath returned false.");
    return false;
  }
  String outgoingPayloadText;
  if (!resolveOutgoingPayloadText(topicText, payloadText, &outgoingPayloadText)) {
    appLogError("publishFileSyncStatusNotice failed. resolveOutgoingPayloadText returned false.");
    return false;
  }
  const bool publishResult = mqttClient.publish(topicText.c_str(), outgoingPayloadText.c_str(), false);
  if (!publishResult) {
    appLogError("publishFileSyncStatusNotice failed. publish returned false. topic=%s", topicText.c_str());
    return false;
  }
  mqttClient.loop();
  return true;
}

/**
 * @brief imagePackageApplyの進捗通知をpublishする。
 * @param destinationId 通知先ID。
 * @param sessionId セッションID。
 * @param destinationDir 展開先ディレクトリ。
 * @param phase フェーズ。
 * @param result 結果（OK/NG）。
 * @param detail 詳細メッセージ。
 * @param errorCode エラーコード。
 * @return publish成功時true。
 */
bool publishImagePackageStatusNotice(const String& destinationId,
                                     const String& sessionId,
                                     const String& destinationDir,
                                     const char* phase,
                                     const char* result,
                                     const char* detail,
                                     const char* errorCode) {
  if (!mqttClient.connected()) {
    appLogWarn("publishImagePackageStatusNotice skipped. mqtt is not connected.");
    return false;
  }
  if (deviceNodeName.length() <= 0 && !resolveDeviceNodeName(&deviceNodeName)) {
    appLogError("publishImagePackageStatusNotice failed. resolveDeviceNodeName returned false.");
    return false;
  }
  String topicText;
  if (!createTopicText("notice", "imagePackageStatus", deviceNodeName.c_str(), &topicText)) {
    appLogError("publishImagePackageStatusNotice failed. createTopicText returned false.");
    return false;
  }
  const String messageId = String(deviceNodeName) + "-" + millis();
  String payloadText;
  jsonService payloadJsonService;
  jsonKeyValueItem itemList[] = {
      {"v", jsonValueType::kString, iotCommon::kProtocolVersion, 0, 0, false},
      {"DstID", jsonValueType::kString, destinationId.length() > 0 ? destinationId.c_str() : "all", 0, 0, false},
      {"SrcID", jsonValueType::kString, deviceNodeName.c_str(), 0, 0, false},
      {"Request", jsonValueType::kString, "Notice", 0, 0, false},
      {"id", jsonValueType::kString, messageId.c_str(), 0, 0, false},
      {"sub", jsonValueType::kString, "imagePackageStatus", 0, 0, false},
      {"sessionId", jsonValueType::kString, sessionId.c_str(), 0, 0, false},
      {"destinationDir", jsonValueType::kString, destinationDir.c_str(), 0, 0, false},
      {"phase", jsonValueType::kString, phase == nullptr ? "" : phase, 0, 0, false},
      {"result", jsonValueType::kString, result == nullptr ? "" : result, 0, 0, false},
      {"detail", jsonValueType::kString, detail == nullptr ? "" : detail, 0, 0, false},
      {"errorCode", jsonValueType::kString, errorCode == nullptr ? "" : errorCode, 0, 0, false},
  };
  if (!payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]))) {
    appLogError("publishImagePackageStatusNotice failed. setValuesByPath returned false.");
    return false;
  }
  String outgoingPayloadText;
  if (!resolveOutgoingPayloadText(topicText, payloadText, &outgoingPayloadText)) {
    appLogError("publishImagePackageStatusNotice failed. resolveOutgoingPayloadText returned false.");
    return false;
  }
  const bool publishResult = mqttClient.publish(topicText.c_str(), outgoingPayloadText.c_str(), false);
  if (!publishResult) {
    appLogError("publishImagePackageStatusNotice failed. publish returned false. topic=%s", topicText.c_str());
    return false;
  }
  mqttClient.loop();
  return true;
}

void extractFileSyncContext(const String& payloadText, String* sessionIdOut, String* targetAreaOut) {
  if (sessionIdOut == nullptr || targetAreaOut == nullptr) {
    return;
  }
  *sessionIdOut = "";
  *targetAreaOut = "";
  jsonService payloadJsonService;
  payloadJsonService.getValueByPath(payloadText, "args.sessionId", sessionIdOut);
  payloadJsonService.getValueByPath(payloadText, "args.targetArea", targetAreaOut);
}

void extractImagePackageContext(const String& payloadText, String* sessionIdOut, String* destinationDirOut) {
  if (sessionIdOut == nullptr || destinationDirOut == nullptr) {
    return;
  }
  *sessionIdOut = "";
  *destinationDirOut = "";
  jsonService payloadJsonService;
  payloadJsonService.getValueByPath(payloadText, "args.sessionId", sessionIdOut);
  payloadJsonService.getValueByPath(payloadText, "args.destinationDir", destinationDirOut);
}

bool ensureLittleFsReadyForFileSync() {
  static bool littleFsReady = false;
  if (littleFsReady) {
    return true;
  }
  littleFsReady = LittleFS.begin(false);
  if (!littleFsReady) {
    appLogError("ensureLittleFsReadyForFileSync failed. LittleFS.begin returned false.");
  }
  return littleFsReady;
}

bool resolveAreaRootPath(const String& targetArea, String* areaRootOut) {
  if (areaRootOut == nullptr) {
    return false;
  }
  if (targetArea.equalsIgnoreCase("images")) {
    *areaRootOut = "/images";
    return true;
  }
  if (targetArea.equalsIgnoreCase("certs")) {
    *areaRootOut = "/certs";
    return true;
  }
  return false;
}

bool ensureDirectoryPathExists(const String& directoryPath) {
  if (directoryPath.length() == 0) {
    return false;
  }
  if (LittleFS.exists(directoryPath)) {
    return true;
  }
  int32_t slashIndex = directoryPath.indexOf('/', 1);
  while (slashIndex >= 0) {
    String currentPath = directoryPath.substring(0, slashIndex);
    if (currentPath.length() > 0 && !LittleFS.exists(currentPath)) {
      if (!LittleFS.mkdir(currentPath)) {
        appLogError("ensureDirectoryPathExists failed. mkdir path=%s", currentPath.c_str());
        return false;
      }
    }
    slashIndex = directoryPath.indexOf('/', slashIndex + 1);
  }
  if (!LittleFS.exists(directoryPath)) {
    if (!LittleFS.mkdir(directoryPath)) {
      appLogError("ensureDirectoryPathExists failed. mkdir final path=%s", directoryPath.c_str());
      return false;
    }
  }
  return true;
}

bool normalizeManagedFilePath(const String& targetArea, const String& requestedPath, String* normalizedPathOut) {
  if (normalizedPathOut == nullptr) {
    return false;
  }
  String areaRoot;
  if (!resolveAreaRootPath(targetArea, &areaRoot)) {
    return false;
  }
  String normalizedPath = requestedPath;
  normalizedPath.trim();
  if (normalizedPath.length() == 0) {
    return false;
  }
  if (!normalizedPath.startsWith("/")) {
    normalizedPath = String("/") + normalizedPath;
  }
  while (normalizedPath.indexOf("//") >= 0) {
    normalizedPath.replace("//", "/");
  }
  if (normalizedPath.indexOf("..") >= 0) {
    appLogError("normalizeManagedFilePath failed. invalid traversal path=%s", normalizedPath.c_str());
    return false;
  }
  if (!normalizedPath.startsWith(areaRoot + "/") && normalizedPath != areaRoot) {
    appLogError("normalizeManagedFilePath failed. path out of area. targetArea=%s path=%s",
                targetArea.c_str(),
                normalizedPath.c_str());
    return false;
  }
  *normalizedPathOut = normalizedPath;
  return true;
}

/** @brief `targetArea=certs` で管理する MQTT CA 証明書の固定パス。 */
constexpr const char* mqttTlsManagedCertPath = "/certs/mqtt-ca.pem";

/**
 * @brief path が MQTT CA 証明書パスか判定する。
 * @param path 判定対象パス。
 * @return 一致時true。
 */
bool isMqttTlsManagedCertPath(const String& path) {
  return path.equalsIgnoreCase(mqttTlsManagedCertPath);
}

/**
 * @brief fileSyncで更新後の MQTT CA 証明書メタ情報を NVS へ同期する。
 * @param sessionId fileSync セッションID。
 * @return 同期成功時true。
 */
bool syncMqttTlsCertificateMetadataAfterFileSync(const String& sessionId) {
  if (!ensureMqttSensitiveDataReady()) {
    appLogError("syncMqttTlsCertificateMetadataAfterFileSync failed. sensitiveDataService is not ready. sessionId=%s",
                sessionId.c_str());
    return false;
  }

  String certSetAt = "";
  struct timeval currentTimeValue {};
  if (gettimeofday(&currentTimeValue, nullptr) == 0) {
    const int64_t utcEpochMillis =
        static_cast<int64_t>(currentTimeValue.tv_sec) * 1000LL +
        static_cast<int64_t>(currentTimeValue.tv_usec / 1000L);
    if (!formatUtcIso8601FromEpochMillis(utcEpochMillis, &certSetAt)) {
      certSetAt = "";
    }
  }
  String certIssueNo = String("filesync-") + sessionId;
  bool syncResult = mqttSensitiveDataService.syncMqttTlsCertificateMetadataFromLittleFs(certIssueNo, certSetAt);
  if (!syncResult) {
    appLogError("syncMqttTlsCertificateMetadataAfterFileSync failed. syncMqttTlsCertificateMetadataFromLittleFs returned false. sessionId=%s certIssueNo=%s certSetAt=%s",
                sessionId.c_str(),
                certIssueNo.c_str(),
                certSetAt.c_str());
    return false;
  }
  appLogInfo("syncMqttTlsCertificateMetadataAfterFileSync completed. sessionId=%s certIssueNo=%s certSetAt=%s",
             sessionId.c_str(),
             certIssueNo.c_str(),
             certSetAt.c_str());
  return true;
}

/**
 * @brief imagePackageApply向けの展開先ディレクトリを正規化・検証する。
 * @param destinationDir 指定された展開先ディレクトリ。
 * @param normalizedDestinationOut 正規化後ディレクトリ出力先。
 * @return 検証成功時true。
 */
bool normalizeImagePackageDestinationDir(const String& destinationDir, String* normalizedDestinationOut) {
  if (normalizedDestinationOut == nullptr) {
    appLogError("normalizeImagePackageDestinationDir failed. normalizedDestinationOut is null.");
    return false;
  }
  String normalizedDestination = destinationDir;
  normalizedDestination.trim();
  if (normalizedDestination.length() == 0) {
    appLogError("normalizeImagePackageDestinationDir failed. destinationDir is empty.");
    return false;
  }
  if (!normalizedDestination.startsWith("/")) {
    normalizedDestination = String("/") + normalizedDestination;
  }
  while (normalizedDestination.indexOf("//") >= 0) {
    normalizedDestination.replace("//", "/");
  }
  if (normalizedDestination.indexOf("..") >= 0) {
    appLogError("normalizeImagePackageDestinationDir failed. traversal detected. destinationDir=%s", normalizedDestination.c_str());
    return false;
  }
  if (normalizedDestination.equalsIgnoreCase("/logs") || normalizedDestination.startsWith("/logs/")) {
    appLogError("normalizeImagePackageDestinationDir failed. /logs is prohibited. destinationDir=%s", normalizedDestination.c_str());
    return false;
  }
  if (!(normalizedDestination.equalsIgnoreCase("/images") || normalizedDestination.startsWith("/images/"))) {
    appLogError("normalizeImagePackageDestinationDir failed. out of /images scope. destinationDir=%s", normalizedDestination.c_str());
    return false;
  }
  *normalizedDestinationOut = normalizedDestination;
  return true;
}

String normalizeSha256Hex(const String& hashText) {
  String normalizedHash = hashText;
  normalizedHash.trim();
  normalizedHash.toLowerCase();
  return normalizedHash;
}

bool parseImagePackageApplyRequest(const String& payloadText, imagePackageApplyRequest* requestOut) {
  if (requestOut == nullptr) {
    appLogError("parseImagePackageApplyRequest failed. requestOut is null.");
    return false;
  }
  cJSON* rootObject = cJSON_Parse(payloadText.c_str());
  if (rootObject == nullptr) {
    appLogError("parseImagePackageApplyRequest failed. payload parse error.");
    return false;
  }
  cJSON* argsObject = cJSON_GetObjectItemCaseSensitive(rootObject, "args");
  if (!cJSON_IsObject(argsObject)) {
    appLogError("parseImagePackageApplyRequest failed. args object is missing.");
    cJSON_Delete(rootObject);
    return false;
  }
  imagePackageApplyRequest request;
  cJSON* sessionIdItem = cJSON_GetObjectItemCaseSensitive(argsObject, "sessionId");
  cJSON* packageUrlItem = cJSON_GetObjectItemCaseSensitive(argsObject, "packageUrl");
  cJSON* packageSha256Item = cJSON_GetObjectItemCaseSensitive(argsObject, "packageSha256");
  cJSON* destinationDirItem = cJSON_GetObjectItemCaseSensitive(argsObject, "destinationDir");
  cJSON* overwriteItem = cJSON_GetObjectItemCaseSensitive(argsObject, "overwrite");
  request.sessionId = cJSON_IsString(sessionIdItem) ? String(sessionIdItem->valuestring) : String("");
  request.packageUrl = cJSON_IsString(packageUrlItem) ? String(packageUrlItem->valuestring) : String("");
  request.packageSha256 = cJSON_IsString(packageSha256Item) ? String(packageSha256Item->valuestring) : String("");
  request.destinationDir = cJSON_IsString(destinationDirItem) ? String(destinationDirItem->valuestring) : String("");
  request.overwrite = cJSON_IsBool(overwriteItem) ? cJSON_IsTrue(overwriteItem) : false;
  cJSON_Delete(rootObject);

  if (request.sessionId.length() == 0 ||
      request.packageUrl.length() == 0 ||
      request.packageSha256.length() == 0 ||
      request.destinationDir.length() == 0) {
    appLogError("parseImagePackageApplyRequest failed. required fields are missing. sessionId=%s packageUrlLength=%ld packageSha256Length=%ld destinationDir=%s",
                request.sessionId.c_str(),
                static_cast<long>(request.packageUrl.length()),
                static_cast<long>(request.packageSha256.length()),
                request.destinationDir.c_str());
    return false;
  }
  *requestOut = request;
  return true;
}

/**
 * @brief HTTPSでZIPを取得しLittleFSへ保存しながらSHA-256を検証する。
 * @param request imagePackageApply要求。
 * @param tempZipPathOut 保存した一時ZIPパス出力先。
 * @return 成功時true。
 */
bool downloadImagePackageZip(const imagePackageApplyRequest& request,
                             String* tempZipPathOut,
                             imagePackageDownloadResult* downloadResultOut) {
  if (downloadResultOut != nullptr) {
    downloadResultOut->success = false;
    downloadResultOut->errorCode = "";
    downloadResultOut->detail = "";
  }
  if (tempZipPathOut == nullptr) {
    appLogError("downloadImagePackageZip failed. tempZipPathOut is null.");
    setImagePackageDownloadFailure(downloadResultOut, "IPKG_INTERNAL_ARGUMENT_ERROR", "tempZipPathOut is null");
    return false;
  }
  if (!ensureLittleFsReadyForFileSync()) {
    appLogError("downloadImagePackageZip failed. LittleFS is not ready.");
    setImagePackageDownloadFailure(downloadResultOut, "IPKG_FILESYSTEM_NOT_READY", "LittleFS is not ready");
    return false;
  }
  if (!request.packageUrl.startsWith("https://")) {
    appLogError("downloadImagePackageZip failed. HTTPS is required. packageUrl=%s", request.packageUrl.c_str());
    setImagePackageDownloadFailure(downloadResultOut, "IPKG_HTTPS_REQUIRED", "packageUrl must start with https://");
    return false;
  }

  if (!ensureMqttSensitiveDataReady()) {
    appLogError("downloadImagePackageZip failed. sensitive data service is not ready.");
    setImagePackageDownloadFailure(downloadResultOut, "IPKG_SENSITIVE_DATA_NOT_READY", "sensitive data service is not ready");
    return false;
  }
  String tlsCaCertificate;
  String certIssueNo;
  String certSetAt;
  const bool loadedStoredCertificate = mqttSensitiveDataService.loadMqttTlsCertificate(&tlsCaCertificate, &certIssueNo, &certSetAt);
  if (!loadedStoredCertificate || tlsCaCertificate.length() == 0) {
#if defined(SENSITIVE_MQTT_TLS_CA_CERT)
    if (strlen(SENSITIVE_MQTT_TLS_CA_CERT) > 0) {
      // [重要] `/certs` の証明書メタ不整合時でも、ヘッダー証明書で復旧経路を確保する。
      tlsCaCertificate = String(SENSITIVE_MQTT_TLS_CA_CERT);
      appLogWarn("downloadImagePackageZip: fallback to header MQTT TLS CA certificate. loadedStoredCertificate=%d certLength=%ld",
                 loadedStoredCertificate ? 1 : 0,
                 static_cast<long>(tlsCaCertificate.length()));
    } else {
      appLogError("downloadImagePackageZip failed. MQTT TLS CA certificate is unavailable.");
      setImagePackageDownloadFailure(downloadResultOut, "IPKG_TLS_CA_UNAVAILABLE", "MQTT TLS CA certificate is unavailable");
      return false;
    }
#else
    appLogError("downloadImagePackageZip failed. MQTT TLS CA certificate is unavailable.");
    setImagePackageDownloadFailure(downloadResultOut, "IPKG_TLS_CA_UNAVAILABLE", "MQTT TLS CA certificate is unavailable");
    return false;
#endif
  }

  if (!ensureDirectoryPathExists("/images/.tmp")) {
    appLogError("downloadImagePackageZip failed. /images/.tmp directory create failed.");
    setImagePackageDownloadFailure(downloadResultOut, "IPKG_TEMP_DIR_CREATE_FAILED", "/images/.tmp directory create failed");
    return false;
  }

  String tempZipPath = String("/images/.tmp/imagePackage-") + request.sessionId + ".zip.tmp";
  LittleFS.remove(tempZipPath);

  MqttTlsClient secureClient;
  secureClient.setCACert(tlsCaCertificate.c_str());
  {
    String urlWithoutScheme = request.packageUrl.substring(strlen("https://"));
    int32_t slashIndex = urlWithoutScheme.indexOf('/');
    String authorityPart = slashIndex >= 0 ? urlWithoutScheme.substring(0, slashIndex) : urlWithoutScheme;
    int32_t colonIndex = authorityPart.indexOf(':');
    String requestHost = colonIndex >= 0 ? authorityPart.substring(0, colonIndex) : authorityPart;
    requestHost.trim();
    secureClient.setTlsValidationContext(requestHost.c_str(), tlsCaCertificate.c_str());
    IPAddress dnsPrimaryFallbackIp(
        SENSITIVE_WIFI_DNS_PRIMARY_OCTET1,
        SENSITIVE_WIFI_DNS_PRIMARY_OCTET2,
        SENSITIVE_WIFI_DNS_PRIMARY_OCTET3,
        SENSITIVE_WIFI_DNS_PRIMARY_OCTET4);
    if (mqttHost != nullptr &&
        requestHost.equalsIgnoreCase(String(mqttHost)) &&
        dnsPrimaryFallbackIp != IPAddress(0, 0, 0, 0)) {
      secureClient.setFallbackEndpointIp(dnsPrimaryFallbackIp);
      appLogInfo("downloadImagePackageZip: host fallback IP enabled. host=%s fallbackIp=%s",
                 requestHost.c_str(),
                 dnsPrimaryFallbackIp.toString().c_str());
    }
  }

  HTTPClient httpClient;
  if (!httpClient.begin(secureClient, request.packageUrl)) {
    appLogError("downloadImagePackageZip failed. HTTPClient::begin returned false. url=%s", request.packageUrl.c_str());
    setImagePackageDownloadFailure(downloadResultOut,
                                   "IPKG_HTTP_BEGIN_FAILED",
                                   String("HTTPClient::begin failed. url=") + request.packageUrl);
    return false;
  }
  httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const int32_t statusCode = httpClient.GET();
  if (statusCode != HTTP_CODE_OK) {
    const String statusReason = HTTPClient::errorToString(statusCode);
    appLogError("downloadImagePackageZip failed. HTTP status is not 200. status=%ld url=%s",
                static_cast<long>(statusCode),
                request.packageUrl.c_str());
    if (statusCode < 0) {
      setImagePackageDownloadFailure(downloadResultOut,
                                     "IPKG_HTTP_CONNECT_FAILED",
                                     String("HTTP GET failed. status=") + String(statusCode) +
                                         " reason=" + statusReason +
                                         " url=" + request.packageUrl);
    } else {
      setImagePackageDownloadFailure(downloadResultOut,
                                     "IPKG_HTTP_STATUS_NOT_OK",
                                     String("HTTP status is not 200. status=") + String(statusCode) +
                                         " url=" + request.packageUrl);
    }
    httpClient.end();
    return false;
  }

  File zipFile = LittleFS.open(tempZipPath, "w");
  if (!zipFile) {
    appLogError("downloadImagePackageZip failed. open temp zip file failed. path=%s", tempZipPath.c_str());
    setImagePackageDownloadFailure(downloadResultOut,
                                   "IPKG_TEMP_FILE_OPEN_FAILED",
                                   String("open temp zip file failed. path=") + tempZipPath);
    httpClient.end();
    return false;
  }

  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  mbedtls_sha256_starts_ret(&shaContext, 0);

  WiFiClient* stream = httpClient.getStreamPtr();
  int32_t remainedLength = httpClient.getSize();
  const uint32_t downloadStartMs = millis();
  uint32_t lastDataReceivedMs = downloadStartMs;
  constexpr uint32_t imagePackageDownloadIdleTimeoutMs = 15000;
  constexpr uint32_t imagePackageDownloadTotalTimeoutMs = 120000;
  size_t totalDownloadedSize = 0;
  // [重要] スタック使用量を抑えるため、I/Oバッファは静的領域を使う。
  static uint8_t downloadBuffer[512];
  while (true) {
    const uint32_t elapsedTotalMs = millis() - downloadStartMs;
    if (elapsedTotalMs > imagePackageDownloadTotalTimeoutMs) {
      appLogError("downloadImagePackageZip failed. total timeout exceeded. elapsedMs=%lu totalDownloaded=%lu remainedLength=%ld",
                  static_cast<unsigned long>(elapsedTotalMs),
                  static_cast<unsigned long>(totalDownloadedSize),
                  static_cast<long>(remainedLength));
      zipFile.close();
      httpClient.end();
      mbedtls_sha256_free(&shaContext);
      LittleFS.remove(tempZipPath);
      setImagePackageDownloadFailure(downloadResultOut,
                                     "IPKG_DOWNLOAD_TOTAL_TIMEOUT",
                                     String("total timeout exceeded. elapsedMs=") + String(elapsedTotalMs) +
                                         " totalDownloaded=" + String(totalDownloadedSize));
      return false;
    }

    size_t availableSize = stream->available();
    if (availableSize == 0) {
      const uint32_t idleMs = millis() - lastDataReceivedMs;
      if (!httpClient.connected()) {
        break;
      }
      if (remainedLength == 0) {
        break;
      }
      if (idleMs > imagePackageDownloadIdleTimeoutMs) {
        appLogError("downloadImagePackageZip failed. idle timeout exceeded. idleMs=%lu totalDownloaded=%lu remainedLength=%ld",
                    static_cast<unsigned long>(idleMs),
                    static_cast<unsigned long>(totalDownloadedSize),
                    static_cast<long>(remainedLength));
        zipFile.close();
        httpClient.end();
        mbedtls_sha256_free(&shaContext);
        LittleFS.remove(tempZipPath);
        setImagePackageDownloadFailure(downloadResultOut,
                                       "IPKG_DOWNLOAD_IDLE_TIMEOUT",
                                       String("idle timeout exceeded. idleMs=") + String(idleMs) +
                                           " totalDownloaded=" + String(totalDownloadedSize));
        return false;
      }
      delay(10);
      continue;
    }
    if (availableSize > sizeof(downloadBuffer)) {
      availableSize = sizeof(downloadBuffer);
    }
    int32_t readSize = stream->readBytes(downloadBuffer, availableSize);
    if (readSize <= 0) {
      break;
    }
    size_t writtenSize = zipFile.write(downloadBuffer, static_cast<size_t>(readSize));
    if (writtenSize != static_cast<size_t>(readSize)) {
      appLogError("downloadImagePackageZip failed. file write mismatch. path=%s expected=%ld actual=%ld",
                  tempZipPath.c_str(),
                  static_cast<long>(readSize),
                  static_cast<long>(writtenSize));
      zipFile.close();
      httpClient.end();
      mbedtls_sha256_free(&shaContext);
      LittleFS.remove(tempZipPath);
      setImagePackageDownloadFailure(downloadResultOut,
                                     "IPKG_TEMP_FILE_WRITE_FAILED",
                                     String("temp zip write mismatch. path=") + tempZipPath +
                                         " expected=" + String(readSize) +
                                         " actual=" + String(writtenSize));
      return false;
    }
    mbedtls_sha256_update_ret(&shaContext, downloadBuffer, static_cast<size_t>(readSize));
    totalDownloadedSize += static_cast<size_t>(readSize);
    lastDataReceivedMs = millis();
    if (remainedLength > 0) {
      remainedLength -= readSize;
      if (remainedLength <= 0) {
        break;
      }
    }
  }

  unsigned char hashBytes[32] = {0};
  mbedtls_sha256_finish_ret(&shaContext, hashBytes);
  mbedtls_sha256_free(&shaContext);
  zipFile.close();
  httpClient.end();

  char hashText[65] = {0};
  for (size_t index = 0; index < 32; ++index) {
    snprintf(&hashText[index * 2], 3, "%02x", hashBytes[index]);
  }
  hashText[64] = '\0';
  const String actualSha256 = normalizeSha256Hex(String(hashText));
  const String expectedSha256 = normalizeSha256Hex(request.packageSha256);
  if (!actualSha256.equals(expectedSha256)) {
    appLogError("downloadImagePackageZip failed. sha256 mismatch. expected=%s actual=%s path=%s",
                expectedSha256.c_str(),
                actualSha256.c_str(),
                tempZipPath.c_str());
    LittleFS.remove(tempZipPath);
    setImagePackageDownloadFailure(downloadResultOut,
                                   "IPKG_SHA256_MISMATCH",
                                   String("sha256 mismatch. expected=") + expectedSha256 +
                                       " actual=" + actualSha256);
    return false;
  }

  *tempZipPathOut = tempZipPath;
  if (downloadResultOut != nullptr) {
    downloadResultOut->success = true;
    downloadResultOut->errorCode = "";
    downloadResultOut->detail = "";
  }
  return true;
}

bool readZipExact(File* file, uint8_t* bufferOut, size_t length) {
  if (file == nullptr || bufferOut == nullptr) {
    appLogError("readZipExact failed. file or buffer is null.");
    return false;
  }
  size_t totalReadSize = 0;
  while (totalReadSize < length) {
    int32_t readSize = file->read(bufferOut + totalReadSize, length - totalReadSize);
    if (readSize <= 0) {
      appLogError("readZipExact failed. readSize=%ld totalReadSize=%ld length=%ld",
                  static_cast<long>(readSize),
                  static_cast<long>(totalReadSize),
                  static_cast<long>(length));
      return false;
    }
    totalReadSize += static_cast<size_t>(readSize);
  }
  return true;
}

bool skipZipBytes(File* file, size_t skipLength) {
  if (file == nullptr) {
    appLogError("skipZipBytes failed. file is null.");
    return false;
  }
  const size_t startPosition = file->position();
  const size_t targetPosition = startPosition + skipLength;
  if (!file->seek(targetPosition)) {
    appLogError("skipZipBytes failed. seek returned false. startPosition=%ld skipLength=%ld targetPosition=%ld",
                static_cast<long>(startPosition),
                static_cast<long>(skipLength),
                static_cast<long>(targetPosition));
    return false;
  }
  return true;
}

uint16_t readLittleEndian16(const uint8_t* buffer) {
  return static_cast<uint16_t>(buffer[0]) | (static_cast<uint16_t>(buffer[1]) << 8);
}

uint32_t readLittleEndian32(const uint8_t* buffer) {
  return static_cast<uint32_t>(buffer[0]) |
         (static_cast<uint32_t>(buffer[1]) << 8) |
         (static_cast<uint32_t>(buffer[2]) << 16) |
         (static_cast<uint32_t>(buffer[3]) << 24);
}

bool normalizeZipEntryRelativePath(const String& entryPath, String* normalizedPathOut, bool* isDirectoryOut) {
  if (normalizedPathOut == nullptr || isDirectoryOut == nullptr) {
    appLogError("normalizeZipEntryRelativePath failed. output parameter is null.");
    return false;
  }
  String normalizedPath = entryPath;
  normalizedPath.trim();
  normalizedPath.replace("\\", "/");
  while (normalizedPath.indexOf("//") >= 0) {
    normalizedPath.replace("//", "/");
  }
  if (normalizedPath.length() == 0) {
    appLogError("normalizeZipEntryRelativePath failed. entryPath is empty.");
    return false;
  }
  if (normalizedPath.startsWith("/")) {
    appLogError("normalizeZipEntryRelativePath failed. absolute path is prohibited. entryPath=%s", normalizedPath.c_str());
    return false;
  }
  bool isDirectory = normalizedPath.endsWith("/");
  if (isDirectory) {
    normalizedPath.remove(normalizedPath.length() - 1);
  }

  String joinedPath = "";
  int32_t segmentStartIndex = 0;
  while (segmentStartIndex <= normalizedPath.length()) {
    int32_t slashIndex = normalizedPath.indexOf('/', segmentStartIndex);
    String segment = (slashIndex >= 0) ? normalizedPath.substring(segmentStartIndex, slashIndex) : normalizedPath.substring(segmentStartIndex);
    segment.trim();
    if (segment.length() == 0 || segment == ".") {
      if (slashIndex < 0) {
        break;
      }
      segmentStartIndex = slashIndex + 1;
      continue;
    }
    if (segment == "..") {
      appLogError("normalizeZipEntryRelativePath failed. traversal segment detected. entryPath=%s", entryPath.c_str());
      return false;
    }
    if (joinedPath.length() > 0) {
      joinedPath += "/";
    }
    joinedPath += segment;
    if (slashIndex < 0) {
      break;
    }
    segmentStartIndex = slashIndex + 1;
  }

  if (joinedPath.length() == 0) {
    appLogError("normalizeZipEntryRelativePath failed. normalized path is empty. entryPath=%s", entryPath.c_str());
    return false;
  }
  *normalizedPathOut = joinedPath;
  *isDirectoryOut = isDirectory;
  return true;
}

/**
 * @brief ZIPパッケージを展開して適用する。
 * @details
 * - [厳守] `destinationDir` 配下にのみ展開し、`..` を含むパスは拒否する。
 * - [厳守] `overwrite=false` 時は既存ファイル上書きを禁止する。
 * - [重要] 現在は ZIP method=0（stored, 無圧縮）のみ対応し、method=8(deflate)等は拒否する。
 * - [重要] 反映時は staged tmp ファイルを `rename` して適用する。
 */
bool applyImagePackageZip(const imagePackageApplyRequest& request, const String& tempZipPath) {
  File zipFile = LittleFS.open(tempZipPath, "r");
  if (!zipFile) {
    appLogError("applyImagePackageZip failed. open zip file failed. tempZipPath=%s", tempZipPath.c_str());
    return false;
  }

  struct stagedZipFileEntry {
    String finalPath;
    String stagedTempPath;
  };
  std::vector<stagedZipFileEntry> stagedEntries;
  auto cleanupStagedEntries = [&stagedEntries]() {
    for (size_t index = 0; index < stagedEntries.size(); ++index) {
      if (LittleFS.exists(stagedEntries[index].stagedTempPath)) {
        LittleFS.remove(stagedEntries[index].stagedTempPath);
      }
    }
  };

  constexpr uint32_t zipLocalFileHeaderSignature = 0x04034b50;
  constexpr uint32_t zipCentralDirectoryHeaderSignature = 0x02014b50;
  constexpr uint32_t zipEndOfCentralDirectorySignature = 0x06054b50;
  int32_t entryIndex = 0;

  while (zipFile.available() > 0) {
    uint8_t signatureBytes[4];
    if (!readZipExact(&zipFile, signatureBytes, sizeof(signatureBytes))) {
      appLogError("applyImagePackageZip failed. signature read failed. entryIndex=%ld", static_cast<long>(entryIndex));
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }
    const uint32_t signature = readLittleEndian32(signatureBytes);
    if (signature == zipCentralDirectoryHeaderSignature || signature == zipEndOfCentralDirectorySignature) {
      break;
    }
    if (signature != zipLocalFileHeaderSignature) {
      appLogError("applyImagePackageZip failed. unsupported signature=0x%08lx entryIndex=%ld",
                  static_cast<unsigned long>(signature),
                  static_cast<long>(entryIndex));
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }

    uint8_t localHeaderBuffer[26];
    if (!readZipExact(&zipFile, localHeaderBuffer, sizeof(localHeaderBuffer))) {
      appLogError("applyImagePackageZip failed. local header read failed. entryIndex=%ld", static_cast<long>(entryIndex));
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }
    const uint16_t generalPurposeFlags = readLittleEndian16(localHeaderBuffer + 2);
    const uint16_t compressionMethod = readLittleEndian16(localHeaderBuffer + 4);
    const uint32_t compressedSize = readLittleEndian32(localHeaderBuffer + 14);
    const uint32_t uncompressedSize = readLittleEndian32(localHeaderBuffer + 18);
    const uint16_t fileNameLength = readLittleEndian16(localHeaderBuffer + 22);
    const uint16_t extraFieldLength = readLittleEndian16(localHeaderBuffer + 24);

    if ((generalPurposeFlags & 0x0008) != 0) {
      appLogError("applyImagePackageZip failed. data descriptor is unsupported. entryIndex=%ld flags=0x%04x",
                  static_cast<long>(entryIndex),
                  static_cast<unsigned>(generalPurposeFlags));
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }
    if ((generalPurposeFlags & 0x0001) != 0) {
      appLogError("applyImagePackageZip failed. encrypted zip entry is unsupported. entryIndex=%ld flags=0x%04x",
                  static_cast<long>(entryIndex),
                  static_cast<unsigned>(generalPurposeFlags));
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }
    if (compressionMethod != 0) {
      appLogError("applyImagePackageZip failed. unsupported compression method. entryIndex=%ld method=%ld (only stored=0 is supported)",
                  static_cast<long>(entryIndex),
                  static_cast<long>(compressionMethod));
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }

    std::vector<uint8_t> fileNameBytes(fileNameLength + 1, 0);
    if (!readZipExact(&zipFile, fileNameBytes.data(), fileNameLength)) {
      appLogError("applyImagePackageZip failed. fileName read failed. entryIndex=%ld fileNameLength=%ld",
                  static_cast<long>(entryIndex),
                  static_cast<long>(fileNameLength));
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }
    String entryPath = String(reinterpret_cast<const char*>(fileNameBytes.data()));
    if (!skipZipBytes(&zipFile, extraFieldLength)) {
      appLogError("applyImagePackageZip failed. extra field skip failed. entryIndex=%ld extraFieldLength=%ld",
                  static_cast<long>(entryIndex),
                  static_cast<long>(extraFieldLength));
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }

    String normalizedRelativePath;
    bool isDirectory = false;
    if (!normalizeZipEntryRelativePath(entryPath, &normalizedRelativePath, &isDirectory)) {
      appLogError("applyImagePackageZip failed. invalid zip entry path. entryIndex=%ld entryPath=%s",
                  static_cast<long>(entryIndex),
                  entryPath.c_str());
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }
    String finalPath = request.destinationDir + "/" + normalizedRelativePath;
    while (finalPath.indexOf("//") >= 0) {
      finalPath.replace("//", "/");
    }
    if (!(finalPath.equals(request.destinationDir) || finalPath.startsWith(request.destinationDir + "/"))) {
      appLogError("applyImagePackageZip failed. entry escaped destinationDir. destinationDir=%s finalPath=%s",
                  request.destinationDir.c_str(),
                  finalPath.c_str());
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }

    if (isDirectory) {
      if (!ensureDirectoryPathExists(finalPath)) {
        appLogError("applyImagePackageZip failed. directory create failed. finalPath=%s", finalPath.c_str());
        zipFile.close();
        cleanupStagedEntries();
        return false;
      }
      if (compressedSize > 0 && !skipZipBytes(&zipFile, compressedSize)) {
        appLogError("applyImagePackageZip failed. directory data skip failed. finalPath=%s compressedSize=%ld",
                    finalPath.c_str(),
                    static_cast<long>(compressedSize));
        zipFile.close();
        cleanupStagedEntries();
        return false;
      }
      ++entryIndex;
      continue;
    }

    if (!request.overwrite && LittleFS.exists(finalPath)) {
      appLogError("applyImagePackageZip failed. overwrite is false but file exists. finalPath=%s", finalPath.c_str());
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }
    const int32_t lastSlashIndex = finalPath.lastIndexOf('/');
    const String parentDirectory = (lastSlashIndex > 0) ? finalPath.substring(0, lastSlashIndex) : String("/");
    if (!ensureDirectoryPathExists(parentDirectory)) {
      appLogError("applyImagePackageZip failed. parent directory create failed. parentDirectory=%s finalPath=%s",
                  parentDirectory.c_str(),
                  finalPath.c_str());
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }

    String stagedTempPath = String("/images/.tmp/ipkg-") + request.sessionId + "-" + String(entryIndex) + ".tmp";
    stagedTempPath.replace("/", "_");
    stagedTempPath = String("/images/.tmp/") + stagedTempPath + ".part";
    if (LittleFS.exists(stagedTempPath)) {
      LittleFS.remove(stagedTempPath);
    }
    File stagedFile = LittleFS.open(stagedTempPath, "w");
    if (!stagedFile) {
      appLogError("applyImagePackageZip failed. open staged file failed. stagedTempPath=%s", stagedTempPath.c_str());
      zipFile.close();
      cleanupStagedEntries();
      return false;
    }

    // [重要] スタック使用量を抑えるため、I/Oバッファは静的領域を使う。
    static uint8_t zipCopyBuffer[512];
    uint32_t remainingDataSize = compressedSize;
    while (remainingDataSize > 0) {
      size_t readLength = remainingDataSize > sizeof(zipCopyBuffer) ? sizeof(zipCopyBuffer) : static_cast<size_t>(remainingDataSize);
      if (!readZipExact(&zipFile, zipCopyBuffer, readLength)) {
        appLogError("applyImagePackageZip failed. entry data read failed. finalPath=%s remainingDataSize=%ld readLength=%ld",
                    finalPath.c_str(),
                    static_cast<long>(remainingDataSize),
                    static_cast<long>(readLength));
        stagedFile.close();
        zipFile.close();
        cleanupStagedEntries();
        LittleFS.remove(stagedTempPath);
        return false;
      }
      size_t writtenSize = stagedFile.write(zipCopyBuffer, readLength);
      if (writtenSize != readLength) {
        appLogError("applyImagePackageZip failed. staged write mismatch. stagedTempPath=%s expected=%ld actual=%ld",
                    stagedTempPath.c_str(),
                    static_cast<long>(readLength),
                    static_cast<long>(writtenSize));
        stagedFile.close();
        zipFile.close();
        cleanupStagedEntries();
        LittleFS.remove(stagedTempPath);
        return false;
      }
      remainingDataSize -= static_cast<uint32_t>(readLength);
    }
    stagedFile.close();
    if (compressedSize != uncompressedSize) {
      appLogError("applyImagePackageZip failed. stored entry size mismatch. finalPath=%s compressedSize=%ld uncompressedSize=%ld",
                  finalPath.c_str(),
                  static_cast<long>(compressedSize),
                  static_cast<long>(uncompressedSize));
      zipFile.close();
      cleanupStagedEntries();
      LittleFS.remove(stagedTempPath);
      return false;
    }

    stagedZipFileEntry stagedEntry;
    stagedEntry.finalPath = finalPath;
    stagedEntry.stagedTempPath = stagedTempPath;
    stagedEntries.push_back(stagedEntry);
    ++entryIndex;
  }
  zipFile.close();

  for (size_t index = 0; index < stagedEntries.size(); ++index) {
    const stagedZipFileEntry& stagedEntry = stagedEntries[index];
    if (LittleFS.exists(stagedEntry.finalPath)) {
      if (!request.overwrite) {
        appLogError("applyImagePackageZip failed. overwrite is false in apply phase. finalPath=%s", stagedEntry.finalPath.c_str());
        cleanupStagedEntries();
        return false;
      }
      if (!LittleFS.remove(stagedEntry.finalPath)) {
        appLogError("applyImagePackageZip failed. existing file remove failed. finalPath=%s", stagedEntry.finalPath.c_str());
        cleanupStagedEntries();
        return false;
      }
    }
    if (!LittleFS.rename(stagedEntry.stagedTempPath, stagedEntry.finalPath)) {
      appLogError("applyImagePackageZip failed. rename staged->final failed. stagedTempPath=%s finalPath=%s",
                  stagedEntry.stagedTempPath.c_str(),
                  stagedEntry.finalPath.c_str());
      cleanupStagedEntries();
      return false;
    }
  }

  appLogInfo("applyImagePackageZip completed. sessionId=%s destinationDir=%s overwrite=%d fileCount=%ld",
             request.sessionId.c_str(),
             request.destinationDir.c_str(),
             request.overwrite ? 1 : 0,
             static_cast<long>(stagedEntries.size()));
  return true;
}

bool handleImagePackageApplyCommand(const String& payloadText, const String& destinationId) {
  imagePackageApplyRequest request;
  if (!parseImagePackageApplyRequest(payloadText, &request)) {
    publishImagePackageStatusNotice(destinationId, "", "", "planning", "NG", "args parse failed", "IPKG_INVALID_ARGS");
    return true;
  }

  String normalizedDestinationDir;
  if (!normalizeImagePackageDestinationDir(request.destinationDir, &normalizedDestinationDir)) {
    publishImagePackageStatusNotice(destinationId,
                                    request.sessionId,
                                    request.destinationDir,
                                    "planning",
                                    "NG",
                                    "invalid destinationDir",
                                    "IPKG_SCOPE_VIOLATION");
    return true;
  }
  request.destinationDir = normalizedDestinationDir;
  if (!ensureDirectoryPathExists(request.destinationDir)) {
    publishImagePackageStatusNotice(destinationId,
                                    request.sessionId,
                                    request.destinationDir,
                                    "planning",
                                    "NG",
                                    "destinationDir create failed",
                                    "IPKG_IO_ERROR");
    return true;
  }
  publishImagePackageStatusNotice(destinationId,
                                  request.sessionId,
                                  request.destinationDir,
                                  "planning",
                                  "OK",
                                  "request accepted",
                                  "");

  String tempZipPath;
  imagePackageDownloadResult downloadResult;
  if (!downloadImagePackageZip(request, &tempZipPath, &downloadResult)) {
    const String errorCode = downloadResult.errorCode.length() > 0 ? downloadResult.errorCode : String("IPKG_DOWNLOAD_OR_HASH_FAILED");
    const String detail = downloadResult.detail.length() > 0 ? downloadResult.detail : String("download or sha256 verification failed");
    publishImagePackageStatusNotice(destinationId,
                                    request.sessionId,
                                    request.destinationDir,
                                    "downloading",
                                    "NG",
                                    detail.c_str(),
                                    errorCode.c_str());
    return true;
  }
  publishImagePackageStatusNotice(destinationId,
                                  request.sessionId,
                                  request.destinationDir,
                                  "downloading",
                                  "OK",
                                  "download completed",
                                  "");

  if (!applyImagePackageZip(request, tempZipPath)) {
    LittleFS.remove(tempZipPath);
    publishImagePackageStatusNotice(destinationId,
                                    request.sessionId,
                                    request.destinationDir,
                                    "extracting",
                                    "NG",
                                    "zip extraction failed",
                                    "IPKG_EXTRACT_FAILED");
    return true;
  }

  LittleFS.remove(tempZipPath);
  publishImagePackageStatusNotice(destinationId,
                                  request.sessionId,
                                  request.destinationDir,
                                  "completed",
                                  "OK",
                                  "image package applied",
                                  "");
  return true;
}

String buildTempFilePath(const String& normalizedPath) {
  return normalizedPath + ".tmp";
}

bool computeLittleFsFileSha256(const String& filePath, String* sha256HexOut) {
  if (sha256HexOut == nullptr) {
    return false;
  }
  File file = LittleFS.open(filePath, "r");
  if (!file) {
    appLogError("computeLittleFsFileSha256 failed. open path=%s", filePath.c_str());
    return false;
  }
  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  mbedtls_sha256_starts_ret(&shaContext, 0);
  // [重要] スタック使用量を抑えるため、I/Oバッファは静的領域を使う。
  static uint8_t sha256Buffer[512];
  while (file.available() > 0) {
    size_t readSize = file.read(sha256Buffer, sizeof(sha256Buffer));
    if (readSize <= 0) {
      break;
    }
    mbedtls_sha256_update_ret(&shaContext, sha256Buffer, readSize);
  }
  file.close();
  unsigned char hashBytes[32] = {0};
  mbedtls_sha256_finish_ret(&shaContext, hashBytes);
  mbedtls_sha256_free(&shaContext);
  char hashText[65] = {0};
  for (size_t index = 0; index < 32; ++index) {
    snprintf(&hashText[index * 2], 3, "%02x", hashBytes[index]);
  }
  hashText[64] = '\0';
  *sha256HexOut = String(hashText);
  return true;
}

bool deletePathRecursively(const String& pathText) {
  File entry = LittleFS.open(pathText, "r");
  if (!entry) {
    return !LittleFS.exists(pathText);
  }
  bool isDirectory = entry.isDirectory();
  if (!isDirectory) {
    entry.close();
    return LittleFS.remove(pathText);
  }
  File child = entry.openNextFile();
  while (child) {
    String childPath = String(child.path());
    child.close();
    if (!deletePathRecursively(childPath)) {
      entry.close();
      return false;
    }
    child = entry.openNextFile();
  }
  entry.close();
  return LittleFS.rmdir(pathText);
}

const fileSyncPlannedFile* findPlannedFile(const String& normalizedPath) {
  for (size_t index = 0; index < currentFileSyncSession.plannedFiles.size(); ++index) {
    if (currentFileSyncSession.plannedFiles[index].path == normalizedPath) {
      return &currentFileSyncSession.plannedFiles[index];
    }
  }
  return nullptr;
}

bool handleFileSyncPlanCommand(const String& payloadText, const String& destinationId) {
  if (!ensureLittleFsReadyForFileSync()) {
    publishFileSyncStatusNotice(destinationId, "", "", "planning", "NG", "LittleFS init failed", "FSYNC_IO_ERROR");
    return true;
  }
  cJSON* rootObject = cJSON_Parse(payloadText.c_str());
  if (rootObject == nullptr) {
    appLogError("handleFileSyncPlanCommand failed. payload parse error.");
    publishFileSyncStatusNotice(destinationId, "", "", "planning", "NG", "payload parse error", "FSYNC_IO_ERROR");
    return true;
  }
  cJSON* argsObject = cJSON_GetObjectItemCaseSensitive(rootObject, "args");
  if (!cJSON_IsObject(argsObject)) {
    appLogError("handleFileSyncPlanCommand failed. args object is missing.");
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId, "", "", "planning", "NG", "args object is missing", "FSYNC_IO_ERROR");
    return true;
  }
  cJSON* sessionIdItem = cJSON_GetObjectItemCaseSensitive(argsObject, "sessionId");
  cJSON* targetAreaItem = cJSON_GetObjectItemCaseSensitive(argsObject, "targetArea");
  cJSON* deleteModeItem = cJSON_GetObjectItemCaseSensitive(argsObject, "deleteMode");
  String sessionId = cJSON_IsString(sessionIdItem) ? String(sessionIdItem->valuestring) : String("");
  String targetArea = cJSON_IsString(targetAreaItem) ? String(targetAreaItem->valuestring) : String("images");
  String deleteMode = cJSON_IsString(deleteModeItem) ? String(deleteModeItem->valuestring) : String("listed");
  String areaRootPath;
  if (sessionId.length() == 0 || !resolveAreaRootPath(targetArea, &areaRootPath)) {
    appLogError("handleFileSyncPlanCommand failed. invalid sessionId/targetArea. sessionId=%s targetArea=%s",
                sessionId.c_str(),
                targetArea.c_str());
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId, sessionId, targetArea, "planning", "NG", "invalid sessionId/targetArea", "FSYNC_SCOPE_VIOLATION");
    return true;
  }
  if (!ensureDirectoryPathExists(areaRootPath)) {
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId, sessionId, targetArea, "planning", "NG", "directory create failed", "FSYNC_IO_ERROR");
    return true;
  }

  std::vector<fileSyncPlannedFile> plannedFiles;
  cJSON* filesArrayItem = cJSON_GetObjectItemCaseSensitive(argsObject, "files");
  if (cJSON_IsArray(filesArrayItem)) {
    int32_t fileCount = cJSON_GetArraySize(filesArrayItem);
    for (int32_t fileIndex = 0; fileIndex < fileCount; ++fileIndex) {
      cJSON* fileObject = cJSON_GetArrayItem(filesArrayItem, fileIndex);
      if (!cJSON_IsObject(fileObject)) {
        continue;
      }
      String rawPath = cJSON_IsString(cJSON_GetObjectItemCaseSensitive(fileObject, "path"))
                           ? String(cJSON_GetObjectItemCaseSensitive(fileObject, "path")->valuestring)
                           : String("");
      String normalizedPath;
      if (!normalizeManagedFilePath(targetArea, rawPath, &normalizedPath)) {
        appLogError("handleFileSyncPlanCommand failed. invalid file path. targetArea=%s path=%s",
                    targetArea.c_str(),
                    rawPath.c_str());
        continue;
      }
      fileSyncPlannedFile plannedFile;
      plannedFile.path = normalizedPath;
      plannedFile.action = cJSON_IsString(cJSON_GetObjectItemCaseSensitive(fileObject, "action"))
                               ? String(cJSON_GetObjectItemCaseSensitive(fileObject, "action")->valuestring)
                               : String("upsert");
      plannedFile.expectedSha256 = cJSON_IsString(cJSON_GetObjectItemCaseSensitive(fileObject, "sha256"))
                                       ? String(cJSON_GetObjectItemCaseSensitive(fileObject, "sha256")->valuestring)
                                       : String("");
      plannedFile.expectedSize = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fileObject, "size"))
                                     ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(fileObject, "size")->valuedouble)
                                     : 0;
      plannedFiles.push_back(plannedFile);
    }
  }

  currentFileSyncSession.active = true;
  currentFileSyncSession.sessionId = sessionId;
  currentFileSyncSession.targetArea = targetArea;
  currentFileSyncSession.deleteMode = deleteMode;
  currentFileSyncSession.plannedFiles = plannedFiles;
  appLogInfo("handleFileSyncPlanCommand accepted. sessionId=%s targetArea=%s fileCount=%ld deleteMode=%s",
             sessionId.c_str(),
             targetArea.c_str(),
             static_cast<long>(plannedFiles.size()),
             deleteMode.c_str());
  cJSON_Delete(rootObject);
  publishFileSyncStatusNotice(destinationId, sessionId, targetArea, "planning", "OK", "plan accepted", "");
  return true;
}

bool handleFileSyncChunkCommand(const String& payloadText, const String& destinationId) {
  if (!currentFileSyncSession.active) {
    appLogError("handleFileSyncChunkCommand failed. no active session.");
    publishFileSyncStatusNotice(destinationId, "", "", "receiving", "NG", "no active session", "FSYNC_SESSION_MISMATCH");
    return true;
  }
  cJSON* rootObject = cJSON_Parse(payloadText.c_str());
  if (rootObject == nullptr) {
    appLogError("handleFileSyncChunkCommand failed. payload parse error.");
    publishFileSyncStatusNotice(destinationId,
                                currentFileSyncSession.sessionId,
                                currentFileSyncSession.targetArea,
                                "receiving",
                                "NG",
                                "payload parse error",
                                "FSYNC_IO_ERROR");
    return true;
  }
  cJSON* argsObject = cJSON_GetObjectItemCaseSensitive(rootObject, "args");
  if (!cJSON_IsObject(argsObject)) {
    cJSON_Delete(rootObject);
    return true;
  }
  String sessionId = cJSON_IsString(cJSON_GetObjectItemCaseSensitive(argsObject, "sessionId"))
                         ? String(cJSON_GetObjectItemCaseSensitive(argsObject, "sessionId")->valuestring)
                         : String("");
  if (sessionId != currentFileSyncSession.sessionId) {
    appLogError("handleFileSyncChunkCommand failed. session mismatch. expected=%s actual=%s",
                currentFileSyncSession.sessionId.c_str(),
                sessionId.c_str());
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId,
                                sessionId,
                                currentFileSyncSession.targetArea,
                                "receiving",
                                "NG",
                                "session mismatch",
                                "FSYNC_SESSION_MISMATCH");
    return true;
  }
  String rawPath = cJSON_IsString(cJSON_GetObjectItemCaseSensitive(argsObject, "path"))
                       ? String(cJSON_GetObjectItemCaseSensitive(argsObject, "path")->valuestring)
                       : String("");
  String normalizedPath;
  if (!normalizeManagedFilePath(currentFileSyncSession.targetArea, rawPath, &normalizedPath)) {
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId,
                                currentFileSyncSession.sessionId,
                                currentFileSyncSession.targetArea,
                                "receiving",
                                "NG",
                                "invalid path",
                                "FSYNC_SCOPE_VIOLATION");
    return true;
  }
  const fileSyncPlannedFile* plannedFile = findPlannedFile(normalizedPath);
  if (plannedFile == nullptr || !plannedFile->action.equalsIgnoreCase("upsert")) {
    appLogError("handleFileSyncChunkCommand failed. planned file not found or not upsert. path=%s", normalizedPath.c_str());
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId,
                                currentFileSyncSession.sessionId,
                                currentFileSyncSession.targetArea,
                                "receiving",
                                "NG",
                                "planned file not found",
                                "FSYNC_CHUNK_MISSING");
    return true;
  }

  String dataBase64 = cJSON_IsString(cJSON_GetObjectItemCaseSensitive(argsObject, "dataBase64"))
                          ? String(cJSON_GetObjectItemCaseSensitive(argsObject, "dataBase64")->valuestring)
                          : String("");
  int32_t chunkIndex = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(argsObject, "chunkIndex"))
                           ? static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(argsObject, "chunkIndex")->valuedouble)
                           : 0;
  std::vector<uint8_t> chunkBytes;
  if (!decodeBase64Text(dataBase64, &chunkBytes)) {
    appLogError("handleFileSyncChunkCommand failed. decodeBase64Text error. path=%s chunkIndex=%ld",
                normalizedPath.c_str(),
                static_cast<long>(chunkIndex));
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId,
                                currentFileSyncSession.sessionId,
                                currentFileSyncSession.targetArea,
                                "receiving",
                                "NG",
                                "chunk base64 decode failed",
                                "FSYNC_IO_ERROR");
    return true;
  }

  String parentDirectory = normalizedPath.substring(0, normalizedPath.lastIndexOf('/'));
  if (parentDirectory.length() == 0) {
    parentDirectory = "/";
  }
  if (!ensureDirectoryPathExists(parentDirectory)) {
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId,
                                currentFileSyncSession.sessionId,
                                currentFileSyncSession.targetArea,
                                "receiving",
                                "NG",
                                "parent directory create failed",
                                "FSYNC_IO_ERROR");
    return true;
  }
  String tempPath = buildTempFilePath(normalizedPath);
  const char* openMode = (chunkIndex <= 0) ? "w" : "a";
  File tempFile = LittleFS.open(tempPath, openMode);
  if (!tempFile) {
    appLogError("handleFileSyncChunkCommand failed. open temp file failed. path=%s mode=%s", tempPath.c_str(), openMode);
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId,
                                currentFileSyncSession.sessionId,
                                currentFileSyncSession.targetArea,
                                "receiving",
                                "NG",
                                "temp file open failed",
                                "FSYNC_IO_ERROR");
    return true;
  }
  size_t writtenSize = tempFile.write(chunkBytes.data(), chunkBytes.size());
  tempFile.close();
  if (writtenSize != chunkBytes.size()) {
    appLogError("handleFileSyncChunkCommand failed. write size mismatch. path=%s expected=%u actual=%u",
                tempPath.c_str(),
                static_cast<unsigned>(chunkBytes.size()),
                static_cast<unsigned>(writtenSize));
    publishFileSyncStatusNotice(destinationId,
                                currentFileSyncSession.sessionId,
                                currentFileSyncSession.targetArea,
                                "receiving",
                                "NG",
                                "chunk write size mismatch",
                                "FSYNC_IO_ERROR");
    cJSON_Delete(rootObject);
    return true;
  }
  cJSON_Delete(rootObject);
  publishFileSyncStatusNotice(destinationId,
                              currentFileSyncSession.sessionId,
                              currentFileSyncSession.targetArea,
                              "receiving",
                              "OK",
                              "chunk accepted",
                              "");
  return true;
}

bool handleFileSyncCommitCommand(const String& payloadText, const String& destinationId) {
  if (!currentFileSyncSession.active) {
    appLogError("handleFileSyncCommitCommand failed. no active session.");
    publishFileSyncStatusNotice(destinationId, "", "", "verifying", "NG", "no active session", "FSYNC_SESSION_MISMATCH");
    return true;
  }
  cJSON* rootObject = cJSON_Parse(payloadText.c_str());
  if (rootObject == nullptr) {
    return true;
  }
  cJSON* argsObject = cJSON_GetObjectItemCaseSensitive(rootObject, "args");
  String sessionId = (cJSON_IsObject(argsObject) && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(argsObject, "sessionId")))
                         ? String(cJSON_GetObjectItemCaseSensitive(argsObject, "sessionId")->valuestring)
                         : String("");
  if (sessionId != currentFileSyncSession.sessionId) {
    appLogError("handleFileSyncCommitCommand failed. session mismatch. expected=%s actual=%s",
                currentFileSyncSession.sessionId.c_str(),
                sessionId.c_str());
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId, sessionId, currentFileSyncSession.targetArea, "verifying", "NG", "session mismatch", "FSYNC_SESSION_MISMATCH");
    return true;
  }

  String areaRootPath;
  if (!resolveAreaRootPath(currentFileSyncSession.targetArea, &areaRootPath)) {
    cJSON_Delete(rootObject);
    publishFileSyncStatusNotice(destinationId,
                                currentFileSyncSession.sessionId,
                                currentFileSyncSession.targetArea,
                                "verifying",
                                "NG",
                                "invalid target area",
                                "FSYNC_SCOPE_VIOLATION");
    return true;
  }

  publishFileSyncStatusNotice(destinationId,
                              currentFileSyncSession.sessionId,
                              currentFileSyncSession.targetArea,
                              "verifying",
                              "OK",
                              "commit start",
                              "");

  bool commitResult = true;
  String errorCodeText = "";
  bool mqttTlsMetadataSyncRequired = false;
  for (size_t index = 0; index < currentFileSyncSession.plannedFiles.size(); ++index) {
    const fileSyncPlannedFile& plannedFile = currentFileSyncSession.plannedFiles[index];
    if (plannedFile.action.equalsIgnoreCase("upsert")) {
      String tempPath = buildTempFilePath(plannedFile.path);
      String actualSha256;
      if (!computeLittleFsFileSha256(tempPath, &actualSha256)) {
        commitResult = false;
        errorCodeText = "FSYNC_IO_ERROR";
        continue;
      }
      if (plannedFile.expectedSha256.length() > 0 && !actualSha256.equalsIgnoreCase(plannedFile.expectedSha256)) {
        appLogError("handleFileSyncCommitCommand failed. sha256 mismatch. path=%s expected=%s actual=%s",
                    plannedFile.path.c_str(),
                    plannedFile.expectedSha256.c_str(),
                    actualSha256.c_str());
        LittleFS.remove(tempPath);
        commitResult = false;
        errorCodeText = "FSYNC_SHA_MISMATCH";
        continue;
      }
      if (LittleFS.exists(plannedFile.path)) {
        LittleFS.remove(plannedFile.path);
      }
      if (!LittleFS.rename(tempPath, plannedFile.path)) {
        appLogError("handleFileSyncCommitCommand failed. rename temp->active failed. temp=%s path=%s",
                    tempPath.c_str(),
                    plannedFile.path.c_str());
        LittleFS.remove(tempPath);
        commitResult = false;
        errorCodeText = "FSYNC_IO_ERROR";
      } else if (currentFileSyncSession.targetArea.equalsIgnoreCase("certs") &&
                 isMqttTlsManagedCertPath(plannedFile.path)) {
        mqttTlsMetadataSyncRequired = true;
      }
      continue;
    }
    if (plannedFile.action.equalsIgnoreCase("delete")) {
      if (LittleFS.exists(plannedFile.path) && !LittleFS.remove(plannedFile.path)) {
        appLogError("handleFileSyncCommitCommand failed. remove listed path failed. path=%s",
                    plannedFile.path.c_str());
        commitResult = false;
        errorCodeText = "FSYNC_IO_ERROR";
      }
      if (currentFileSyncSession.targetArea.equalsIgnoreCase("certs") &&
          isMqttTlsManagedCertPath(plannedFile.path)) {
        mqttTlsMetadataSyncRequired = true;
      }
    }
  }

  if (currentFileSyncSession.deleteMode.equalsIgnoreCase("all")) {
    File areaRoot = LittleFS.open(areaRootPath, "r");
    if (areaRoot && areaRoot.isDirectory()) {
      File child = areaRoot.openNextFile();
      while (child) {
        String childPath = String(child.path());
        child.close();
        if (!deletePathRecursively(childPath)) {
          appLogError("handleFileSyncCommitCommand failed. deleteMode=all remove failed. path=%s", childPath.c_str());
          commitResult = false;
          errorCodeText = "FSYNC_IO_ERROR";
        }
        child = areaRoot.openNextFile();
      }
    }
    areaRoot.close();
    if (currentFileSyncSession.targetArea.equalsIgnoreCase("certs")) {
      mqttTlsMetadataSyncRequired = true;
    }
  }

  if (commitResult && mqttTlsMetadataSyncRequired) {
    if (!syncMqttTlsCertificateMetadataAfterFileSync(currentFileSyncSession.sessionId)) {
      appLogError("handleFileSyncCommitCommand failed. mqtt tls metadata sync failed. sessionId=%s",
                  currentFileSyncSession.sessionId.c_str());
      commitResult = false;
      errorCodeText = "FSYNC_IO_ERROR";
    }
  }

  appLogInfo("handleFileSyncCommitCommand completed. sessionId=%s result=%s",
             currentFileSyncSession.sessionId.c_str(),
             commitResult ? "OK" : "NG");
  publishFileSyncStatusNotice(destinationId,
                              currentFileSyncSession.sessionId,
                              currentFileSyncSession.targetArea,
                              commitResult ? "completed" : "failed",
                              commitResult ? "OK" : "NG",
                              commitResult ? "commit completed" : "commit failed",
                              errorCodeText.c_str());
  currentFileSyncSession = fileSyncSessionState{};
  cJSON_Delete(rootObject);
  return true;
}

/**
 * @brief set/get系の受信を暫定処理する。
 * @param commandName コマンド名（set/get）。
 * @param normalizedSubName 正規化済みサブコマンド。
 * @param parsedMessage 解析済みメッセージ。
 * @return 処理済みならtrue。
 * @details
 * - [重要] 現時点は受信口を固定し、詳細制御実装は後続タスクへ委譲する。
 */
bool handleSetOrGetSubCommand(const char* commandName,
                              const String& normalizedSubName,
                              const mqtt::mqttIncomingMessage& parsedMessage) {
  if (commandName == nullptr || strlen(commandName) == 0) {
    return false;
  }

  if (normalizedSubName.length() == 0) {
    return false;
  }

  if (strcmp(commandName, "set") == 0 && normalizedSubName.equalsIgnoreCase("keyDeviceSet")) {
    jsonService payloadJsonService;
    String keyDeviceBase64;
    payloadJsonService.getValueByPath(parsedMessage.rawPayload, "args.keyDevice", &keyDeviceBase64);
    if (keyDeviceBase64.length() == 0) {
      appLogError("handleSetOrGetSubCommand failed. keyDeviceSet requires args.keyDevice.");
      return true;
    }
    if (!ensureMqttSensitiveDataReady()) {
      appLogError("handleSetOrGetSubCommand failed. sensitiveDataService is not ready.");
      return true;
    }
    if (!mqttSensitiveDataService.saveKeyDevice(keyDeviceBase64)) {
      appLogError("handleSetOrGetSubCommand failed. saveKeyDevice returned false.");
      return true;
    }
    appLogWarn("handleSetOrGetSubCommand: keyDeviceSet saved successfully. keyLength=%ld", static_cast<long>(keyDeviceBase64.length()));
    return true;
  }

  if (strcmp(commandName, "set") == 0 && normalizedSubName.equalsIgnoreCase("fileLogSet")) {
    cJSON* rootObject = cJSON_Parse(parsedMessage.rawPayload.c_str());
    if (rootObject == nullptr) {
      appLogError("handleSetOrGetSubCommand failed. fileLogSet payload parse failed.");
      return true;
    }
    cJSON* argsObject = cJSON_GetObjectItemCaseSensitive(rootObject, "args");
    cJSON* enabledItem = (argsObject != nullptr) ? cJSON_GetObjectItemCaseSensitive(argsObject, "enabled") : nullptr;
    bool enabledValue = false;
    bool enabledParsed = false;
    if (enabledItem != nullptr) {
      if (cJSON_IsBool(enabledItem)) {
        enabledValue = cJSON_IsTrue(enabledItem);
        enabledParsed = true;
      } else if (cJSON_IsNumber(enabledItem)) {
        enabledValue = (enabledItem->valuedouble != 0.0);
        enabledParsed = true;
      } else if (cJSON_IsString(enabledItem) && enabledItem->valuestring != nullptr) {
        String enabledText = String(enabledItem->valuestring);
        enabledText.trim();
        if (enabledText.equalsIgnoreCase("true") || enabledText == "1") {
          enabledValue = true;
          enabledParsed = true;
        } else if (enabledText.equalsIgnoreCase("false") || enabledText == "0") {
          enabledValue = false;
          enabledParsed = true;
        }
      }
    }
    cJSON_Delete(rootObject);
    if (!enabledParsed) {
      appLogError("handleSetOrGetSubCommand failed. fileLogSet requires args.enabled(bool|number|string).");
      return true;
    }
    setFileLogEnabled(enabledValue);
    appLogWarn("handleSetOrGetSubCommand: fileLogSet applied. enabled=%d srcId=%s dstId=%s",
               static_cast<int>(enabledValue),
               parsedMessage.srcId.c_str(),
               parsedMessage.dstId.c_str());
    return true;
  }

  if (strcmp(commandName, "get") == 0 && normalizedSubName.equalsIgnoreCase("fileLogStatus")) {
    appLogInfo("handleSetOrGetSubCommand: fileLogStatus. enabled=%d srcId=%s dstId=%s",
               static_cast<int>(isFileLogEnabled()),
               parsedMessage.srcId.c_str(),
               parsedMessage.dstId.c_str());
    return true;
  }

  if (strcmp(commandName, "get") == 0 &&
      normalizedSubName.equalsIgnoreCase(iotCommon::mqtt::subCommand::get::kTrh)) {
    i2cService* i2cServiceInstance = getI2cServiceInstance();
    if (i2cServiceInstance == nullptr) {
      appLogError("handleSetOrGetSubCommand failed. get/trh requested but I2C service is not started. srcId=%s dstId=%s",
                  parsedMessage.srcId.c_str(),
                  parsedMessage.dstId.c_str());
      return true;
    }

    jsonService payloadJsonService;
    String requestIdText;
    payloadJsonService.getValueByPath(parsedMessage.rawPayload, "id", &requestIdText);
    i2cEnvironmentSnapshot snapshot{};
    const bool readResult = i2cServiceInstance->requestEnvironmentSnapshot(&snapshot, 2000);
    if (readResult) {
      appLogInfo("handleSetOrGetSubCommand get/trh success. srcId=%s dstId=%s requestId=%s address=0x%02X temperature=%.2f humidity=%.2f pressure=%.2f",
                 parsedMessage.srcId.c_str(),
                 parsedMessage.dstId.c_str(),
                 requestIdText.c_str(),
                 static_cast<unsigned>(snapshot.sensorAddress),
                 static_cast<double>(snapshot.temperatureC),
                 static_cast<double>(snapshot.humidityRh),
                 static_cast<double>(snapshot.pressureHpa));
    } else {
      appLogWarn("handleSetOrGetSubCommand get/trh failed. srcId=%s dstId=%s requestId=%s detected=%d address=0x%02X",
                 parsedMessage.srcId.c_str(),
                 parsedMessage.dstId.c_str(),
                 requestIdText.c_str(),
                 snapshot.isSensorDetected ? 1 : 0,
                 static_cast<unsigned>(snapshot.sensorAddress));
    }

    const char* detailText = readResult ? "BME280 read success" : "BME280 read failed";
    if (!publishTrhNotice(parsedMessage.srcId, requestIdText, snapshot, readResult, detailText)) {
      appLogError("handleSetOrGetSubCommand get/trh failed. publishTrhNotice returned false. srcId=%s dstId=%s requestId=%s",
                  parsedMessage.srcId.c_str(),
                  parsedMessage.dstId.c_str(),
                  requestIdText.c_str());
    }
    return true;
  }

  appLogInfo("handleSetOrGetSubCommand accepted. command=%s sub=%s dstId=%s srcId=%s",
             commandName,
             normalizedSubName.c_str(),
             parsedMessage.dstId.c_str(),
             parsedMessage.srcId.c_str());
  return true;
}

/**
 * @brief call系サブコマンドを処理する。
 * @param normalizedSubName 正規化済みサブコマンド。
 * @param payloadText 受信payload。
 * @return 処理済みならtrue。
 */
bool handleCallSubCommand(const String& normalizedSubName,
                          const String& payloadText,
                          const mqtt::mqttIncomingMessage& parsedMessage) {
  if (!verifyFileSyncCommandSignature(normalizedSubName, payloadText)) {
    if (normalizedSubName.equalsIgnoreCase("imagePackageApply")) {
      String sessionId;
      String destinationDir;
      extractImagePackageContext(payloadText, &sessionId, &destinationDir);
      publishImagePackageStatusNotice(parsedMessage.srcId,
                                      sessionId,
                                      destinationDir,
                                      "verifying",
                                      "NG",
                                      "signature verification failed",
                                      "IPKG_SIG_INVALID");
    } else {
      String sessionId;
      String targetArea;
      extractFileSyncContext(payloadText, &sessionId, &targetArea);
      publishFileSyncStatusNotice(parsedMessage.srcId,
                                  sessionId,
                                  targetArea,
                                  "verifying",
                                  "NG",
                                  "signature verification failed",
                                  "FSYNC_SIG_INVALID");
    }
    appLogError("handleCallSubCommand rejected. signature verification failed. sub=%s srcId=%s dstId=%s",
                normalizedSubName.c_str(),
                parsedMessage.srcId.c_str(),
                parsedMessage.dstId.c_str());
    return true;
  }

  if (normalizedSubName == iotCommon::mqtt::subCommand::call::kRestart) {
    long delayMs = 0;
    jsonService payloadJsonService;
    payloadJsonService.getValueByPath(payloadText, "args.delayMs", &delayMs);
    if (delayMs < 0) {
      delayMs = 0;
    }
    appLogWarn("handleCallSubCommand: restart requested. delayMs=%ld", delayMs);
    if (delayMs > 0) {
      delay(static_cast<uint32_t>(delayMs));
    }
    ESP.restart();
    return true;
  }

  if (normalizedSubName == iotCommon::mqtt::subCommand::call::kMaintenance) {
    const bool saveResult = maintenanceMode::requestMaintenanceModeOnNextBoot();
    if (!saveResult) {
      appLogError("handleCallSubCommand failed. could not persist maintenance request. action=restart_without_flag");
    } else {
      appLogWarn("handleCallSubCommand: maintenance request persisted. reboot to AP maintenance mode.");
    }
    delay(100);
    ESP.restart();
    return true;
  }

  if (normalizedSubName.equalsIgnoreCase("fileSyncPlan")) {
    return handleFileSyncPlanCommand(payloadText, parsedMessage.srcId);
  }

  if (normalizedSubName.equalsIgnoreCase("fileSyncChunk")) {
    return handleFileSyncChunkCommand(payloadText, parsedMessage.srcId);
  }

  if (normalizedSubName.equalsIgnoreCase("fileSyncCommit")) {
    return handleFileSyncCommitCommand(payloadText, parsedMessage.srcId);
  }

  if (normalizedSubName.equalsIgnoreCase("imagePackageApply")) {
    return handleImagePackageApplyCommand(payloadText, parsedMessage.srcId);
  }

  if (normalizedSubName.equalsIgnoreCase("securePing")) {
    jsonService payloadJsonService;
    String requestIdText;
    String ivBase64;
    String cipherBase64;
    String tagBase64;
    payloadJsonService.getValueByPath(payloadText, "args.requestId", &requestIdText);
    payloadJsonService.getValueByPath(payloadText, "args.enc.iv", &ivBase64);
    payloadJsonService.getValueByPath(payloadText, "args.enc.ct", &cipherBase64);
    payloadJsonService.getValueByPath(payloadText, "args.enc.tag", &tagBase64);
    if (requestIdText.length() == 0 || ivBase64.length() == 0 || cipherBase64.length() == 0 || tagBase64.length() == 0) {
      appLogError("handleCallSubCommand securePing failed. required args are missing. requestIdLength=%ld ivLength=%ld ctLength=%ld tagLength=%ld",
                  static_cast<long>(requestIdText.length()),
                  static_cast<long>(ivBase64.length()),
                  static_cast<long>(cipherBase64.length()),
                  static_cast<long>(tagBase64.length()));
      return true;
    }

    std::vector<uint8_t> keyBytes;
    if (!loadKDeviceBytes(&keyBytes)) {
      appLogError("handleCallSubCommand securePing failed. no valid keyDevice found.");
      return true;
    }
    std::vector<uint8_t> ivBytes;
    std::vector<uint8_t> cipherBytes;
    std::vector<uint8_t> tagBytes;
    if (!decodeBase64Text(ivBase64, &ivBytes) || !decodeBase64Text(cipherBase64, &cipherBytes) || !decodeBase64Text(tagBase64, &tagBytes)) {
      appLogError("handleCallSubCommand securePing failed. base64 decode failed.");
      return true;
    }
    String plainText;
    if (!decryptAesGcm(keyBytes, ivBytes, cipherBytes, tagBytes, &plainText)) {
      appLogError("handleCallSubCommand securePing failed. decrypt failed.");
      return true;
    }

    String responsePlainText;
    // [重要][再発防止] 応答平文に元payload全体を含めると暗号後サイズが大きくなり、
    // PubSubClient の publish が失敗し secureEcho timeout につながるため、最小情報のみに制限する。
    // [厳守] decrypted は JSON文字列ではなく JSON値として返す（LocalServer側で JSON.parse するため）。
    responsePlainText = "{\"result\":\"OK\",\"requestId\":\"" + requestIdText + "\",\"decrypted\":" + plainText + "}";
    std::vector<uint8_t> responseIvBytes;
    std::vector<uint8_t> responseCipherBytes;
    std::vector<uint8_t> responseTagBytes;
    if (!encryptAesGcm(keyBytes, responsePlainText, &responseIvBytes, &responseCipherBytes, &responseTagBytes)) {
      appLogError("handleCallSubCommand securePing failed. encrypt failed.");
      return true;
    }
    String responseIvBase64;
    String responseCipherBase64;
    String responseTagBase64;
    if (!encodeBase64Text(responseIvBytes, &responseIvBase64) ||
        !encodeBase64Text(responseCipherBytes, &responseCipherBase64) ||
        !encodeBase64Text(responseTagBytes, &responseTagBase64)) {
      appLogError("handleCallSubCommand securePing failed. base64 encode failed.");
      return true;
    }
    if (deviceNodeName.length() <= 0 && !resolveDeviceNodeName(&deviceNodeName)) {
      appLogError("handleCallSubCommand securePing failed. resolveDeviceNodeName failed.");
      return true;
    }
    String topicText;
    if (!createTopicText("notice", "secureEcho", deviceNodeName.c_str(), &topicText)) {
      appLogError("handleCallSubCommand securePing failed. createTopicText failed.");
      return true;
    }
    String payloadOut = "{}";
    jsonKeyValueItem responseItems[] = {
        {"v", jsonValueType::kString, iotCommon::kProtocolVersion, 0, 0, false},
        {"DstID", jsonValueType::kString, parsedMessage.srcId.c_str(), 0, 0, false},
        {"SrcID", jsonValueType::kString, deviceNodeName.c_str(), 0, 0, false},
        {"Request", jsonValueType::kString, "Notice", 0, 0, false},
        {"id", jsonValueType::kString, requestIdText.c_str(), 0, 0, false},
        {"sub", jsonValueType::kString, "secureEcho", 0, 0, false},
        {"requestId", jsonValueType::kString, requestIdText.c_str(), 0, 0, false},
        {"enc.alg", jsonValueType::kString, "A256GCM", 0, 0, false},
        {"enc.iv", jsonValueType::kString, responseIvBase64.c_str(), 0, 0, false},
        {"enc.ct", jsonValueType::kString, responseCipherBase64.c_str(), 0, 0, false},
        {"enc.tag", jsonValueType::kString, responseTagBase64.c_str(), 0, 0, false},
    };
    if (!payloadJsonService.setValuesByPath(&payloadOut, responseItems, sizeof(responseItems) / sizeof(responseItems[0]))) {
      appLogError("handleCallSubCommand securePing failed. payloadJsonService.setValuesByPath returned false.");
      return true;
    }
    String outgoingPayloadText;
    if (!resolveOutgoingPayloadText(topicText, payloadOut, &outgoingPayloadText)) {
      appLogError("handleCallSubCommand securePing failed. resolveOutgoingPayloadText failed. topic=%s", topicText.c_str());
      return true;
    }
    bool publishResult = mqttClient.publish(topicText.c_str(), outgoingPayloadText.c_str(), false);
    if (!publishResult) {
      appLogError("handleCallSubCommand securePing failed. publish failed. topic=%s", topicText.c_str());
      return true;
    }
    appLogInfo("handleCallSubCommand securePing success. requestId=%s", requestIdText.c_str());
    return true;
  }

  return false;
}

/**
 * @brief サーバーから受信したMQTTペイロードを解析してログ出力する。
 * @param topicName 受信トピック。
 * @param payloadBuffer 受信ペイロード。
 * @param payloadLength ペイロード長。
 */
void onMqttMessageReceived(char* topicName, byte* payloadBuffer, unsigned int payloadLength) {
  if (payloadBuffer == nullptr) {
    appLogError("onMqttMessageReceived failed. payloadBuffer is null.");
    return;
  }
  String payloadText;
  payloadText.reserve(payloadLength + 1);
  for (unsigned int index = 0; index < payloadLength; ++index) {
    payloadText += static_cast<char>(payloadBuffer[index]);
  }
  String effectivePayloadText;
  bool wasEncrypted = false;
  if (!resolveIncomingPayloadText(topicName, payloadText, &effectivePayloadText, &wasEncrypted)) {
    appLogError("onMqttMessageReceived failed. payload security validation failed. topic=%s",
                (topicName == nullptr ? "(null)" : topicName));
    return;
  }

  mqtt::mqttIncomingMessage parsedMessage{};
  bool parseResult = mqtt::parseMqttIncomingMessage(topicName, effectivePayloadText.c_str(), &parsedMessage);
  if (!parseResult) {
    appLogWarn("onMqttMessageReceived: parse failed. topic=%s payload=%s",
               (topicName == nullptr ? "(null)" : topicName),
               effectivePayloadText.c_str());
    return;
  }
  appLogInfo("onMqttMessageReceived: parsed. encrypted=%d type=%d command=%s sub=%s dstId=%s srcId=%s",
             wasEncrypted ? 1 : 0,
             static_cast<int>(parsedMessage.messageType),
             parsedMessage.commandName.c_str(),
             parsedMessage.subName.c_str(),
             parsedMessage.dstId.c_str(),
             parsedMessage.srcId.c_str());
  const String normalizedSubName = normalizeSubCommand(parsedMessage.subName);

  bool isStatusCallTopic = (topicName != nullptr && strstr(topicName, "esp32lab/call/status/") != nullptr);
  if (isStatusCallTopic &&
      normalizedSubName == iotCommon::mqtt::jsonKey::status::kCommand) {
    appUtil::appTaskMessageDetail statusReplyDetail = appUtil::createEmptyMessageDetail();
    statusReplyDetail.hasIntValue = true;
    statusReplyDetail.intValue = static_cast<int32_t>(mainTaskStartupCpuMillis);
    statusReplyDetail.text = "status reply requested by mqtt callback";
    statusReplyDetail.text2 = iotCommon::mqtt::subCommand::status::kReply;
    statusReplyDetail.text3 = statusValueOnline;
    bool sendResult = appUtil::sendMessage(appTaskId::kMqtt,
                                           appTaskId::kMqtt,
                                           appMessageType::kMqttPublishOnlineRequest,
                                           &statusReplyDetail,
                                           100);
    if (!sendResult) {
      appLogError("onMqttMessageReceived failed. could not queue status reply. topic=%s srcId=%s dstId=%s",
                  (topicName == nullptr ? "(null)" : topicName),
                  parsedMessage.srcId.c_str(),
                  parsedMessage.dstId.c_str());
    } else {
      appLogInfo("onMqttMessageReceived: status reply queued. topic=%s srcId=%s dstId=%s",
                 (topicName == nullptr ? "(null)" : topicName),
                 parsedMessage.srcId.c_str(),
                 parsedMessage.dstId.c_str());
    }
  }

  bool isOtaStartCallTopic = (topicName != nullptr && strstr(topicName, "esp32lab/call/otaStart/") != nullptr);
  if (isOtaStartCallTopic &&
      normalizedSubName == iotCommon::mqtt::subCommand::call::kOtaStart) {
    jsonService payloadJsonService;
    otaStartRequestContext otaRequestContext;
    payloadJsonService.getValueByPath(parsedMessage.rawPayload, "id", &otaRequestContext.transactionId);
    payloadJsonService.getValueByPath(parsedMessage.rawPayload, "args.firmwareVersion", &otaRequestContext.firmwareVersion);
    payloadJsonService.getValueByPath(parsedMessage.rawPayload, "args.firmwareUrl", &otaRequestContext.firmwareUrl);
    payloadJsonService.getValueByPath(parsedMessage.rawPayload, "args.sha256", &otaRequestContext.firmwareSha256);
    if (otaRequestContext.firmwareSha256.length() == 0) {
      payloadJsonService.getValueByPath(parsedMessage.rawPayload, "args.firmwareSha256", &otaRequestContext.firmwareSha256);
    }

    if (otaRequestContext.firmwareUrl.length() == 0) {
      appLogError("onMqttMessageReceived failed. otaStart firmwareUrl is empty. topic=%s payload=%s",
                  (topicName == nullptr ? "(null)" : topicName),
                  payloadText.c_str());
      return;
    }

    storePendingOtaStartRequest(otaRequestContext);
    bool sendResult = appUtil::sendMessage(appTaskId::kOta,
                                           appTaskId::kMqtt,
                                           appMessageType::kOtaStartRequest,
                                           nullptr,
                                           100);
    if (!sendResult) {
      appLogError("onMqttMessageReceived failed. could not queue ota start. topic=%s version=%s url=%s",
                  (topicName == nullptr ? "(null)" : topicName),
                  otaRequestContext.firmwareVersion.c_str(),
                  otaRequestContext.firmwareUrl.c_str());
    } else {
      appLogInfo("onMqttMessageReceived: ota start queued. topic=%s version=%s url=%s",
                 (topicName == nullptr ? "(null)" : topicName),
                 otaRequestContext.firmwareVersion.c_str(),
                 otaRequestContext.firmwareUrl.c_str());
    }
  }

  const bool isRollbackTestCallTopic = (topicName != nullptr && strstr(topicName, "esp32lab/call/") != nullptr);
  if (isRollbackTestCallTopic &&
      normalizedSubName == iotCommon::mqtt::subCommand::call::kRollbackTestEnable) {
    const bool enableResult = otaRollback::enableRollbackFailureTestMode();
    appLogWarn("onMqttMessageReceived: rollback test mode enable requested. result=%d", enableResult ? 1 : 0);
    return;
  }
  if (isRollbackTestCallTopic &&
      normalizedSubName == iotCommon::mqtt::subCommand::call::kRollbackTestDisable) {
    const bool disableResult = otaRollback::disableRollbackFailureTestMode();
    appLogInfo("onMqttMessageReceived: rollback test mode disable requested. result=%d", disableResult ? 1 : 0);
    return;
  }

  const bool isCallTopic = (topicName != nullptr && strstr(topicName, "esp32lab/call/") != nullptr);
  if (isCallTopic && handleCallSubCommand(normalizedSubName, effectivePayloadText, parsedMessage)) {
    return;
  }

  const bool isSetTopic = (topicName != nullptr && strstr(topicName, "esp32lab/set/") != nullptr);
  if (isSetTopic && handleSetOrGetSubCommand("set", normalizedSubName, parsedMessage)) {
    return;
  }

  const bool isGetTopic = (topicName != nullptr && strstr(topicName, "esp32lab/get/") != nullptr);
  if (isGetTopic && handleSetOrGetSubCommand("get", normalizedSubName, parsedMessage)) {
    return;
  }
}

/**
 * @brief MQTTブローカーへの到達確認を実施する。
 * @param brokerHost ブローカーのホスト名またはIP。
 * @return 到達確認成功でtrue、失敗時false。
 */
bool pingBrokerHost(const char* brokerHost) {
  if (brokerHost == nullptr || strlen(brokerHost) == 0) {
    appLogError("pingBrokerHost failed. brokerHost is null or empty.");
    return false;
  }

  mqttResolvedBrokerIpValid = false;
  IPAddress brokerIpAddress;
  if (brokerIpAddress.fromString(brokerHost)) {
    appLogInfo("pingBrokerHost: direct IP host will be used. brokerHost=%s", brokerHost);
  } else {
    IPAddress configuredIpAddress;
    const bool configuredIpAvailable = strlen(SENSITIVE_MQTT_FALLBACK_IP) > 0;
    const bool configuredIpParseResult = configuredIpAvailable && configuredIpAddress.fromString(SENSITIVE_MQTT_FALLBACK_IP);
    if (configuredIpParseResult) {
      // [重要] 現在仕様: IPが設定されている場合は、DNS試行より先にIP直指定で疎通を確認する。
      brokerIpAddress = configuredIpAddress;
      appLogWarn("pingBrokerHost: configured IP will be used before DNS. brokerHost=%s configuredIp=%s",
                 brokerHost,
                 brokerIpAddress.toString().c_str());
    } else {
      const bool resolveResult = WiFi.hostByName(brokerHost, brokerIpAddress);
      if (!resolveResult) {
        appLogError("pingBrokerHost failed. hostByName failed and configured IP is unavailable. brokerHost=%s configuredIp=%s",
                    brokerHost,
                    SENSITIVE_MQTT_FALLBACK_IP);
        return false;
      }
      appLogInfo("pingBrokerHost: DNS resolved host. brokerHost=%s resolvedIp=%s",
                 brokerHost,
                 brokerIpAddress.toString().c_str());
    }
  }
  mqttResolvedBrokerIpAddress = brokerIpAddress;
  mqttResolvedBrokerIpValid = true;
  appLogInfo("pingBrokerHost start. brokerHost=%s resolvedIp=%s",
             brokerHost,
             brokerIpAddress.toString().c_str());

  // [重要] Arduino-ESP32 2.0.17ではICMP APIの利用が難しいため、TCP到達確認をping代替とする。
  WiFiClient probeClient;
  bool probeResult = probeClient.connect(brokerIpAddress, static_cast<uint16_t>(mqttPort));
  if (!probeResult) {
    appLogError("pingBrokerHost failed (tcp-probe). brokerHost=%s resolvedIp=%s brokerPort=%ld",
                brokerHost,
                brokerIpAddress.toString().c_str(),
                static_cast<long>(mqttPort));
    appLogError("pingBrokerHost network snapshot. wifiStatus=%d ssid=%s localIp=%s gateway=%s subnet=%s",
                static_cast<int>(WiFi.status()),
                WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str());
    return false;
  }

  appLogInfo("pingBrokerHost success (tcp-probe). brokerHost=%s resolvedIp=%s brokerPort=%ld",
             brokerHost,
             brokerIpAddress.toString().c_str(),
             static_cast<long>(mqttPort));
  probeClient.stop();
  return true;
}

/**
 * @brief MQTT接続情報を内部バッファへ保存する。
 * @param receivedMessage mainTaskから受信したMQTT初期化要求メッセージ。
 */
void storeMqttConfig(const appTaskMessage& receivedMessage) {
  strncpy(mqttHost, receivedMessage.text, sizeof(mqttHost) - 1);
  mqttHost[sizeof(mqttHost) - 1] = '\0';
  strncpy(mqttUser, receivedMessage.text2, sizeof(mqttUser) - 1);
  mqttUser[sizeof(mqttUser) - 1] = '\0';
  strncpy(mqttPass, receivedMessage.text3, sizeof(mqttPass) - 1);
  mqttPass[sizeof(mqttPass) - 1] = '\0';
  mqttPort = receivedMessage.intValue;
  mqttTls = receivedMessage.boolValue;
}

/**
 * @brief MQTTブローカーへ接続する。
 * @return 接続成功時true、失敗時false。
 */
bool connectToMqttBroker() {
  constexpr int32_t maxRetryCount = 10;
  constexpr int32_t retryDelayMs = 200;

  if (strlen(mqttHost) == 0) {
    appLogError("connectToMqttBroker failed. host is empty.");
    return false;
  }
  if (mqttPort <= 0 || mqttPort > 65535) {
    appLogError("connectToMqttBroker failed. invalid port=%ld", static_cast<long>(mqttPort));
    return false;
  }
  if (strlen(mqttUser) == 0 || strlen(mqttPass) == 0) {
    appLogError("connectToMqttBroker failed. mqtt user/password is required. userLength=%ld passLength=%ld",
                static_cast<long>(strlen(mqttUser)),
                static_cast<long>(strlen(mqttPass)));
    return false;
  }
  if (!configureMqttTransportClient(mqttTls)) {
    appLogError("connectToMqttBroker failed. configureMqttTransportClient failed. mqttTls=%d",
                static_cast<int>(mqttTls));
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    appLogError("connectToMqttBroker failed. wifi is not connected. wifiStatus=%d", static_cast<int>(WiFi.status()));
    ledController::indicateErrorPattern();
    return false;
  }

  ledController::indicateMqttConnecting();
  ledController::indicateCommunicationActivity();
  bool pingResult = pingBrokerHost(mqttHost);
  if (!pingResult) {
    appLogError("connectToMqttBroker failed. ping to brokerHost=%s did not succeed.", mqttHost);
    appLogError("connectToMqttBroker network snapshot. wifiStatus=%d ssid=%s localIp=%s gateway=%s subnet=%s",
                static_cast<int>(WiFi.status()),
                WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str());
    ledController::indicateErrorPattern();
    return false;
  }

  if (mqttResolvedBrokerIpValid) {
    mqttClient.setServer(mqttResolvedBrokerIpAddress, static_cast<uint16_t>(mqttPort));
    appLogWarn("connectToMqttBroker: mqttClient.setServer uses resolved IP. host=%s ip=%s port=%ld",
               mqttHost,
               mqttResolvedBrokerIpAddress.toString().c_str(),
               static_cast<long>(mqttPort));
  } else {
    mqttClient.setServer(mqttHost, static_cast<uint16_t>(mqttPort));
    appLogInfo("connectToMqttBroker: mqttClient.setServer uses host. host=%s port=%ld",
               mqttHost,
               static_cast<long>(mqttPort));
  }
  mqttClient.setCallback(onMqttMessageReceived);
  // [重要] 4096byteバッファは暗号化payloadの肥大化対策として優先的にPSRAM利用を狙う。
  // [厳守] 実割当先はライブラリ内部malloc依存のため、ヒープ差分をログで常時監視する。
  const size_t spiramFreeBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t internalFreeBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  bool bufferSizeResult = mqttClient.setBufferSize(mqttPacketBufferSizeBytes);
  const size_t spiramFreeAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t internalFreeAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (!bufferSizeResult) {
    appLogError("connectToMqttBroker failed. mqttClient.setBufferSize(%u) failed.",
                static_cast<unsigned>(mqttPacketBufferSizeBytes));
    return false;
  }
  const long spiramDelta = static_cast<long>(spiramFreeBefore) - static_cast<long>(spiramFreeAfter);
  const long internalDelta = static_cast<long>(internalFreeBefore) - static_cast<long>(internalFreeAfter);
  appLogInfo("connectToMqttBroker: mqtt buffer allocated. size=%u spiramDelta=%ld internalDelta=%ld",
             static_cast<unsigned>(mqttPacketBufferSizeBytes),
             spiramDelta,
             internalDelta);
  if (spiramDelta <= 0) {
    appLogWarn("connectToMqttBroker: mqtt buffer may not be on PSRAM. size=%u internalDelta=%ld",
               static_cast<unsigned>(mqttPacketBufferSizeBytes),
               internalDelta);
  }
  String clientId = "esp32lab-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  if (deviceNodeName.length() <= 0) {
    bool resolveNameResult = resolveDeviceNodeName(&deviceNodeName);
    if (!resolveNameResult) {
      appLogError("connectToMqttBroker failed. resolveDeviceNodeName failed.");
      return false;
    }
  }
  String willTopicText;
  bool willTopicResult = createTopicText("notice",
                                         iotCommon::mqtt::jsonKey::status::kCommand,
                                         deviceNodeName.c_str(),
                                         &willTopicText);
  if (!willTopicResult) {
    appLogError("connectToMqttBroker failed. createTopicText for will failed.");
    return false;
  }
  String willPayloadText;
  bool willPayloadResult = mqtt::buildMqttStatusPayload(iotCommon::mqtt::subCommand::status::kWill,
                                                        "Offline",
                                                        mainTaskStartupCpuMillis,
                                                        &willPayloadText);
  if (!willPayloadResult) {
    // [重要] NTP未同期の起動直後でもMQTT接続自体は成立させるため、同一キー構造のwill payloadへフォールバックする。
    String efuseMacText = "00:00:00:00:00:00";
    createEfuseMacAddressText(&efuseMacText);
    String networkMacText = WiFi.macAddress();
    if (networkMacText.length() <= 0) {
      networkMacText = efuseMacText;
    }
    String fallbackIdText = String("will-") + String(static_cast<unsigned long>(millis()));
    String fallbackTsText = "";
    String fallbackStartUpTimeText = "";
    buildWillFallbackTimeText(mainTaskStartupCpuMillis, &fallbackTsText, &fallbackStartUpTimeText);
    String fallbackWifiSsid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("(dummy-ssid)");
    String fallbackIpAddress = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("0.0.0.0");
    long fallbackRssi = static_cast<long>(WiFi.RSSI());
    const String resolvedFirmwareWrittenAt =
        firmwareInfo::resolveFirmwareWrittenAtForStatus(appVersion::kFirmwareVersion, appVersion::kFirmwareWrittenAt);
    char willFallbackBuffer[700] = {};
    int printLength = snprintf(
        willFallbackBuffer,
        sizeof(willFallbackBuffer),
        "{\"v\":\"1\",\"DstID\":\"all\",\"SrcID\":\"%s\",\"Request\":\"Notice\",\"macAddr\":\"%s\",\"macAddrNetwork\":\"%s\","
        "\"id\":\"%s\",\"ts\":\"%s\",\"status\":\"status\",\"sub\":\"Will\",\"onlineState\":\"Offline\","
        "\"startUpTime\":\"%s\",\"fwVersion\":\"%s\",\"fwWrittenAt\":\"%s\",\"firmwareVersion\":\"%s\",\"firmwareWrittenAt\":\"%s\",\"wifiSignalLevel\":%ld,"
        "\"ipAddress\":\"%s\",\"wifiSsid\":\"%s\",\"detail\":\"Disconnect\"}",
        deviceNodeName.c_str(),
        efuseMacText.c_str(),
        networkMacText.c_str(),
        fallbackIdText.c_str(),
        fallbackTsText.c_str(),
        fallbackStartUpTimeText.c_str(),
        appVersion::kFirmwareVersion,
        resolvedFirmwareWrittenAt.c_str(),
        appVersion::kFirmwareVersion,
        resolvedFirmwareWrittenAt.c_str(),
        fallbackRssi,
        fallbackIpAddress.c_str(),
        fallbackWifiSsid.c_str());
    if (printLength > 0 && printLength < static_cast<int>(sizeof(willFallbackBuffer))) {
      willPayloadText = String(willFallbackBuffer);
    } else {
      willPayloadText = "{\"v\":\"1\",\"status\":\"status\",\"sub\":\"Will\",\"onlineState\":\"Offline\",\"detail\":\"Disconnect\"}";
    }
    appLogWarn("connectToMqttBroker: will payload fallback applied. reason=utc unsynchronized or payload build failure");
  }
  String willOutgoingPayloadText;
  if (!resolveOutgoingPayloadText(willTopicText, willPayloadText, &willOutgoingPayloadText)) {
    appLogError("connectToMqttBroker failed. resolveOutgoingPayloadText for will failed.");
    return false;
  }
  appLogInfo("connectToMqttBroker: will payload prepared. topicLength=%ld payloadLength=%ld",
             static_cast<long>(willTopicText.length()),
             static_cast<long>(willOutgoingPayloadText.length()));
  appLogInfo("connectToMqttBroker start. host=%s port=%ld user=%s pass=%s clientId=%s",
             mqttHost,
             static_cast<long>(mqttPort),
             (strlen(mqttUser) > 0 ? mqttUser : "(empty)"),
             (strlen(mqttPass) > 0 ? "******" : "(empty)"),
             clientId.c_str());

  for (int32_t retryIndex = 0; retryIndex < maxRetryCount; ++retryIndex) {
    ledController::indicateMqttConnecting();
    ledController::indicateCommunicationActivity();
    bool connectResult = mqttClient.connect(clientId.c_str(),
                                            mqttUser,
                                            mqttPass,
                                            willTopicText.c_str(),
                                            1,
                                            true,
                                            willOutgoingPayloadText.c_str());

    if (connectResult) {
      String subscribeTopicSet = String("esp32lab/set/+/") + deviceNodeName;
      String subscribeTopicGet = String("esp32lab/get/+/") + deviceNodeName;
      String subscribeTopicCall = String("esp32lab/call/+/") + deviceNodeName;
      String subscribeTopicNetwork = String("esp32lab/network/+/") + deviceNodeName;
      String subscribeTopicSetAll = "esp32lab/set/+/all";
      String subscribeTopicGetAll = "esp32lab/get/+/all";
      String subscribeTopicCallAll = "esp32lab/call/+/all";
      String subscribeTopicNetworkAll = "esp32lab/network/+/all";
      bool subscribeResult =
          mqttClient.subscribe(subscribeTopicSet.c_str(), 1) &&
          mqttClient.subscribe(subscribeTopicGet.c_str(), 1) &&
          mqttClient.subscribe(subscribeTopicCall.c_str(), 1) &&
          mqttClient.subscribe(subscribeTopicNetwork.c_str(), 1) &&
          mqttClient.subscribe(subscribeTopicSetAll.c_str(), 1) &&
          mqttClient.subscribe(subscribeTopicGetAll.c_str(), 1) &&
          mqttClient.subscribe(subscribeTopicCallAll.c_str(), 1) &&
          mqttClient.subscribe(subscribeTopicNetworkAll.c_str(), 1);
      if (!subscribeResult) {
        appLogWarn("connectToMqttBroker: subscribe failed. receiver=%s", deviceNodeName.c_str());
      }
      ledController::indicateMqttConnected();
      appLogInfo("connectToMqttBroker success. state=%d", mqttClient.state());
      return true;
    }

    appLogWarn("connectToMqttBroker retry. retry=%ld state=%d", static_cast<long>(retryIndex + 1), mqttClient.state());
    vTaskDelay(pdMS_TO_TICKS(retryDelayMs));
  }

  appLogError("connectToMqttBroker failed. state=%d", mqttClient.state());
  ledController::indicateErrorPattern();
  return false;
}

/**
 * @brief MQTTへonlineステータスを初回publishする。
 * @return publish成功時true、失敗時false。
 */
bool publishStatusNotice(const char* subName, const char* onlineStateText, uint32_t startupCpuMillis) {
  if (!mqttClient.connected()) {
    appLogError("publishStatusNotice failed. mqtt is not connected.");
    return false;
  }

  String topicText;
  if (deviceNodeName.length() <= 0) {
    bool resolveNameResult = resolveDeviceNodeName(&deviceNodeName);
    if (!resolveNameResult) {
      appLogError("publishStatusNotice failed. resolveDeviceNodeName failed.");
      return false;
    }
  }
  bool topicResult = createTopicText("notice",
                                     iotCommon::mqtt::jsonKey::status::kCommand,
                                     deviceNodeName.c_str(),
                                     &topicText);
  if (!topicResult) {
    appLogError("publishStatusNotice failed. createTopicText failed.");
    return false;
  }

  const char* safeSubName = (subName == nullptr || strlen(subName) == 0) ? iotCommon::mqtt::subCommand::status::kStartUp : subName;
  const char* safeOnlineStateText = (onlineStateText == nullptr || strlen(onlineStateText) == 0) ? statusValueOnline : onlineStateText;
  String plainPayloadText;
  bool buildPayloadResult = mqtt::buildMqttStatusPayload(safeSubName, safeOnlineStateText, startupCpuMillis, &plainPayloadText);
  if (!buildPayloadResult) {
    appLogError("publishStatusNotice failed. buildMqttStatusPayload failed. topic=%s", topicText.c_str());
    return false;
  }
  String outgoingPayloadText;
  if (!resolveOutgoingPayloadText(topicText, plainPayloadText, &outgoingPayloadText)) {
    appLogError("publishStatusNotice failed. resolveOutgoingPayloadText failed. topic=%s", topicText.c_str());
    return false;
  }
  bool publishResult = mqttClient.publish(topicText.c_str(), outgoingPayloadText.c_str(), true);
  ledController::indicateCommunicationActivity();
  if (!publishResult) {
    appLogError("publishStatusNotice failed. topic=%s sub=%s onlineState=%s",
                topicText.c_str(),
                safeSubName,
                safeOnlineStateText);
    return false;
  }

  mqttClient.loop();
  appLogInfo("publishStatusNotice success. topic=%s sub=%s onlineState=%s",
             topicText.c_str(),
             safeSubName,
             safeOnlineStateText);
  return true;
}

/**
 * @brief 現在UTCをISO8601文字列へ変換する。
 * @param utcIso8601Out 出力先。
 * @return 成功時true、失敗時false。
 */
bool createCurrentUtcIso8601Text(String* utcIso8601Out) {
  if (utcIso8601Out == nullptr) {
    appLogError("createCurrentUtcIso8601Text failed. utcIso8601Out is null.");
    return false;
  }

  struct timeval currentTimeValue {};
  if (gettimeofday(&currentTimeValue, nullptr) != 0) {
    appLogError("createCurrentUtcIso8601Text failed. gettimeofday returned non-zero.");
    return false;
  }

  struct tm utcBrokenTime {};
  if (gmtime_r(&currentTimeValue.tv_sec, &utcBrokenTime) == nullptr) {
    appLogError("createCurrentUtcIso8601Text failed. gmtime_r returned null.");
    return false;
  }

  char utcBuffer[40] = {};
  const int writtenLength = snprintf(utcBuffer,
                                     sizeof(utcBuffer),
                                     "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
                                     utcBrokenTime.tm_year + 1900,
                                     utcBrokenTime.tm_mon + 1,
                                     utcBrokenTime.tm_mday,
                                     utcBrokenTime.tm_hour,
                                     utcBrokenTime.tm_min,
                                     utcBrokenTime.tm_sec,
                                     static_cast<long>(currentTimeValue.tv_usec / 1000));
  if (writtenLength <= 0 || writtenLength >= static_cast<int>(sizeof(utcBuffer))) {
    appLogError("createCurrentUtcIso8601Text failed. snprintf overflow. writtenLength=%ld",
                static_cast<long>(writtenLength));
    return false;
  }

  *utcIso8601Out = String(utcBuffer);
  return true;
}

/**
 * @brief 温湿度・気圧通知をpublishする。
 * @param destinationId 返信先ID。
 * @param requestId 応答へ引き継ぐ要求ID。
 * @param snapshot BME280読取結果。
 * @param isSuccess 読取成功フラグ。
 * @param detailText 補足メッセージ。
 * @return publish成功時true、失敗時false。
 */
bool publishTrhNotice(const String& destinationId,
                      const String& requestId,
                      const i2cEnvironmentSnapshot& snapshot,
                      bool isSuccess,
                      const char* detailText) {
  if (!mqttClient.connected()) {
    appLogError("publishTrhNotice failed. mqtt is not connected.");
    return false;
  }

  if (deviceNodeName.length() <= 0) {
    const bool resolveNameResult = resolveDeviceNodeName(&deviceNodeName);
    if (!resolveNameResult) {
      appLogError("publishTrhNotice failed. resolveDeviceNodeName failed.");
      return false;
    }
  }

  String topicText;
  if (!createTopicText("notice", iotCommon::mqtt::subCommand::notice::kTrh, deviceNodeName.c_str(), &topicText)) {
    appLogError("publishTrhNotice failed. createTopicText failed.");
    return false;
  }

  const String messageId = requestId.length() > 0 ? requestId : (String(deviceNodeName) + "-" + millis());
  String timestampText;
  if (!createCurrentUtcIso8601Text(&timestampText)) {
    timestampText = "";
  }

  cJSON* rootObject = cJSON_CreateObject();
  if (rootObject == nullptr) {
    appLogError("publishTrhNotice failed. cJSON_CreateObject returned null.");
    return false;
  }

  cJSON_AddStringToObject(rootObject, "v", "1");
  cJSON_AddStringToObject(rootObject, "DstID", destinationId.length() > 0 ? destinationId.c_str() : "all");
  cJSON_AddStringToObject(rootObject, "SrcID", deviceNodeName.c_str());
  cJSON_AddStringToObject(rootObject, "Request", "Notice");
  cJSON_AddStringToObject(rootObject, "id", messageId.c_str());
  cJSON_AddStringToObject(rootObject, "ts", timestampText.c_str());
  cJSON_AddStringToObject(rootObject, "op", "notice");
  cJSON_AddStringToObject(rootObject, "sub", iotCommon::mqtt::subCommand::notice::kTrh);
  cJSON_AddStringToObject(rootObject, "Res", isSuccess ? iotCommon::mqtt::responseResult::kOk
                                                       : iotCommon::mqtt::responseResult::kNg);
  cJSON_AddStringToObject(rootObject, "detail", detailText == nullptr ? "" : detailText);

  cJSON* argsObject = cJSON_AddObjectToObject(rootObject, "args");
  if (argsObject == nullptr) {
    cJSON_Delete(rootObject);
    appLogError("publishTrhNotice failed. cJSON_AddObjectToObject(args) returned null.");
    return false;
  }
  cJSON_AddStringToObject(argsObject, "sensorId", "bme280-1");
  char sensorAddressText[8] = {};
  snprintf(sensorAddressText, sizeof(sensorAddressText), "0x%02X", static_cast<unsigned>(snapshot.sensorAddress));
  cJSON_AddStringToObject(argsObject, "sensorAddress", sensorAddressText);
  if (isSuccess) {
    cJSON_AddNumberToObject(argsObject, "temperatureC", static_cast<double>(snapshot.temperatureC));
    cJSON_AddNumberToObject(argsObject, "humidityRh", static_cast<double>(snapshot.humidityRh));
    cJSON_AddNumberToObject(argsObject, "pressureHpa", static_cast<double>(snapshot.pressureHpa));
  }

  char* serializedPayload = cJSON_PrintUnformatted(rootObject);
  cJSON_Delete(rootObject);
  if (serializedPayload == nullptr) {
    appLogError("publishTrhNotice failed. cJSON_PrintUnformatted returned null.");
    return false;
  }
  const String plainPayloadText = String(serializedPayload);
  cJSON_free(serializedPayload);

  String outgoingPayloadText;
  if (!resolveOutgoingPayloadText(topicText, plainPayloadText, &outgoingPayloadText)) {
    appLogError("publishTrhNotice failed. resolveOutgoingPayloadText failed. topic=%s", topicText.c_str());
    return false;
  }

  const bool publishResult = mqttClient.publish(topicText.c_str(), outgoingPayloadText.c_str(), false);
  if (!publishResult) {
    appLogError("publishTrhNotice failed. topic=%s requestId=%s",
                topicText.c_str(),
                messageId.c_str());
    return false;
  }

  mqttClient.loop();
  appLogInfo("publishTrhNotice success. topic=%s requestId=%s result=%s temperature=%.2f humidity=%.2f pressure=%.2f",
             topicText.c_str(),
             messageId.c_str(),
             isSuccess ? "OK" : "NG",
             static_cast<double>(snapshot.temperatureC),
             static_cast<double>(snapshot.humidityRh),
             static_cast<double>(snapshot.pressureHpa));
  return true;
}

/**
 * @brief OTA進捗通知をpublishする。
 * @param progressPercent 進捗率。
 * @param phase OTAフェーズ。
 * @param detail 補足メッセージ。
 * @param firmwareVersion 対象ファームウェア版数。
 * @return publish成功時true。
 */
bool publishOtaProgressNotice(int32_t progressPercent,
                              const char* phase,
                              const char* detail,
                              const char* firmwareVersion) {
  if (!mqttClient.connected()) {
    appLogError("publishOtaProgressNotice failed. mqtt is not connected.");
    return false;
  }

  if (deviceNodeName.length() <= 0) {
    bool resolveNameResult = resolveDeviceNodeName(&deviceNodeName);
    if (!resolveNameResult) {
      appLogError("publishOtaProgressNotice failed. resolveDeviceNodeName failed.");
      return false;
    }
  }

  String topicText;
  if (!createTopicText("notice", "otaProgress", deviceNodeName.c_str(), &topicText)) {
    appLogError("publishOtaProgressNotice failed. createTopicText failed.");
    return false;
  }

  const String messageId = String(deviceNodeName) + "-" + millis();
  String payloadText;
  jsonService payloadJsonService;
  jsonKeyValueItem itemList[] = {
      {"v", jsonValueType::kString, iotCommon::kProtocolVersion, 0, 0, false},
      {"DstID", jsonValueType::kString, "all", 0, 0, false},
      {"SrcID", jsonValueType::kString, deviceNodeName.c_str(), 0, 0, false},
      {"Request", jsonValueType::kString, "Notice", 0, 0, false},
      {"id", jsonValueType::kString, messageId.c_str(), 0, 0, false},
      {"sub", jsonValueType::kString, "otaProgress", 0, 0, false},
      {"fwVersion", jsonValueType::kString, firmwareVersion == nullptr ? "" : firmwareVersion, 0, 0, false},
      {"firmwareVersion", jsonValueType::kString, firmwareVersion == nullptr ? "" : firmwareVersion, 0, 0, false},
      {"phase", jsonValueType::kString, phase == nullptr ? "" : phase, 0, 0, false},
      {"detail", jsonValueType::kString, detail == nullptr ? "" : detail, 0, 0, false},
      {"progressPercent", jsonValueType::kLong, nullptr, 0, progressPercent, false},
  };
  if (!payloadJsonService.setValuesByPath(&payloadText, itemList, sizeof(itemList) / sizeof(itemList[0]))) {
    appLogError("publishOtaProgressNotice failed. setValuesByPath returned false. phase=%s detail=%s progress=%ld",
                phase == nullptr ? "(null)" : phase,
                detail == nullptr ? "(null)" : detail,
                static_cast<long>(progressPercent));
    return false;
  }

  String outgoingPayloadText;
  if (!resolveOutgoingPayloadText(topicText, payloadText, &outgoingPayloadText)) {
    appLogError("publishOtaProgressNotice failed. resolveOutgoingPayloadText failed. topic=%s", topicText.c_str());
    return false;
  }
  const bool publishResult = mqttClient.publish(topicText.c_str(), outgoingPayloadText.c_str(), false);
  if (!publishResult) {
    appLogError("publishOtaProgressNotice failed. topic=%s payloadLength=%ld",
                topicText.c_str(),
                static_cast<long>(payloadText.length()));
    return false;
  }
  mqttClient.loop();
  appLogInfo("publishOtaProgressNotice success. topic=%s phase=%s progress=%ld",
             topicText.c_str(),
             phase == nullptr ? "" : phase,
             static_cast<long>(progressPercent));
  return true;
}
}

/**
 * @brief MQTTタスクを生成し、受信用キューを登録する。
 * @return 生成成功時true、失敗時false。
 */
bool mqttTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kMqtt, 8);

  if (mqttTaskStackBuffer == nullptr) {
    mqttTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (mqttTaskStackBuffer == nullptr) {
    appLogWarn("mqttTask: PSRAM stack allocation failed. fallback to internal RAM.");
    mqttTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (mqttTaskStackBuffer == nullptr) {
    appLogError("mqttTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "mqttTask",
      taskStackSize,
      this,
      taskPriority,
      mqttTaskStackBuffer,
      &mqttTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("mqttTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("mqttTask created. stackBytes=%u", static_cast<unsigned>(taskStackSize));
  return true;
}

/**
 * @brief FreeRTOSタスクエントリ。
 * @param taskParameter thisポインタ。
 */
void mqttTask::taskEntry(void* taskParameter) {
  mqttTask* self = static_cast<mqttTask*>(taskParameter);
  self->runLoop();
}

/**
 * @brief MQTTタスクの常駐ループ。
 * @details
 * - 起動要求受信時: startup ackを返信。
 * - MQTT初期化要求受信時: 接続結果を返信。
 * - online publish要求受信時: publish結果を返信。
 */
void mqttTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("mqttTask loop started. (skeleton)");
  for (;;) {
    if (isMqttInitialized && mqttClient.connected()) {
      mqttClient.loop();
    }

    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kMqtt, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kMqtt;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "mqttTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kMqttInitRequest) {
      appLogInfo("mqttTask: init request received. url=%s user=%s pass=%s port=%ld tls=%d",
                 receivedMessage.text,
                 receivedMessage.text2,
                 (strlen(receivedMessage.text3) > 0 ? "******" : "(empty)"),
                 static_cast<long>(receivedMessage.intValue),
                 static_cast<int>(receivedMessage.boolValue));

      storeMqttConfig(receivedMessage);
      bool initResult = connectToMqttBroker();
      isMqttInitialized = initResult;

      appTaskMessage doneMessage{};
      doneMessage.sourceTaskId = appTaskId::kMqtt;
      doneMessage.destinationTaskId = appTaskId::kMain;
      doneMessage.messageType = initResult ? appMessageType::kMqttInitDone : appMessageType::kTaskError;
      doneMessage.intValue = initResult ? 1 : 0;
      strncpy(doneMessage.text, initResult ? "mqtt init done" : "mqtt init failed", sizeof(doneMessage.text) - 1);
      doneMessage.text[sizeof(doneMessage.text) - 1] = '\0';
      bool sendResult = messageService.sendMessage(doneMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("mqttTask: failed to send mqtt init response.");
      } else {
        appLogInfo("mqttTask: mqtt init response sent. type=%d detail=%s",
                   static_cast<int>(doneMessage.messageType),
                   doneMessage.text);
      }
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kMqttPublishOnlineRequest) {
      appLogInfo("mqttTask: publish online request received. message=%s", receivedMessage.text);
      if (receivedMessage.intValue > 0) {
        mainTaskStartupCpuMillis = static_cast<uint32_t>(receivedMessage.intValue);
      }

      const char* requestSubName = (strlen(receivedMessage.text2) > 0)
                                       ? receivedMessage.text2
                                       : iotCommon::mqtt::subCommand::status::kStartUp;
      const char* requestOnlineState = (strlen(receivedMessage.text3) > 0)
                                           ? receivedMessage.text3
                                           : statusValueOnline;
      bool publishResult = isMqttInitialized && publishStatusNotice(requestSubName,
                                                                    requestOnlineState,
                                                                    mainTaskStartupCpuMillis);
      if (!publishResult && !mqttClient.connected()) {
        // [重要] 送信失敗かつ切断状態なら初期化完了フラグを落として再接続シーケンスへ委譲する。
        isMqttInitialized = false;
      }
      appTaskMessage doneMessage{};
      doneMessage.sourceTaskId = appTaskId::kMqtt;
      doneMessage.destinationTaskId = appTaskId::kMain;
      doneMessage.messageType = publishResult ? appMessageType::kMqttPublishOnlineDone : appMessageType::kTaskError;
      doneMessage.intValue = publishResult ? 1 : 0;
      strncpy(doneMessage.text, publishResult ? "mqtt online publish done" : "mqtt online publish failed", sizeof(doneMessage.text) - 1);
      doneMessage.text[sizeof(doneMessage.text) - 1] = '\0';
      bool sendResult = messageService.sendMessage(doneMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("mqttTask: failed to send mqtt publish response.");
      } else {
        appLogInfo("mqttTask: mqtt publish response sent. type=%d detail=%s",
                   static_cast<int>(doneMessage.messageType),
                   doneMessage.text);
      }
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kMqttPublishOtaProgressRequest) {
      bool publishResult = isMqttInitialized &&
                           publishOtaProgressNotice(receivedMessage.intValue,
                                                    receivedMessage.text,
                                                    receivedMessage.text2,
                                                    receivedMessage.text3);
      if (!publishResult && !mqttClient.connected()) {
        isMqttInitialized = false;
      }
      if (!publishResult) {
        appLogError("mqttTask: ota progress publish failed. phase=%s detail=%s progress=%ld version=%s",
                    receivedMessage.text,
                    receivedMessage.text2,
                    static_cast<long>(receivedMessage.intValue),
                    receivedMessage.text3);
      } else {
        appLogInfo("mqttTask: ota progress published. phase=%s detail=%s progress=%ld version=%s",
                   receivedMessage.text,
                   receivedMessage.text2,
                   static_cast<long>(receivedMessage.intValue),
                   receivedMessage.text3);
      }
      if (isTerminalOtaPhase(receivedMessage.text)) {
        appTaskMessage doneMessage{};
        doneMessage.sourceTaskId = appTaskId::kMqtt;
        doneMessage.destinationTaskId = receivedMessage.sourceTaskId;
        doneMessage.messageType = appMessageType::kMqttPublishOtaProgressDone;
        doneMessage.intValue = publishResult ? 1 : 0;
        doneMessage.intValue2 = receivedMessage.intValue;
        strncpy(doneMessage.text, receivedMessage.text, sizeof(doneMessage.text) - 1);
        doneMessage.text[sizeof(doneMessage.text) - 1] = '\0';
        strncpy(doneMessage.text2,
                publishResult ? "mqtt ota terminal publish done" : "mqtt ota terminal publish failed",
                sizeof(doneMessage.text2) - 1);
        doneMessage.text2[sizeof(doneMessage.text2) - 1] = '\0';
        bool sendResult = messageService.sendMessage(doneMessage, pdMS_TO_TICKS(300));
        if (!sendResult) {
          appLogError("mqttTask: failed to send ota terminal publish ack. phase=%s progress=%ld targetTask=%d",
                      receivedMessage.text,
                      static_cast<long>(receivedMessage.intValue),
                      static_cast<int>(receivedMessage.sourceTaskId));
        } else {
          appLogInfo("mqttTask: ota terminal publish ack sent. phase=%s progress=%ld result=%d targetTask=%d",
                     receivedMessage.text,
                     static_cast<long>(receivedMessage.intValue),
                     publishResult ? 1 : 0,
                     static_cast<int>(receivedMessage.sourceTaskId));
        }
      }
    }

    // TODO: MQTT初期化、接続、subscribe/publish処理を実装する。
    // [重要] OTA終端通知(done/error)の遅延を抑えるため、ループ間隔を短縮する。
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
