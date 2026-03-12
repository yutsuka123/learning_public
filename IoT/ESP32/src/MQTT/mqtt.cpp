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
#include <PubSubClient.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>

#include "common.h"
#include "firmwareInfo.h"
#include "interTaskMessage.h"
#include "jsonService.h"
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

 private:
  /** @brief IP接続時の証明書照合ホスト名。 */
  const char* tlsHostName_ = nullptr;
  /** @brief IP接続時のCA証明書。 */
  const char* tlsCaCertificate_ = nullptr;
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
sensitiveDataService mqttSensitiveDataService;
bool mqttSensitiveDataInitialized = false;

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
    // [厳守] 証明書検証を無効化しない。CA証明書未設定時はTLS接続を開始しない。
#if !defined(SENSITIVE_MQTT_TLS_CA_CERT)
    appLogError("configureMqttTransportClient failed. SENSITIVE_MQTT_TLS_CA_CERT is not defined.");
    return false;
#else
    if (strlen(SENSITIVE_MQTT_TLS_CA_CERT) == 0) {
      appLogError("configureMqttTransportClient failed. SENSITIVE_MQTT_TLS_CA_CERT is empty.");
      return false;
    }
    mqttTlsNetworkClient.setCACert(SENSITIVE_MQTT_TLS_CA_CERT);
    mqttTlsNetworkClient.setTlsValidationContext(mqttHost, SENSITIVE_MQTT_TLS_CA_CERT);
    mqttClient.setClient(mqttTlsNetworkClient);
    appLogInfo("configureMqttTransportClient: TLS transport selected. tlsHost=%s", mqttHost);
    return true;
#endif
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
    responsePlainText = "{\"result\":\"OK\",\"requestId\":\"" + requestIdText + "\",\"echo\":" + payloadText + ",\"decrypted\":\"" + plainText + "\"}";
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
    bool publishResult = mqttClient.publish(topicText.c_str(), payloadOut.c_str(), false);
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

  mqtt::mqttIncomingMessage parsedMessage{};
  bool parseResult = mqtt::parseMqttIncomingMessage(topicName, payloadText.c_str(), &parsedMessage);
  if (!parseResult) {
    appLogWarn("onMqttMessageReceived: parse failed. topic=%s payload=%s",
               (topicName == nullptr ? "(null)" : topicName),
               payloadText.c_str());
    return;
  }
  appLogInfo("onMqttMessageReceived: parsed. type=%d command=%s sub=%s dstId=%s srcId=%s",
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
  if (isCallTopic && handleCallSubCommand(normalizedSubName, payloadText, parsedMessage)) {
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
  bool bufferSizeResult = mqttClient.setBufferSize(1024);
  if (!bufferSizeResult) {
    appLogError("connectToMqttBroker failed. mqttClient.setBufferSize(1024) failed.");
    return false;
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
  appLogInfo("connectToMqttBroker: will payload prepared. topicLength=%ld payloadLength=%ld",
             static_cast<long>(willTopicText.length()),
             static_cast<long>(willPayloadText.length()));
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
                                            willPayloadText.c_str());

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
  bool publishResult = mqtt::sendMqttStatus(&mqttClient,
                                            topicText.c_str(),
                                            safeSubName,
                                            safeOnlineStateText,
                                            startupCpuMillis);
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

  const bool publishResult = mqttClient.publish(topicText.c_str(), payloadText.c_str(), false);
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
  appLogInfo("mqttTask created.");
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
