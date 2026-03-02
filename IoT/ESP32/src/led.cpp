/**
 * @file led.cpp
 * @brief LED表示制御の実装。
 * @details
 * - [重要] 本実装はGPIO直叩きでLEDを制御する。
 * - [厳守] 青: GPIO7 / 緑: GPIO6 / 赤: GPIO5 を使用する。
 * - [将来対応] メッセージ連携方式へ移行する場合も、点灯パターン定義は本ファイルを正とする。
 */

#include "led.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <string.h>

#include "interTaskMessage.h"
#include "log.h"

namespace {
/** @brief 青LEDのGPIO番号。@type uint8_t */
constexpr uint8_t blueLedGpio = 7;
/** @brief 緑LEDのGPIO番号。@type uint8_t */
constexpr uint8_t greenLedGpio = 6;
/** @brief 赤LEDのGPIO番号。@type uint8_t */
constexpr uint8_t redLedGpio = 5;

/** @brief ledTask用スタック領域。 */
StackType_t* ledTaskStackBuffer = nullptr;
/** @brief ledTask用制御ブロック。 */
StaticTask_t ledTaskControlBlock;

/** @brief LED排他制御ミューテックス。 */
SemaphoreHandle_t ledMutex = nullptr;
/** @brief GPIO初期化済みフラグ。 */
bool isLedInitialized = false;
/** @brief 緑LED点滅位相（true=点灯）。 */
bool greenBlinkState = false;
/** @brief 緑LED前回トグル時刻(ms)。 */
uint32_t lastGreenToggleMs = 0;
/** @brief 緑LEDが定常点灯状態かどうか。 */
bool isGreenSteadyOn = false;

/**
 * @brief LED制御用ミューテックスを初期化する。
 */
void ensureLedMutexInitialized() {
  if (ledMutex == nullptr) {
    ledMutex = xSemaphoreCreateMutex();
    if (ledMutex == nullptr) {
      appLogError("ensureLedMutexInitialized failed. xSemaphoreCreateMutex returned null.");
    }
  }
}

/**
 * @brief LED GPIOを初期化する。
 */
void ensureLedHardwareInitialized() {
  if (isLedInitialized) {
    return;
  }
  pinMode(blueLedGpio, OUTPUT);
  pinMode(greenLedGpio, OUTPUT);
  pinMode(redLedGpio, OUTPUT);
  digitalWrite(blueLedGpio, LOW);
  digitalWrite(greenLedGpio, LOW);
  digitalWrite(redLedGpio, LOW);
  isLedInitialized = true;
}

/**
 * @brief LED制御ロックを取得する。
 * @return 取得成功時true、失敗時false。
 */
bool lockLedControl(TickType_t timeoutTicks) {
  ensureLedMutexInitialized();
  if (ledMutex == nullptr) {
    return false;
  }
  BaseType_t takeResult = xSemaphoreTake(ledMutex, timeoutTicks);
  if (takeResult != pdTRUE) {
    appLogWarn("lockLedControl failed. timeoutTicks=%lu", static_cast<unsigned long>(timeoutTicks));
    return false;
  }
  return true;
}

/**
 * @brief LED制御ロックを解放する。
 */
void unlockLedControl() {
  if (ledMutex != nullptr) {
    xSemaphoreGive(ledMutex);
  }
}

/**
 * @brief 単色LEDを設定する。
 * @param isOn trueで点灯、falseで消灯。
 */
void setBlueLed(bool isOn) {
  ensureLedHardwareInitialized();
  digitalWrite(blueLedGpio, isOn ? HIGH : LOW);
}

/**
 * @brief 単色LEDを設定する。
 * @param isOn trueで点灯、falseで消灯。
 */
void setGreenLed(bool isOn) {
  ensureLedHardwareInitialized();
  digitalWrite(greenLedGpio, isOn ? HIGH : LOW);
}

/**
 * @brief 単色LEDを設定する。
 * @param isOn trueで点灯、falseで消灯。
 */
void setRedLed(bool isOn) {
  ensureLedHardwareInitialized();
  digitalWrite(redLedGpio, isOn ? HIGH : LOW);
}

/**
 * @brief 全LEDを消灯する。
 */
void setAllLedOff() {
  setBlueLed(false);
  setGreenLed(false);
  setRedLed(false);
}

/**
 * @brief 緑LEDを指定間隔で点滅更新する。
 */
void updateGreenBlinkByInterval(uint32_t intervalMs) {
  ensureLedHardwareInitialized();
  uint32_t currentMs = millis();
  if ((currentMs - lastGreenToggleMs) >= intervalMs) {
    greenBlinkState = !greenBlinkState;
    setGreenLed(greenBlinkState);
    lastGreenToggleMs = currentMs;
  }
}

/**
 * @brief 赤LEDパターンを反復実行する。
 */
void executeRedPattern(uint32_t repeatCount,
                       uint32_t pulseCount,
                       uint32_t onDurationMs,
                       uint32_t offDurationMs,
                       uint32_t restDurationMs) {
  if (!lockLedControl(portMAX_DELAY)) {
    return;
  }
  ensureLedHardwareInitialized();

  for (uint32_t repeatIndex = 0; repeatIndex < repeatCount; ++repeatIndex) {
    for (uint32_t pulseIndex = 0; pulseIndex < pulseCount; ++pulseIndex) {
      setRedLed(true);
      vTaskDelay(pdMS_TO_TICKS(onDurationMs));
      setRedLed(false);
      vTaskDelay(pdMS_TO_TICKS(offDurationMs));
    }
    vTaskDelay(pdMS_TO_TICKS(restDurationMs));
  }
  unlockLedControl();
}
}

