/**
 * @file input.cpp
 * @brief GPIO4ボタン入力を監視し、短押し/長押しを処理する入力タスク実装。
 * @details
 * - [重要] 50ms周期で入力監視し、3回連続同一状態でチャタリングを除去する。
 * - [厳守] 短押し(<=500ms)は青/緑LEDパターン後にstatus通知を送信する。
 * - [厳守] 長押し(>=1000ms)は起動中ならメンテナンスモード、通常時は再起動する。
 * - [将来対応] メンテナンスモードの詳細機能は別タスクへ分離する。
 */

#include "input.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <string.h>

#include "common.h"
#include "interTaskMessage.h"
#include "led.h"
#include "log.h"
#include "util.h"

namespace {
/** @brief ボタン入力GPIO番号。@type uint8_t */
constexpr uint8_t buttonInputGpio = 4;
/** @brief ボタン押下時の論理レベル（押下LOW）。@type uint8_t */
constexpr uint8_t buttonPressedLevel = LOW;
/** @brief 入力監視周期(ms)。@type uint32_t */
constexpr uint32_t inputScanIntervalMs = 50;
/** @brief チャタリング確定に必要な連続一致回数。@type uint8_t */
constexpr uint8_t debounceConfirmCount = 3;
/** @brief 短押し判定上限(ms)。@type uint32_t */
constexpr uint32_t shortPressMaxDurationMs = 999;
/** @brief 長押し判定下限(ms)。@type uint32_t */
constexpr uint32_t longPressMinDurationMs = 1000;
/** @brief 起動中長押し判定期間(ms)。@type uint32_t */
constexpr uint32_t startupMaintenanceWindowMs = 30000;

StackType_t* inputTaskStackBuffer = nullptr;
StaticTask_t inputTaskControlBlock;

/** @brief 入力状態管理。 */
struct buttonStateContext {
  bool rawPressed = false;
  bool stablePressed = false;
  uint8_t consecutiveCount = 0;
  bool isPressTimingActive = false;
  bool longPressHandledDuringCurrentPress = false;
  uint32_t stablePressStartMs = 0;
  bool isMaintenanceMode = false;
};

/**
 * @brief ボタンGPIOを初期化する。
 */
void initializeButtonGpio() {
  // [重要] 回路見直し後の押下LOW構成に合わせて内部プルアップを使用する。
  pinMode(buttonInputGpio, INPUT_PULLUP);
}

/**
 * @brief MQTT status通知を要求する。
 * @param sourceDescription ログ表示用の起因説明。
 * @return 送信要求成功時true、失敗時false。
 */
bool requestButtonStatusNotice(const char* sourceDescription) {
  appUtil::appTaskMessageDetail statusDetail = appUtil::createEmptyMessageDetail();
  statusDetail.text = sourceDescription;
  statusDetail.text2 = iotCommon::mqtt::subCommand::status::kButton;
  statusDetail.text3 = "Online";
  bool sendResult = appUtil::sendMessage(appTaskId::kMqtt,
                                         appTaskId::kInput,
                                         appMessageType::kMqttPublishOnlineRequest,
                                         &statusDetail,
                                         300);
  if (!sendResult) {
    appLogError("inputTask: requestButtonStatusNotice failed. sourceDescription=%s",
                (sourceDescription == nullptr ? "(null)" : sourceDescription));
    return false;
  }
  appLogInfo("inputTask: requestButtonStatusNotice queued. sub=%s detail=button source=%s",
             iotCommon::mqtt::subCommand::status::kButton,
             (sourceDescription == nullptr ? "(null)" : sourceDescription));
  return true;
}

/**
 * @brief 短押しイベントを処理する。
 */
void handleShortPressEvent() {
  ledController::indicateButtonShortPressPattern();
  requestButtonStatusNotice("button short press");
}

/**
 * @brief 長押しイベントを処理する。
 * @param currentPressDurationMs 押下継続時間(ms)。
 * @param context 入力状態。
 */
void handleLongPressEvent(uint32_t currentPressDurationMs, buttonStateContext* context) {
  if (context == nullptr) {
    appLogError("inputTask: handleLongPressEvent failed. context is null. currentPressDurationMs=%lu",
                static_cast<unsigned long>(currentPressDurationMs));
    return;
  }

  uint32_t uptimeMs = millis();
  if (uptimeMs <= startupMaintenanceWindowMs) {
    context->isMaintenanceMode = true;
    ledController::indicateMaintenanceModeRedOn();
    appLogWarn("inputTask: maintenance mode entered by startup long press. pressDurationMs=%lu uptimeMs=%lu",
               static_cast<unsigned long>(currentPressDurationMs),
               static_cast<unsigned long>(uptimeMs));
    return;
  }

  appLogWarn("inputTask: long press detected. reboot sequence start. pressDurationMs=%lu uptimeMs=%lu",
             static_cast<unsigned long>(currentPressDurationMs),
             static_cast<unsigned long>(uptimeMs));
  ledController::indicateButtonLongPressRebootPattern();
  esp_restart();
}

/**
 * @brief 押下イベント（離した瞬間）を判定して処理する。
 * @param pressDurationMs 押下継続時間(ms)。
 * @param context 入力状態。
 */
void handlePressReleaseEvent(uint32_t pressDurationMs, buttonStateContext* context) {
  if (context == nullptr) {
    appLogError("inputTask: handlePressReleaseEvent failed. context is null. pressDurationMs=%lu",
                static_cast<unsigned long>(pressDurationMs));
    return;
  }
  if (context->longPressHandledDuringCurrentPress) {
    appLogInfo("inputTask: release ignored because long press was already handled. pressDurationMs=%lu",
               static_cast<unsigned long>(pressDurationMs));
    return;
  }
  if (context->isMaintenanceMode) {
    appLogInfo("inputTask: button release ignored in maintenance mode. pressDurationMs=%lu",
               static_cast<unsigned long>(pressDurationMs));
    return;
  }
  if (pressDurationMs <= shortPressMaxDurationMs) {
    handleShortPressEvent();
    return;
  }
  if (pressDurationMs >= longPressMinDurationMs) {
    handleLongPressEvent(pressDurationMs, context);
    return;
  }
  appLogInfo("inputTask: middle press ignored. pressDurationMs=%lu", static_cast<unsigned long>(pressDurationMs));
}
}

