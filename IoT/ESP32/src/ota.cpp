/**
 * @file ota.cpp
 * @brief OTA更新機能のタスク実装。
 * @details
 * - [重要] MQTTで受信したOTA開始要求を受け、HTTPS/HTTPからfirmware.binを取得して更新する。
 * - [厳守] SHA256一致を確認できた場合のみOTAを確定する。
 * - [禁止] 検証失敗時にUpdate.end(true)で不完全イメージを確定しない。
 * - [将来対応] 同一進捗率ごとの細粒度リトライ制御は別途拡張する。
 */

#include "ota.h"

#include <Update.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "firmwareInfo.h"
#include "interTaskMessage.h"
#include "log.h"
#include "sensitiveData.h"
#include "util.h"

namespace {
StackType_t* otaTaskStackBuffer = nullptr;
StaticTask_t otaTaskControlBlock;
otaStartRequestContext pendingOtaStartRequest{};
bool hasPendingOtaStartRequest = false;

constexpr uint32_t kOtaProgressHoldMs = 0;
constexpr uint32_t kOtaAttemptMaxCount = 3;
constexpr uint32_t kOtaRetryDelayMs = 5000;
constexpr uint32_t kOtaDonePublishWaitMs = 2000;
constexpr uint32_t kOtaTerminalPublishAckTimeoutMs = 3500;
constexpr size_t kDownloadBufferSize = 4096;
constexpr uint16_t kDefaultHttpPort = 80;
constexpr uint16_t kDefaultHttpsPort = 443;
constexpr int32_t kOtaProgressPublishStepPercent = 5;

struct otaUrlInfo {
  String scheme;
  String host;
  uint16_t port;
  String path;
};

/**
 * @brief OTA向けTLSクライアント。
 * @details
 * - [重要] IP直指定で接続する場合でも、証明書検証は本来のホスト名で実施する。
 * - [禁止] OTAだけ `setInsecure()` で暫定運用しない。
 */
class OtaTlsClient : public WiFiClientSecure {
 public:
  /**
   * @brief IP接続時に使うTLS検証コンテキストを設定する。
   * @param tlsHostName 証明書検証に使うホスト名。
   * @param tlsCaCertificate ルートCA証明書PEM。
   */
  void setTlsValidationContext(const char* tlsHostName, const char* tlsCaCertificate) {
    tlsHostName_ = tlsHostName;
    tlsCaCertificate_ = tlsCaCertificate;
  }

  /**
   * @brief IPアドレス指定でTLS接続する。
   * @param ip 接続先IPアドレス。
   * @param port 接続先ポート。
   * @return 接続成功時1、失敗時0。
   */
  int connect(IPAddress ip, uint16_t port) override {
    bool hostAvailable = (tlsHostName_ != nullptr && strlen(tlsHostName_) > 0);
    bool caAvailable = (tlsCaCertificate_ != nullptr && strlen(tlsCaCertificate_) > 0);
    if (!hostAvailable || !caAvailable) {
      appLogWarn("OtaTlsClient::connect fallback to default path. ip=%s port=%u hostAvailable=%d caAvailable=%d",
                 ip.toString().c_str(),
                 static_cast<unsigned>(port),
                 hostAvailable ? 1 : 0,
                 caAvailable ? 1 : 0);
      return WiFiClientSecure::connect(ip, port);
    }
    return WiFiClientSecure::connect(ip, port, tlsHostName_, tlsCaCertificate_, nullptr, nullptr);
  }

