/**
 * @file timeServer.cpp
 * @brief タイムサーバー時刻同期タスク実装。
 * @details
 * - [重要] 初回同期はmainTaskからの明示要求で開始する。
 * - [厳守] 24時間ごとに再同期を試み、成功時は最終同期時刻を更新する。
 * - [推奨] 未同期状態では短周期再試行し、同期完了後に24時間周期へ移行する。
 */

#include "timeServer.h"

#include <esp_heap_caps.h>
#include <string.h>
#include <WiFi.h>

#include "timeService.h"
#include "interTaskMessage.h"
#include "log.h"

namespace {
/** @brief timeServerTask用スタック領域。PSRAM優先で確保し、失敗時は内部RAMへフォールバックする。 */
StackType_t* timeServerTaskStackBuffer = nullptr;
/** @brief timeServerTask用の静的タスク制御ブロック。 */
StaticTask_t timeServerTaskControlBlock;
/** @brief 時刻同期サービスの単一インスタンス。 */
timeService timeSyncService;
/** @brief 初回同期要求受信済みフラグ。 */
bool hasReceivedInitRequest = false;
/** @brief 最終同期試行時刻(ms)。 */
uint32_t lastSyncAttemptAtMs = 0;
/** @brief 定期再同期間隔(ms): 24時間。 */
constexpr uint32_t periodicSyncIntervalMs = 24UL * 60UL * 60UL * 1000UL;
/** @brief 未同期時の再試行間隔(ms): 5分。 */
constexpr uint32_t unsyncedRetryIntervalMs = 5UL * 60UL * 1000UL;

/**
 * @brief 現在同期を試行すべきか判定する。
 * @param nowMs 現在時刻（millis）。
 * @return 試行すべき場合true。
 */
bool shouldAttemptSync(uint32_t nowMs) {
  if (!hasReceivedInitRequest) {
    return false;
  }

  uint32_t elapsedMs = nowMs - lastSyncAttemptAtMs;
  if (!timeSyncService.hasSynchronizedOnce()) {
    return elapsedMs >= unsyncedRetryIntervalMs;
  }
  return elapsedMs >= periodicSyncIntervalMs;
}
}  // namespace

bool timeServerTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kTimeServer, 8);

  if (timeServerTaskStackBuffer == nullptr) {
    timeServerTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (timeServerTaskStackBuffer == nullptr) {
    appLogWarn("timeServerTask: PSRAM stack allocation failed. fallback to internal RAM.");
    timeServerTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (timeServerTaskStackBuffer == nullptr) {
    appLogError("timeServerTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "timeServerTask",
      taskStackSize,
      this,
      taskPriority,
      timeServerTaskStackBuffer,
      &timeServerTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("timeServerTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }

  appLogInfo("timeServerTask created.");
  return true;
}

void timeServerTask::taskEntry(void* taskParameter) {
  timeServerTask* self = static_cast<timeServerTask*>(taskParameter);
  self->runLoop();
}

void timeServerTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("timeServerTask loop started.");

  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kTimeServer, &receivedMessage, pdMS_TO_TICKS(50));

    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage startupAckMessage{};
      startupAckMessage.sourceTaskId = appTaskId::kTimeServer;
      startupAckMessage.destinationTaskId = appTaskId::kMain;
      startupAckMessage.messageType = appMessageType::kStartupAck;
      startupAckMessage.intValue = 1;
      strncpy(startupAckMessage.text, "timeServerTask startup ack", sizeof(startupAckMessage.text) - 1);
      startupAckMessage.text[sizeof(startupAckMessage.text) - 1] = '\0';
      messageService.sendMessage(startupAckMessage, pdMS_TO_TICKS(100));
    }

    if (receiveResult && receivedMessage.messageType == appMessageType::kTimeServerInitRequest) {
      hasReceivedInitRequest = true;
      lastSyncAttemptAtMs = millis();
      appLogInfo("timeServerTask: init request received. timeServerUrl=%s timeServerUser=%s timeServerPass=%s timeServerPort=%ld timeServerTls=%d",
                 receivedMessage.text,
                 receivedMessage.text2,
                 (strlen(receivedMessage.text3) > 0 ? "******" : "(empty)"),
                 static_cast<long>(receivedMessage.intValue),
                 static_cast<int>(receivedMessage.boolValue));

      bool initSyncResult = timeSyncService.initializeAndSync(
          String(receivedMessage.text),
          String(receivedMessage.text2),
          String(receivedMessage.text3),
          receivedMessage.intValue,
          receivedMessage.boolValue);

      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kTimeServer;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kTimeServerInitDone;
      responseMessage.intValue = initSyncResult ? 1 : 0;
      strncpy(responseMessage.text,
              initSyncResult ? "time server init done" : "time server init failed",
              sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      strncpy(responseMessage.text2, timeSyncService.getCurrentUtcIso8601().c_str(), sizeof(responseMessage.text2) - 1);
      responseMessage.text2[sizeof(responseMessage.text2) - 1] = '\0';
      bool sendResult = messageService.sendMessage(responseMessage, pdMS_TO_TICKS(200));
      if (!sendResult) {
        appLogError("timeServerTask: failed to send init response.");
      }
    }

    uint32_t nowMs = millis();
    if (shouldAttemptSync(nowMs)) {
      lastSyncAttemptAtMs = nowMs;
      bool periodicSyncResult = timeSyncService.syncNow();
      appLogInfo("timeServerTask periodic sync. result=%d utcNow=%s",
                 static_cast<int>(periodicSyncResult),
                 timeSyncService.getCurrentUtcIso8601().c_str());
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