void ledController::initializeByMainOnBoot() {
  if (!lockLedControl(portMAX_DELAY)) {
    return;
  }
  setAllLedOff();
  // [重要] 再起動時は最低0.5秒消灯を厳守する。
  vTaskDelay(pdMS_TO_TICKS(500));
  setBlueLed(true);
  unlockLedControl();
}

void ledController::indicateWifiConnecting() {
  if (!lockLedControl(pdMS_TO_TICKS(20))) {
    return;
  }
  isGreenSteadyOn = false;
  updateGreenBlinkByInterval(500);
  unlockLedControl();
}

void ledController::indicateWifiConnected() {
  if (!lockLedControl(portMAX_DELAY)) {
    return;
  }
  isGreenSteadyOn = false;
  setGreenLed(true);
  // [重要] 要件どおり2秒間点灯する。
  vTaskDelay(pdMS_TO_TICKS(2000));
  setGreenLed(false);
  unlockLedControl();
}

void ledController::indicateMqttConnecting() {
  if (!lockLedControl(pdMS_TO_TICKS(20))) {
    return;
  }
  isGreenSteadyOn = false;
  updateGreenBlinkByInterval(200);
  unlockLedControl();
}

void ledController::indicateMqttConnected() {
  if (!lockLedControl(portMAX_DELAY)) {
    return;
  }
  setGreenLed(true);
  isGreenSteadyOn = true;
  unlockLedControl();
}

void ledController::indicateCommunicationActivity() {
  if (!lockLedControl(portMAX_DELAY)) {
    return;
  }

  bool restoreGreenOn = isGreenSteadyOn;
  // [重要] 通信時は一旦消灯し、0.3秒点灯してアクティビティを表現する。
  setGreenLed(false);
  vTaskDelay(pdMS_TO_TICKS(300));
  setGreenLed(true);
  vTaskDelay(pdMS_TO_TICKS(300));
  setGreenLed(restoreGreenOn);
  unlockLedControl();
}

void ledController::indicateRebootPattern() {
  executeRedPattern(3, 1, 300, 0, 1000);
}

void ledController::indicateAbortPattern() {
  executeRedPattern(3, 2, 300, 300, 1000);
}

void ledController::indicateErrorPattern() {
  executeRedPattern(3, 4, 300, 300, 1000);
}

/**
 * @brief LEDタスクを生成し、受信用キューを登録する。
 * @return 生成成功時true、失敗時false。
 */
bool ledTask::startTask() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  messageService.registerTaskQueue(appTaskId::kLed, 8);

  if (ledTaskStackBuffer == nullptr) {
    ledTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (ledTaskStackBuffer == nullptr) {
    appLogWarn("ledTask: PSRAM stack allocation failed. fallback to internal RAM.");
    ledTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (ledTaskStackBuffer == nullptr) {
    appLogError("ledTask creation failed. heap_caps_malloc stack failed. caps=SPIRAM|INTERNAL");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "ledTask",
      taskStackSize,
      this,
      taskPriority,
      ledTaskStackBuffer,
      &ledTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("ledTask creation failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("ledTask created.");
  return true;
}

/**
 * @brief FreeRTOSタスクエントリ。
 * @param taskParameter thisポインタ。
 */
void ledTask::taskEntry(void* taskParameter) {
  ledTask* self = static_cast<ledTask*>(taskParameter);
  self->runLoop();
}

/**
 * @brief LEDタスク常駐ループ。
 * @details
 * - 現在は起動ACKのみ処理する最小実装。
 * - [将来対応] メッセージ駆動のLED制御を追加する。
 */
void ledTask::runLoop() {
  interTaskMessageService& messageService = getInterTaskMessageService();
  appLogInfo("ledTask loop started. (skeleton)");
  for (;;) {
    appTaskMessage receivedMessage{};
    bool receiveResult = messageService.receiveMessage(appTaskId::kLed, &receivedMessage, pdMS_TO_TICKS(50));
    if (receiveResult && receivedMessage.messageType == appMessageType::kStartupRequest) {
      appTaskMessage responseMessage{};
      responseMessage.sourceTaskId = appTaskId::kLed;
      responseMessage.destinationTaskId = appTaskId::kMain;
      responseMessage.messageType = appMessageType::kStartupAck;
      responseMessage.intValue = 1;
      strncpy(responseMessage.text, "ledTask startup ack", sizeof(responseMessage.text) - 1);
      responseMessage.text[sizeof(responseMessage.text) - 1] = '\0';
      messageService.sendMessage(responseMessage, pdMS_TO_TICKS(100));
    }

    // TODO: LED表示パターン制御を実装する。
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