 private:
  const char* tlsHostName_ = nullptr;
  const char* tlsCaCertificate_ = nullptr;
};

String buildOtaDetailText(const char* detailPrefix, int32_t attemptNumber) {
  String detailText = detailPrefix == nullptr ? "" : String(detailPrefix);
  if (attemptNumber > 0) {
    detailText += " attempt ";
    detailText += String(attemptNumber);
    detailText += "/";
    detailText += String(kOtaAttemptMaxCount);
  }
  return detailText;
}

/**
 * @brief 現在のUTC時刻をISO8601文字列で取得する。
 * @details
 * - [重要] OTA成功時刻の永続化に使う。
 * - [制限] NTP未同期時は空文字を返す。
 * @return ISO8601文字列。未同期や変換失敗時は空文字。
 */
String getCurrentUtcIso8601Text() {
  constexpr time_t kMinimumValidEpochSeconds = 1609459200;  // 2021-01-01T00:00:00Z
  const time_t currentEpochSeconds = time(nullptr);
  if (currentEpochSeconds < kMinimumValidEpochSeconds) {
    appLogWarn("getCurrentUtcIso8601Text skipped. currentEpochSeconds=%ld", static_cast<long>(currentEpochSeconds));
    return "";
  }

  struct tm utcTimeInfo {};
  if (gmtime_r(&currentEpochSeconds, &utcTimeInfo) == nullptr) {
    appLogError("getCurrentUtcIso8601Text failed. gmtime_r returned null. currentEpochSeconds=%ld",
                static_cast<long>(currentEpochSeconds));
    return "";
  }

  char formattedBuffer[32] = {};
  const size_t writtenLength = strftime(formattedBuffer, sizeof(formattedBuffer), "%Y-%m-%dT%H:%M:%SZ", &utcTimeInfo);
  if (writtenLength <= 0) {
    appLogError("getCurrentUtcIso8601Text failed. strftime returned 0. currentEpochSeconds=%ld",
                static_cast<long>(currentEpochSeconds));
    return "";
  }

  return String(formattedBuffer);
}

/**
 * @brief MQTTへ進捗を送るべきwrite進捗率か判定する。
 * @details
 * - [重要] UI表示よりも最終 `done/error` 通知の確実性を優先し、write進捗は5%刻みへ間引く。
 * - [重要] 99%以上は `done` に任せるため、writeフェーズでは送信しない。
 * @param progressPercent 今回算出した進捗率。
 * @param lastPublishedProgressPercent 前回publishした進捗率。
 * @returns publish対象ならtrue。
 */
bool shouldPublishWriteProgress(int32_t progressPercent, int32_t lastPublishedProgressPercent) {
  if (progressPercent <= 0 || progressPercent >= 99) {
    return false;
  }
  if (lastPublishedProgressPercent < 0) {
    return true;
  }
  return (progressPercent - lastPublishedProgressPercent) >= kOtaProgressPublishStepPercent;
}

/**
 * @brief MQTTタスクからOTA終端通知のpublish完了ACKを待機する。
 * @details
 * - [重要] `done/error` は「キュー投入成功」ではなく「MQTT publish実行結果」を確認する。
 * - [制限] ACK待機中に他メッセージを受信した場合はログを残して無視する。
 * @param expectedProgressPercent 期待する進捗率。
 * @param expectedPhase 期待するフェーズ。
 * @return ACK成功時true、失敗/タイムアウト時false。
 */
bool waitForTerminalPublishAck(int32_t expectedProgressPercent, const String& expectedPhase) {
  interTaskMessageService& messageService = getInterTaskMessageService();
  const uint32_t waitStartMs = millis();
  while ((millis() - waitStartMs) < kOtaTerminalPublishAckTimeoutMs) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kOta, &receivedMessage, pdMS_TO_TICKS(100));
    if (!receiveResult) {
      continue;
    }
    if (receivedMessage.messageType != appMessageType::kMqttPublishOtaProgressDone) {
      appLogWarn("waitForTerminalPublishAck skipped unexpected message. messageType=%d sourceTask=%d",
                 static_cast<int>(receivedMessage.messageType),
                 static_cast<int>(receivedMessage.sourceTaskId));
      continue;
    }
    String ackPhase = String(receivedMessage.text);
    ackPhase.trim();
    if (!ackPhase.equalsIgnoreCase(expectedPhase) || receivedMessage.intValue2 != expectedProgressPercent) {
      appLogWarn("waitForTerminalPublishAck got mismatched ack. expectedPhase=%s expectedProgress=%ld ackPhase=%s ackProgress=%ld",
                 expectedPhase.c_str(),
                 static_cast<long>(expectedProgressPercent),
                 ackPhase.c_str(),
                 static_cast<long>(receivedMessage.intValue2));
      continue;
    }

