/**
 * @file wifi.cpp
 * @brief Wi-Fi機能のタスク実装。
 * @details
 * - [重要] mainTaskから受信した資格情報でSTA接続を行い、結果をメッセージで返信する。
 * - [厳守] 接続再試行時はWi-Fi状態を明示的にリセットしてから再接続する。
 * - [制限] APモードや静的IP設定は未対応。
 */

#include "../header/wifi.h"

#include <esp_heap_caps.h>
#include <string.h>
#include <WiFi.h>

#include "../header/sensitiveData.h"
#include "interTaskMessage.h"
#include "led.h"
#include "log.h"

namespace {
/** @brief wifiTask用スタック領域。PSRAM優先で確保し、失敗時は内部RAMへフォールバックする。 */
StackType_t* wifiTaskStackBuffer = nullptr;
/** @brief wifiTask用の静的タスク制御ブロック。 */
StaticTask_t wifiTaskControlBlock;
/** @brief Wi-Fi接続イベント登録済みフラグ。多重登録を防止する。 */
bool wifiEventHandlerRegistered = false;
/** @brief 現在の接続サイクルでDNS再適用済みかを示す。 */
bool dnsReapplyCompletedForCurrentConnection = false;

/**
 * @brief Wi-Fiステータス値を可読文字列へ変換する。
 * @param wifiStatus Wi-Fiステータス。
 * @return ステータス文字列。
 */
const char* wifiStatusToText(wl_status_t wifiStatus) {
  switch (wifiStatus) {
    case WL_NO_SHIELD:
      return "WL_NO_SHIELD";
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "WL_UNKNOWN";
  }
}

/**
 * @brief Wi-Fi DNS設定を反映する（方法①: ESP32側で明示指定）。
 * @details
 * - [重要] DHCP配布DNSに依存せず、`sensitiveData.h` の定義値を常に適用する。
 * - [将来対応] DHCP/ルーター側で名前解決が安定したら固定DNS指定を廃止する。
 * @return 適用成功時true、失敗時false。
 */
bool applyWifiDnsConfiguration() {
  IPAddress primaryDnsAddress(
      SENSITIVE_WIFI_DNS_PRIMARY_OCTET1,
      SENSITIVE_WIFI_DNS_PRIMARY_OCTET2,
      SENSITIVE_WIFI_DNS_PRIMARY_OCTET3,
      SENSITIVE_WIFI_DNS_PRIMARY_OCTET4);
  IPAddress secondaryDnsAddress(0, 0, 0, 0);

  bool configResult = WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, primaryDnsAddress, secondaryDnsAddress);
  if (!configResult) {
    appLogError("applyWifiDnsConfiguration failed. WiFi.config failed. primary=%s",
                primaryDnsAddress.toString().c_str());
    return false;
  }

  appLogInfo("applyWifiDnsConfiguration success. primary=%s secondary=%s",
             primaryDnsAddress.toString().c_str(),
             secondaryDnsAddress.toString().c_str());
  return true;
}

/**
 * @brief Wi-FiイベントでGOT_IP直後にDNSを再適用するフックを登録する。
 * @details
 * - [重要] DHCP適用後にDNSが上書きされるケースへ対処する。
 * - [厳守] 多重登録を避けるため、本関数は初回のみ登録する。
 */
void ensureWifiEventHandlerRegistered() {
  if (wifiEventHandlerRegistered) {
    return;
  }

  WiFi.onEvent([](arduino_event_id_t eventId, arduino_event_info_t) {
    if (eventId == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      dnsReapplyCompletedForCurrentConnection = false;
      appLogInfo("wifi event handler: connection lost. reset dns re-apply state.");
      return;
    }
    if (eventId != ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      return;
    }

    if (dnsReapplyCompletedForCurrentConnection) {
      return;
    }

    dnsReapplyCompletedForCurrentConnection = true;
    bool dnsApplyResult = applyWifiDnsConfiguration();
    if (!dnsApplyResult) {
      appLogError("wifi event handler: applyWifiDnsConfiguration failed on GOT_IP.");
      dnsReapplyCompletedForCurrentConnection = false;
      return;
    }

    appLogInfo("wifi event handler: DNS re-applied after GOT_IP. dns1=%s dns2=%s",
               WiFi.dnsIP(0).toString().c_str(),
               WiFi.dnsIP(1).toString().c_str());
  });

  wifiEventHandlerRegistered = true;
  appLogInfo("wifi event handler registered. targetEvent=ARDUINO_EVENT_WIFI_STA_GOT_IP");
}