bool inputTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kInput, 8);

  if (inputTaskStackBuffer == nullptr) {
    inputTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (inputTaskStackBuffer == nullptr) {
    appLogWarn("inputTask: PSRAM stack allocation failed. fallback to internal RAM.");
    inputTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (inputTaskStackBuffer == nullptr) {
    appLogError("inputTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "inputTask",
      taskStackSize,
      this,
      taskPriority,
      inputTaskStackBuffer,
      &inputTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("inputTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("inputTask created.");
  return true;
}

void inputTask::taskEntry(void* taskParameter) {
  inputTask* self = static_cast<inputTask*>(taskParameter);
  self->runLoop();
}

void inputTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  initializeButtonGpio();
  buttonStateContext buttonContext{};
  appLogInfo("inputTask loop started. gpio=%u intervalMs=%lu debounceCount=%u",
             static_cast<unsigned>(buttonInputGpio),
             static_cast<unsigned long>(inputScanIntervalMs),
             static_cast<unsigned>(debounceConfirmCount));
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kInput, &receivedMessage, 0);
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kInput;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "inputTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }

    bool currentRawPressed = (digitalRead(buttonInputGpio) == buttonPressedLevel);
    if (currentRawPressed == buttonContext.rawPressed) {
      if (buttonContext.consecutiveCount < debounceConfirmCount) {
        ++buttonContext.consecutiveCount;
      }
    } else {
      buttonContext.rawPressed = currentRawPressed;
      buttonContext.consecutiveCount = 1;
    }

    if (buttonContext.consecutiveCount >= debounceConfirmCount &&
        buttonContext.stablePressed != buttonContext.rawPressed) {
      buttonContext.stablePressed = buttonContext.rawPressed;
      if (buttonContext.stablePressed) {
        buttonContext.isPressTimingActive = true;
        buttonContext.longPressHandledDuringCurrentPress = false;
        buttonContext.stablePressStartMs = millis();
        appLogInfo("inputTask: button press confirmed. gpio=%u", static_cast<unsigned>(buttonInputGpio));
      } else if (buttonContext.isPressTimingActive) {
        uint32_t pressDurationMs = millis() - buttonContext.stablePressStartMs;
        buttonContext.isPressTimingActive = false;
        appLogInfo("inputTask: button release confirmed. pressDurationMs=%lu",
                   static_cast<unsigned long>(pressDurationMs));
        handlePressReleaseEvent(pressDurationMs, &buttonContext);
        buttonContext.longPressHandledDuringCurrentPress = false;
      }
    }

    // [重要] 長押しは「押下中」に判定時間へ到達した瞬間に即処理する。
    if (buttonContext.stablePressed &&
        buttonContext.isPressTimingActive &&
        !buttonContext.longPressHandledDuringCurrentPress) {
      uint32_t pressingDurationMs = millis() - buttonContext.stablePressStartMs;
      if (pressingDurationMs >= longPressMinDurationMs) {
        buttonContext.longPressHandledDuringCurrentPress = true;
        appLogInfo("inputTask: long press threshold reached while pressing. pressDurationMs=%lu",
                   static_cast<unsigned long>(pressingDurationMs));
        handleLongPressEvent(pressingDurationMs, &buttonContext);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(inputScanIntervalMs));
  }
}