    const bool publishResult = (receivedMessage.intValue == 1);
    if (!publishResult) {
      appLogError("waitForTerminalPublishAck failed. phase=%s progress=%ld detail=%s",
                  ackPhase.c_str(),
                  static_cast<long>(receivedMessage.intValue2),
                  receivedMessage.text2);
      return false;
    }
    appLogInfo("waitForTerminalPublishAck success. phase=%s progress=%ld",
               ackPhase.c_str(),
               static_cast<long>(receivedMessage.intValue2));
    return true;
  }

  appLogError("waitForTerminalPublishAck timeout. expectedPhase=%s expectedProgress=%ld timeoutMs=%lu",
              expectedPhase.c_str(),
              static_cast<long>(expectedProgressPercent),
              static_cast<unsigned long>(kOtaTerminalPublishAckTimeoutMs));
  return false;
}

bool publishOtaProgress(int32_t progressPercent,
                        const String& phase,
                        const String& detail,
                        const String& firmwareVersion) {
  const bool isTerminalPhase = phase.equalsIgnoreCase("done") || phase.equalsIgnoreCase("error");
  const uint32_t sendTimeoutMs = isTerminalPhase ? 1500 : 500;
  const int32_t sendRetryCount = isTerminalPhase ? 5 : 3;
  for (int32_t retryIndex = 0; retryIndex < sendRetryCount; ++retryIndex) {
    appUtil::appTaskMessageDetail progressDetail = appUtil::createEmptyMessageDetail();
    progressDetail.hasIntValue = true;
    progressDetail.intValue = progressPercent;
    progressDetail.text = phase.c_str();
    progressDetail.text2 = detail.c_str();
    progressDetail.text3 = firmwareVersion.c_str();
    if (appUtil::sendMessage(appTaskId::kMqtt,
                             appTaskId::kOta,
                             appMessageType::kMqttPublishOtaProgressRequest,
                             &progressDetail,
                             sendTimeoutMs)) {
      if (isTerminalPhase) {
        const bool ackResult = waitForTerminalPublishAck(progressPercent, phase);
        if (!ackResult) {
          appLogWarn("publishOtaProgress terminal ack wait failed. phase=%s progress=%ld retry=%ld/%ld",
                     phase.c_str(),
                     static_cast<long>(progressPercent),
                     static_cast<long>(retryIndex + 1),
                     static_cast<long>(sendRetryCount));
          continue;
        }
      }
      return true;
    }
    if (retryIndex + 1 < sendRetryCount) {
      appLogWarn("publishOtaProgress retry. phase=%s progress=%ld retry=%ld/%ld",
                 phase.c_str(),
                 static_cast<long>(progressPercent),
                 static_cast<long>(retryIndex + 1),
                 static_cast<long>(sendRetryCount));
      delay(isTerminalPhase ? 150 : 50);
    }
  }
  appLogError("publishOtaProgress failed after retries. phase=%s progress=%ld detail=%s version=%s",
              phase.c_str(),
              static_cast<long>(progressPercent),
              detail.c_str(),
              firmwareVersion.c_str());
  return false;
}

void updateOtaDisplay(const String& line1, const String& line2) {
  appLogInfo("updateOtaDisplay: line1=%s line2=%s holdMs=%lu",
             line1.substring(0, 16).c_str(),
             line2.substring(0, 16).c_str(),
             static_cast<unsigned long>(kOtaProgressHoldMs));
}

String convertSha256ToHex(const unsigned char hashBytes[32]) {
  static constexpr char hexChars[] = "0123456789abcdef";
  char hashText[65];
  for (size_t index = 0; index < 32; ++index) {
    hashText[index * 2] = hexChars[(hashBytes[index] >> 4) & 0x0F];
    hashText[index * 2 + 1] = hexChars[hashBytes[index] & 0x0F];
  }
  hashText[64] = '\0';
  return String(hashText);
}