/**
 * @brief Wi-Fi接続を同期的に実行する。
 * @param wifiSsid 接続先SSID。
 * @param wifiPass 接続先パスワード。
 * @return 接続成功時true、失敗時false。
 */
bool connectToWifiRouter(const char* wifiSsid, const char* wifiPass) {
  constexpr int32_t connectAttemptCount = 3;
  constexpr int32_t pollCountPerAttempt = 14;  // 1回あたり約7秒
  constexpr int32_t pollDelayMs = 500;
  constexpr int32_t reconnectBackoffMs = 1200;

  if (wifiSsid == nullptr) {
    appLogError("connectToWifiRouter failed. wifiSsid is null.");
    return false;
  }
  if (strlen(wifiSsid) == 0) {
    appLogError("connectToWifiRouter failed. wifiSsid is empty.");
    return false;
  }

  appLogInfo("connectToWifiRouter start. ssid=%s pass=%s", wifiSsid, (wifiPass != nullptr && strlen(wifiPass) > 0) ? "******" : "(empty)");
  ensureWifiEventHandlerRegistered();
  dnsReapplyCompletedForCurrentConnection = false;

  wl_status_t finalStatus = WL_IDLE_STATUS;
  for (int32_t connectAttemptIndex = 0; connectAttemptIndex < connectAttemptCount; ++connectAttemptIndex) {
    const int32_t displayAttempt = connectAttemptIndex + 1;

    // [重要] ハンドシェイク不安定時の再試行で状態を確実にリセットする。
    WiFi.disconnect(true, true);
    vTaskDelay(pdMS_TO_TICKS(120));
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(120));
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    bool dnsConfigResult = applyWifiDnsConfiguration();
    if (!dnsConfigResult) {
      appLogError("connectToWifiRouter failed. applyWifiDnsConfiguration failed.");
      finalStatus = WL_CONNECT_FAILED;
      break;
    }

    appLogInfo("connectToWifiRouter attempt start. attempt=%ld/%ld ssid=%s",
               static_cast<long>(displayAttempt),
               static_cast<long>(connectAttemptCount),
               wifiSsid);
    WiFi.begin(wifiSsid, (wifiPass == nullptr) ? "" : wifiPass);

    wl_status_t previousStatus = WL_IDLE_STATUS;
    for (int32_t pollIndex = 0; pollIndex < pollCountPerAttempt; ++pollIndex) {
      ledController::indicateWifiConnecting();
      wl_status_t currentStatus = WiFi.status();
      finalStatus = currentStatus;
      if (currentStatus == WL_CONNECTED) {
        ledController::indicateWifiConnected();
        appLogInfo("connectToWifiRouter success. attempt=%ld ip=%s rssi=%d",
                   static_cast<long>(displayAttempt),
                   WiFi.localIP().toString().c_str(),
                   WiFi.RSSI());
        appLogInfo("connectToWifiRouter DNS resolved setting. dns1=%s dns2=%s",
                   WiFi.dnsIP(0).toString().c_str(),
                   WiFi.dnsIP(1).toString().c_str());
        return true;
      }

      if (pollIndex == 0 || currentStatus != previousStatus) {
        appLogWarn("connectToWifiRouter status. attempt=%ld poll=%ld status=%d statusText=%s",
                   static_cast<long>(displayAttempt),
                   static_cast<long>(pollIndex + 1),
                   static_cast<int>(currentStatus),
                   wifiStatusToText(currentStatus));
      } else {
        appLogDebug("connectToWifiRouter waiting. attempt=%ld poll=%ld status=%d statusText=%s",
                    static_cast<long>(displayAttempt),
                    static_cast<long>(pollIndex + 1),
                    static_cast<int>(currentStatus),
                    wifiStatusToText(currentStatus));
      }
      previousStatus = currentStatus;

      // [重要] 明確な失敗状態は次attemptへ速やかに移行して復帰時間を短縮する。
      if (currentStatus == WL_CONNECT_FAILED || currentStatus == WL_NO_SSID_AVAIL) {
        appLogWarn("connectToWifiRouter early-break. attempt=%ld status=%d statusText=%s",
                   static_cast<long>(displayAttempt),
                   static_cast<int>(currentStatus),
                   wifiStatusToText(currentStatus));
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(pollDelayMs));
    }

    appLogWarn("connectToWifiRouter attempt failed. attempt=%ld/%ld finalStatus=%d statusText=%s",
               static_cast<long>(displayAttempt),
               static_cast<long>(connectAttemptCount),
               static_cast<int>(finalStatus),
               wifiStatusToText(finalStatus));
    vTaskDelay(pdMS_TO_TICKS(reconnectBackoffMs));
  }

  appLogError("connectToWifiRouter failed after retries. finalStatus=%d statusText=%s",
              static_cast<int>(finalStatus),
              wifiStatusToText(finalStatus));
  ledController::indicateErrorPattern();
  return false;
}
}