bool parseOtaUrl(const String& urlText, otaUrlInfo* urlInfoOut) {
  if (urlInfoOut == nullptr) {
    appLogError("parseOtaUrl failed. urlInfoOut is null.");
    return false;
  }
  const int schemeSeparatorIndex = urlText.indexOf("://");
  if (schemeSeparatorIndex <= 0) {
    appLogError("parseOtaUrl failed. scheme separator not found. url=%s", urlText.c_str());
    return false;
  }

  otaUrlInfo parsedUrlInfo;
  parsedUrlInfo.scheme = urlText.substring(0, schemeSeparatorIndex);
  const String remainText = urlText.substring(schemeSeparatorIndex + 3);
  const int pathStartIndex = remainText.indexOf('/');
  const String hostPortText = pathStartIndex >= 0 ? remainText.substring(0, pathStartIndex) : remainText;
  parsedUrlInfo.path = pathStartIndex >= 0 ? remainText.substring(pathStartIndex) : "/";

  const int portSeparatorIndex = hostPortText.indexOf(':');
  if (portSeparatorIndex >= 0) {
    parsedUrlInfo.host = hostPortText.substring(0, portSeparatorIndex);
    parsedUrlInfo.port = static_cast<uint16_t>(hostPortText.substring(portSeparatorIndex + 1).toInt());
  } else {
    parsedUrlInfo.host = hostPortText;
    parsedUrlInfo.port = parsedUrlInfo.scheme.equalsIgnoreCase("https") ? kDefaultHttpsPort : kDefaultHttpPort;
  }

  if (parsedUrlInfo.host.length() == 0 || parsedUrlInfo.port == 0) {
    appLogError("parseOtaUrl failed. invalid host or port. url=%s host=%s port=%u",
                urlText.c_str(),
                parsedUrlInfo.host.c_str(),
                static_cast<unsigned>(parsedUrlInfo.port));
    return false;
  }

  *urlInfoOut = parsedUrlInfo;
  return true;
}

bool resolveOtaTargetIp(const otaUrlInfo& urlInfo, IPAddress* resolvedIpAddressOut, bool* dnsResolvedOut, bool* fallbackUsedOut) {
  if (resolvedIpAddressOut == nullptr || dnsResolvedOut == nullptr || fallbackUsedOut == nullptr) {
    appLogError("resolveOtaTargetIp failed. invalid parameter.");
    return false;
  }

  *dnsResolvedOut = false;
  *fallbackUsedOut = false;

  IPAddress directIpAddress;
  if (directIpAddress.fromString(urlInfo.host)) {
    *resolvedIpAddressOut = directIpAddress;
    *dnsResolvedOut = true;
    return true;
  }

  IPAddress resolvedIpAddress;
  if (WiFi.hostByName(urlInfo.host.c_str(), resolvedIpAddress)) {
    *resolvedIpAddressOut = resolvedIpAddress;
    *dnsResolvedOut = true;
    return true;
  }

  IPAddress fallbackIpAddress;
  if (strlen(SENSITIVE_MQTT_FALLBACK_IP) > 0 && fallbackIpAddress.fromString(SENSITIVE_MQTT_FALLBACK_IP)) {
    *resolvedIpAddressOut = fallbackIpAddress;
    *fallbackUsedOut = true;
    appLogWarn("resolveOtaTargetIp fallback will be used. host=%s fallbackIp=%s",
               urlInfo.host.c_str(),
               fallbackIpAddress.toString().c_str());
    return true;
  }

  appLogError("resolveOtaTargetIp failed. host=%s fallback=%s",
              urlInfo.host.c_str(),
              SENSITIVE_MQTT_FALLBACK_IP);
  return false;
}

bool logOtaConnectionDiagnostics(const String& firmwareUrl, otaUrlInfo* urlInfoOut, IPAddress* resolvedIpAddressOut) {
  if (urlInfoOut == nullptr || resolvedIpAddressOut == nullptr) {
    appLogError("logOtaConnectionDiagnostics failed. output parameter is null.");
    return false;
  }

  otaUrlInfo urlInfo;
  if (!parseOtaUrl(firmwareUrl, &urlInfo)) {
    return false;
  }

  IPAddress resolvedIpAddress;
  bool dnsResolved = false;
  bool fallbackUsed = false;
  bool resolveResult = resolveOtaTargetIp(urlInfo, &resolvedIpAddress, &dnsResolved, &fallbackUsed);
  appLogInfo("OTA diagnostics. url=%s scheme=%s host=%s port=%u path=%s resolveResult=%d dnsResolved=%d fallbackUsed=%d resolvedIp=%s",
             firmwareUrl.c_str(),
             urlInfo.scheme.c_str(),
             urlInfo.host.c_str(),
             static_cast<unsigned>(urlInfo.port),
             urlInfo.path.c_str(),
             resolveResult ? 1 : 0,
             dnsResolved ? 1 : 0,
             fallbackUsed ? 1 : 0,
             resolveResult ? resolvedIpAddress.toString().c_str() : "(unresolved)");

  if (!resolveResult) {
    return false;
  }

  WiFiClient tcpProbeClient;
  const bool tcpProbeResult = tcpProbeClient.connect(resolvedIpAddress, urlInfo.port);
  appLogInfo("OTA diagnostics. tcpProbe host=%s ip=%s port=%u result=%d",
             urlInfo.host.c_str(),
             resolvedIpAddress.toString().c_str(),
             static_cast<unsigned>(urlInfo.port),
             tcpProbeResult ? 1 : 0);
  if (tcpProbeResult) {
    tcpProbeClient.stop();
  }

  *urlInfoOut = urlInfo;
  *resolvedIpAddressOut = resolvedIpAddress;
  return true;
}

bool writeHttpGetRequest(WiFiClient* client, const otaUrlInfo& urlInfo) {
  if (client == nullptr) {
    appLogError("writeHttpGetRequest failed. client is null.");
    return false;
  }
  const String requestText =
      String("GET ") + urlInfo.path + " HTTP/1.1\r\n" +
      "Host: " + urlInfo.host + "\r\n" +
      "User-Agent: esp32lab-ota/1.0\r\n" +
      "Connection: close\r\n\r\n";
  const size_t writtenSize = client->print(requestText);
  if (writtenSize != requestText.length()) {
    appLogError("writeHttpGetRequest failed. writtenSize=%u expected=%u host=%s path=%s",
                static_cast<unsigned>(writtenSize),
                static_cast<unsigned>(requestText.length()),
                urlInfo.host.c_str(),
                urlInfo.path.c_str());
    return false;
  }
  return true;
}

bool readHttpResponseHeader(WiFiClient* client, int* statusCodeOut, int32_t* contentLengthOut, String* errorDetailOut) {
  if (client == nullptr || statusCodeOut == nullptr || contentLengthOut == nullptr || errorDetailOut == nullptr) {
    appLogError("readHttpResponseHeader failed. invalid parameter.");
    return false;
  }

  *statusCodeOut = -1;
  *contentLengthOut = -1;
  *errorDetailOut = "";

  String statusLine = client->readStringUntil('\n');
  statusLine.trim();
  if (!statusLine.startsWith("HTTP/1.")) {
    *errorDetailOut = "invalid status line";
    appLogError("readHttpResponseHeader failed. invalid status line. statusLine=%s", statusLine.c_str());
    return false;
  }

  const int firstSpaceIndex = statusLine.indexOf(' ');
  const int secondSpaceIndex = statusLine.indexOf(' ', firstSpaceIndex + 1);
  if (firstSpaceIndex < 0 || secondSpaceIndex < 0) {
    *errorDetailOut = "status parse failed";
    appLogError("readHttpResponseHeader failed. could not parse status line. statusLine=%s", statusLine.c_str());
    return false;
  }
  *statusCodeOut = statusLine.substring(firstSpaceIndex + 1, secondSpaceIndex).toInt();

  for (;;) {
    String headerLine = client->readStringUntil('\n');
    if (headerLine.length() == 0) {
      continue;
    }
    headerLine.trim();
    if (headerLine.length() == 0) {
      break;
    }
    if (headerLine.startsWith("Content-Length:")) {
      *contentLengthOut = headerLine.substring(strlen("Content-Length:")).toInt();
    }
  }
  return true;
}