/**
 * @brief Wi-Fiタスクを生成し、受信用キューを登録する。
 * @return 生成成功時true、失敗時false。
 */
bool wifiTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kWifi, 8);

  if (wifiTaskStackBuffer == nullptr) {
    wifiTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (wifiTaskStackBuffer == nullptr) {
    appLogWarn("wifiTask: PSRAM stack allocation failed. fallback to internal RAM.");
    wifiTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (wifiTaskStackBuffer == nullptr) {
    appLogError("wifiTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "wifiTask",
      taskStackSize,
      this,
      taskPriority,
      wifiTaskStackBuffer,
      &wifiTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("wifiTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("wifiTask created. stackBytes=%u", static_cast<unsigned>(taskStackSize));
  return true;
}

/**
 * @brief FreeRTOSタスクエントリ。
 * @param taskParameter thisポインタ。
 */
void wifiTask::taskEntry(void* taskParameter) {
  wifiTask* self = static_cast<wifiTask*>(taskParameter);
  self->runLoop();
}

/**
 * @brief Wi-Fiタスクの常駐ループ。
 * @details
 * - 起動要求受信時: startup ackを返信。
 * - Wi-Fi初期化要求受信時: 接続実行後、成功/失敗をmainTaskへ返信。
 */
void wifiTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("wifiTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kWifi, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kWifi;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "wifiTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }
    if (receiveResult && receivedMessage.messageType == appMessageType::kWifiInitRequest) {
      appLogInfo("wifiTask: init request received. ssid=%s pass=%s",
                 receivedMessage.text,
                 (strlen(receivedMessage.text2) > 0 ? "******" : "(empty)"));

      bool connectResult = connectToWifiRouter(receivedMessage.text, receivedMessage.text2);
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kWifi;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.intValue = connectResult ? 1 : 0;

      if (connectResult) {
        responseMessage.messageType = appMessageType::kWifiInitDone;
        strncpy(responseMessage.text, "wifi init done", sizeof(responseMessage.text) - 1);
      } else {
        responseMessage.messageType = appMessageType::kTaskError;
        strncpy(responseMessage.text, "wifi init failed", sizeof(responseMessage.text) - 1);
      }
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';

      bool sendResult = messageService.sendMessage(responseMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("wifiTask: failed to send response. type=%d", static_cast<int>(responseMessage.messageType));
      } else {
        appLogInfo("wifiTask: response sent. type=%d detail=%s",
                   static_cast<int>(responseMessage.messageType),
                   responseMessage.text);
      }
    }

    // TODO: Wi-Fi初期化、AP接続、再接続制御を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