bool takePendingOtaStartRequest(otaStartRequestContext* requestContextOut) {
  if (requestContextOut == nullptr || !hasPendingOtaStartRequest) {
    return false;
  }
  *requestContextOut = pendingOtaStartRequest;
  hasPendingOtaStartRequest = false;
  pendingOtaStartRequest = otaStartRequestContext{};
  return true;
}

bool executeSingleOtaAttempt(const otaStartRequestContext& requestContext,
                             int32_t attemptNumber,
                             String* errorDetailOut) {
  if (errorDetailOut == nullptr) {
    appLogError("executeSingleOtaAttempt failed. errorDetailOut is null.");
    return false;
  }
  if (requestContext.firmwareUrl.length() == 0) {
    *errorDetailOut = "firmwareUrl empty";
    return false;
  }

  otaUrlInfo urlInfo;
  IPAddress resolvedIpAddress;
  if (!logOtaConnectionDiagnostics(requestContext.firmwareUrl, &urlInfo, &resolvedIpAddress)) {
    *errorDetailOut = "ota resolve failed";
    return false;
  }

  WiFiClient plainClient;
  OtaTlsClient secureClient;
  const bool useTls = requestContext.firmwareUrl.startsWith("https://");
  WiFiClient* activeClient = nullptr;
  bool connectResult = false;

  if (useTls) {
    secureClient.setTimeout(15000);
    secureClient.setCACert(SENSITIVE_MQTT_TLS_CA_CERT);
    secureClient.setTlsValidationContext(urlInfo.host.c_str(), SENSITIVE_MQTT_TLS_CA_CERT);
    connectResult = secureClient.connect(resolvedIpAddress, urlInfo.port);
    activeClient = &secureClient;
  } else {
    plainClient.setTimeout(15000);
    connectResult = plainClient.connect(resolvedIpAddress, urlInfo.port);
    activeClient = &plainClient;
  }

  if (!connectResult || activeClient == nullptr) {
    *errorDetailOut = "http connect failed";
    appLogError("executeSingleOtaAttempt failed. connect failed. host=%s ip=%s port=%u useTls=%d",
                urlInfo.host.c_str(),
                resolvedIpAddress.toString().c_str(),
                static_cast<unsigned>(urlInfo.port),
                useTls ? 1 : 0);
    return false;
  }

  if (!writeHttpGetRequest(activeClient, urlInfo)) {
    *errorDetailOut = "http request failed";
    activeClient->stop();
    return false;
  }

  int httpStatusCode = -1;
  int32_t contentLength = -1;
  String headerErrorDetail;
  if (!readHttpResponseHeader(activeClient, &httpStatusCode, &contentLength, &headerErrorDetail)) {
    *errorDetailOut = headerErrorDetail.length() > 0 ? headerErrorDetail : "header read failed";
    activeClient->stop();
    return false;
  }

  if (httpStatusCode != 200) {
    *errorDetailOut = "http status " + String(httpStatusCode);
    appLogError("executeSingleOtaAttempt failed. GET status=%d url=%s",
                httpStatusCode,
                requestContext.firmwareUrl.c_str());
    activeClient->stop();
    return false;
  }

  if (!Update.begin(contentLength > 0 ? static_cast<size_t>(contentLength) : UPDATE_SIZE_UNKNOWN)) {
    *errorDetailOut = "Update.begin failed";
    appLogError("executeSingleOtaAttempt failed. Update.begin failed. contentLength=%d error=%s",
                contentLength,
                Update.errorString());
    activeClient->stop();
    return false;
  }

  uint8_t downloadBuffer[kDownloadBufferSize];
  int32_t totalWrittenBytes = 0;
  int32_t lastPublishedPercent = -1;
  mbedtls_sha256_context sha256Context;
  mbedtls_sha256_init(&sha256Context);
  mbedtls_sha256_starts_ret(&sha256Context, 0);

  publishOtaProgress(0, "prepare", buildOtaDetailText("download start", attemptNumber), requestContext.firmwareVersion);
  updateOtaDisplay("OTA START", String("TRY ") + attemptNumber);

  while (activeClient->connected() || activeClient->available() > 0) {
    const size_t availableBytes = activeClient->available();
    if (availableBytes == 0) {
      delay(20);
      continue;
    }

    const size_t readSize = activeClient->readBytes(downloadBuffer, min(availableBytes, sizeof(downloadBuffer)));
    if (readSize == 0) {
      delay(20);
      continue;
    }

    mbedtls_sha256_update_ret(&sha256Context, downloadBuffer, readSize);
    const size_t writtenSize = Update.write(downloadBuffer, readSize);
    if (writtenSize != readSize) {
      *errorDetailOut = "Update.write failed";
      appLogError("executeSingleOtaAttempt failed. Update.write mismatch. readSize=%u writtenSize=%u error=%s",
                  static_cast<unsigned>(readSize),
                  static_cast<unsigned>(writtenSize),
                  Update.errorString());
      mbedtls_sha256_free(&sha256Context);
      Update.abort();
      activeClient->stop();
      return false;
    }

    totalWrittenBytes += static_cast<int32_t>(writtenSize);
    if (contentLength > 0) {
      const int32_t progressPercent =
          static_cast<int32_t>((static_cast<int64_t>(totalWrittenBytes) * 100) / contentLength);
      if (shouldPublishWriteProgress(progressPercent, lastPublishedPercent)) {
        lastPublishedPercent = progressPercent;
        publishOtaProgress(progressPercent,
                           "write",
                           buildOtaDetailText("downloading", attemptNumber),
                           requestContext.firmwareVersion);
        updateOtaDisplay(String("OTA ") + progressPercent + "%", requestContext.firmwareVersion);
      }
    }
  }

  unsigned char hashBytes[32];
  mbedtls_sha256_finish_ret(&sha256Context, hashBytes);
  mbedtls_sha256_free(&sha256Context);

  if (contentLength > 0 && totalWrittenBytes != contentLength) {
    *errorDetailOut = "content length mismatch";
    appLogError("executeSingleOtaAttempt failed. content length mismatch. expected=%ld actual=%ld url=%s",
                static_cast<long>(contentLength),
                static_cast<long>(totalWrittenBytes),
                requestContext.firmwareUrl.c_str());
    Update.abort();
    activeClient->stop();
    return false;
  }

  const String calculatedSha256 = convertSha256ToHex(hashBytes);
  if (requestContext.firmwareSha256.length() > 0 &&
      !calculatedSha256.equalsIgnoreCase(requestContext.firmwareSha256)) {
    *errorDetailOut = "sha256 mismatch";
    appLogError("executeSingleOtaAttempt failed. sha256 mismatch. expected=%s actual=%s url=%s",
                requestContext.firmwareSha256.c_str(),
                calculatedSha256.c_str(),
                requestContext.firmwareUrl.c_str());
    Update.abort();
    activeClient->stop();
    return false;
  }

  publishOtaProgress(95, "verify", "sha256 ok", requestContext.firmwareVersion);
  updateOtaDisplay("OTA VERIFY", "SHA256 OK");

  if (!Update.end()) {
    *errorDetailOut = String("Update.end failed: ") + Update.errorString();
    appLogError("executeSingleOtaAttempt failed. Update.end failed. error=%s", Update.errorString());
    activeClient->stop();
    return false;
  }
  if (!Update.isFinished()) {
    *errorDetailOut = "Update not finished";
    appLogError("executeSingleOtaAttempt failed. Update is not finished.");
    activeClient->stop();
    return false;
  }

  activeClient->stop();
  const String otaAppliedAt = getCurrentUtcIso8601Text();
  if (otaAppliedAt.length() > 0) {
    const bool saveResult = firmwareInfo::saveOtaAppliedAt(requestContext.firmwareVersion, otaAppliedAt);
    if (!saveResult) {
      appLogError("executeSingleOtaAttempt warning. failed to save ota applied time. version=%s otaAppliedAt=%s",
                  requestContext.firmwareVersion.c_str(),
                  otaAppliedAt.c_str());
    }
  } else {
    appLogWarn("executeSingleOtaAttempt warning. ota applied time could not be resolved. version=%s",
               requestContext.firmwareVersion.c_str());
  }
  const bool donePublishResult = publishOtaProgress(100, "done", "success", requestContext.firmwareVersion);
  updateOtaDisplay("OTA SUCCESS", "REBOOT");
  if (!donePublishResult) {
    appLogError("executeSingleOtaAttempt warning. done progress publish failed. version=%s url=%s",
                requestContext.firmwareVersion.c_str(),
                requestContext.firmwareUrl.c_str());
  } else {
    appLogInfo("executeSingleOtaAttempt: done progress published. waitMs=%lu",
               static_cast<unsigned long>(kOtaDonePublishWaitMs));
  }
  vTaskDelay(pdMS_TO_TICKS(kOtaDonePublishWaitMs));
  appLogInfo("executeSingleOtaAttempt success. version=%s url=%s sha256=%s",
             requestContext.firmwareVersion.c_str(),
             requestContext.firmwareUrl.c_str(),
             calculatedSha256.c_str());
  return true;
}
}

bool storePendingOtaStartRequest(const otaStartRequestContext& requestContext) {
  pendingOtaStartRequest = requestContext;
  hasPendingOtaStartRequest = true;
  return true;
}

bool otaTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kOta, 8);

  if (otaTaskStackBuffer == nullptr) {
    otaTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (otaTaskStackBuffer == nullptr) {
    appLogWarn("otaTask: PSRAM stack allocation failed. fallback to internal RAM.");
    otaTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (otaTaskStackBuffer == nullptr) {
    appLogError("otaTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "otaTask",
      taskStackSize,
      this,
      taskPriority,
      otaTaskStackBuffer,
      &otaTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("otaTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("otaTask created.");
  return true;
}

void otaTask::taskEntry(void* taskParameter) {
  otaTask* self = static_cast<otaTask*>(taskParameter);
  self->runLoop();
}

void otaTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("otaTask loop started.");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kOta, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kOta;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "otaTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kOtaStartRequest) {
      otaStartRequestContext requestContext;
      if (!takePendingOtaStartRequest(&requestContext)) {
        appLogError("otaTask: received kOtaStartRequest but no pending request exists.");
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }

      String lastErrorDetail;
      bool otaSuccess = false;
      for (int32_t attemptNumber = 1; attemptNumber <= static_cast<int32_t>(kOtaAttemptMaxCount); ++attemptNumber) {
        publishOtaProgress(0,
                           "prepare",
                           buildOtaDetailText("accepted", attemptNumber),
                           requestContext.firmwareVersion);
        appLogInfo("otaTask: start OTA attempt. attempt=%ld version=%s url=%s",
                   static_cast<long>(attemptNumber),
                   requestContext.firmwareVersion.c_str(),
                   requestContext.firmwareUrl.c_str());
        if (executeSingleOtaAttempt(requestContext, attemptNumber, &lastErrorDetail)) {
          otaSuccess = true;
          break;
        }
        publishOtaProgress(0,
                           "retry",
                           buildOtaDetailText(lastErrorDetail.c_str(), attemptNumber),
                           requestContext.firmwareVersion);
        updateOtaDisplay("OTA RETRY", String(attemptNumber) + "/" + kOtaAttemptMaxCount);
        vTaskDelay(pdMS_TO_TICKS(kOtaRetryDelayMs));
      }

      if (!otaSuccess) {
        publishOtaProgress(0, "error", lastErrorDetail, requestContext.firmwareVersion);
        updateOtaDisplay("OTA ERROR", lastErrorDetail.substring(0, 16));
        appLogError("otaTask: OTA failed after retries. version=%s url=%s reason=%s",
                    requestContext.firmwareVersion.c_str(),
                    requestContext.firmwareUrl.c_str(),
                    lastErrorDetail.c_str());
      } else {
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP.restart();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
