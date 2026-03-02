/**
 * @file i2c.cpp
 * @brief I2Cアクセス直列化サービス実装。
 * @details
 * - [重要] I2Cアクセスは専用タスクでのみ実行し、同時送信を禁止する。
 * - [推奨] LCDアドレスや初期化条件は本ファイル先頭定数で管理する。
 */

#include "i2c.h"

#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <string.h>

#include "log.h"

namespace {
/** @brief LCDで優先的に試験するI2Cアドレス。@type uint8_t */
constexpr uint8_t lcdAddressCandidate1 = 0x27;
/** @brief LCDの代替I2Cアドレス。@type uint8_t */
constexpr uint8_t lcdAddressCandidate2 = 0x3F;
/** @brief LCD列数。@type uint8_t */
constexpr uint8_t lcdColumnCount = 16;
/** @brief LCD行数。@type uint8_t */
constexpr uint8_t lcdRowCount = 2;
/** @brief 表示要求キュー長。@type uint32_t */
constexpr uint32_t requestQueueLength = 8;
/** @brief ESP32側SDAピン番号。@type uint8_t */
constexpr uint8_t i2cSdaPin = 8;
/** @brief ESP32側SCLピン番号。@type uint8_t */
constexpr uint8_t i2cSclPin = 9;

/** @brief i2cTaskの静的スタック領域。 */
StackType_t* i2cTaskStackBuffer = nullptr;
/** @brief i2cTaskの静的制御ブロック。 */
StaticTask_t i2cTaskControlBlock;
/** @brief I2C表示要求を受けるFreeRTOSキュー。 */
QueueHandle_t i2cRequestQueue = nullptr;
/** @brief hd44780 I2C LCDドライバインスタンス。 */
hd44780_I2Cexp i2cLcd;
/** @brief I2Cバス初期化済みフラグ。 */
bool isI2cInitialized = false;
/** @brief LCD初期化済みフラグ。 */
bool isLcdInitialized = false;
/** @brief 検出済みLCDアドレス。 */
uint8_t detectedLcdAddress = 0;

/**
 * @brief I2Cバスを初期化する。
 * @return 初期化成功時true、失敗時false。
 */
bool initializeI2cBus() {
  if (isI2cInitialized) {
    return true;
  }

  Wire.begin(i2cSdaPin, i2cSclPin);
  Wire.setTimeOut(100);
  isI2cInitialized = true;
  appLogInfo("initializeI2cBus success. sda=%u scl=%u",
             static_cast<unsigned>(i2cSdaPin),
             static_cast<unsigned>(i2cSclPin));

  appLogInfo("I2C scan start.");
  bool foundAnyAddress = false;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t transmissionResult = static_cast<uint8_t>(Wire.endTransmission());
    if (transmissionResult == 0) {
      foundAnyAddress = true;
      appLogInfo("I2C scan found. address=0x%02X", static_cast<unsigned>(address));
    }
  }
  if (!foundAnyAddress) {
    appLogWarn("I2C scan found no address in range 0x01-0x7E.");
  }
  appLogInfo("I2C scan end.");
  return true;
}

/**
 * @brief 指定I2Cアドレスの応答有無を確認する。
 * @param address 確認対象アドレス。
 * @return 応答ありでtrue、なしでfalse。
 */
bool isI2cAddressResponding(uint8_t address) {
  Wire.beginTransmission(address);
  uint8_t endResult = static_cast<uint8_t>(Wire.endTransmission());
  return (endResult == 0);
}

/**
 * @brief 指定I2CアドレスのendTransmission結果を取得する。
 * @param address 確認対象アドレス。
 * @return endTransmission戻り値。
 */
uint8_t getI2cAddressTestResult(uint8_t address) {
  Wire.beginTransmission(address);
  return static_cast<uint8_t>(Wire.endTransmission());
}

/**
 * @brief LCDのI2Cアドレスを検出する。
 * @param detectedAddressOut 検出アドレス出力。
 * @return 検出成功時true、失敗時false。
 */
bool detectLcdAddress(uint8_t* detectedAddressOut) {
  if (detectedAddressOut == nullptr) {
    appLogError("detectLcdAddress failed. detectedAddressOut is null.");
    return false;
  }

  // [重要] Freenove資料準拠: 0x27を優先し、応答がなければ0x3Fを試す。
  uint8_t testResult27 = getI2cAddressTestResult(lcdAddressCandidate1);
  appLogInfo("detectLcdAddress test. address=0x%02X result=%u",
             static_cast<unsigned>(lcdAddressCandidate1),
             static_cast<unsigned>(testResult27));
  if (testResult27 == 0) {
    *detectedAddressOut = lcdAddressCandidate1;
    appLogInfo("detectLcdAddress success. address=0x%02X (preferred)", static_cast<unsigned>(*detectedAddressOut));
    return true;
  }
  uint8_t testResult3f = getI2cAddressTestResult(lcdAddressCandidate2);
  appLogInfo("detectLcdAddress test. address=0x%02X result=%u",
             static_cast<unsigned>(lcdAddressCandidate2),
             static_cast<unsigned>(testResult3f));
  if (testResult3f == 0) {
    *detectedAddressOut = lcdAddressCandidate2;
    appLogInfo("detectLcdAddress success. address=0x%02X (fallback)", static_cast<unsigned>(*detectedAddressOut));
    return true;
  }

  appLogError("detectLcdAddress failed. tried=0x%02X,0x%02X",
              static_cast<unsigned>(lcdAddressCandidate1),
              static_cast<unsigned>(lcdAddressCandidate2));
  return false;
}

/**
 * @brief LCDを初期化する。
 * @return 初期化成功時true、失敗時false。
 */
bool initializeLcdDevice() {
  if (isLcdInitialized) {
    return true;
  }
  if (!initializeI2cBus()) {
    appLogError("initializeLcdDevice failed. initializeI2cBus returned false.");
    return false;
  }

  uint8_t lcdAddress = 0;
  if (!detectLcdAddress(&lcdAddress)) {
    return false;
  }
  detectedLcdAddress = lcdAddress;

  int beginResult = i2cLcd.begin(lcdColumnCount, lcdRowCount);
  if (beginResult != 0) {
    appLogError("initializeLcdDevice failed. i2cLcd.begin error=%d address=0x%02X",
                beginResult,
                static_cast<unsigned>(detectedLcdAddress));
    return false;
  }

  i2cLcd.backlight();
  i2cLcd.clear();
  i2cLcd.home();
  i2cLcd.display();
  isLcdInitialized = true;
  appLogInfo("initializeLcdDevice success. address=0x%02X cols=%u rows=%u",
             static_cast<unsigned>(detectedLcdAddress),
             static_cast<unsigned>(lcdColumnCount),
             static_cast<unsigned>(lcdRowCount));
  return true;
}

/**
 * @brief LCDへ2行文字列を出力する。
 * @param request 表示要求データ。
 * @return 表示成功時true、失敗時false。
 */
bool renderLcdText(const i2cDisplayRequest& request) {
  appLogInfo("renderLcdText start. line1=%s line2=%s holdMs=%lu",
             request.line1,
             request.line2,
             static_cast<unsigned long>(request.holdMs));
  if (!initializeLcdDevice()) {
    appLogError("renderLcdText failed. initializeLcdDevice returned false.");
    return false;
  }

  i2cLcd.clear();
  i2cLcd.setCursor(0, 0);
  i2cLcd.print(request.line1);
  i2cLcd.setCursor(0, 1);
  i2cLcd.print(request.line2);
  appLogInfo("renderLcdText success. line1=%s line2=%s holdMs=%lu",
             request.line1,
             request.line2,
             static_cast<unsigned long>(request.holdMs));
  return true;
}
}  // namespace

bool i2cService::startTask() {
  if (i2cRequestQueue == nullptr) {
    i2cRequestQueue = xQueueCreate(requestQueueLength, sizeof(i2cDisplayRequest));
  }
  if (i2cRequestQueue == nullptr) {
    appLogError("i2cService::startTask failed. xQueueCreate returned null.");
    return false;
  }

  if (i2cTaskStackBuffer == nullptr) {
    i2cTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (i2cTaskStackBuffer == nullptr) {
    appLogWarn("i2cService: PSRAM stack allocation failed. fallback to internal RAM.");
    i2cTaskStackBuffer = static_cast<StackType_t*>(
        heap_caps_malloc(taskStackSize * sizeof(StackType_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (i2cTaskStackBuffer == nullptr) {
    appLogError("i2cService::startTask failed. heap_caps_malloc stack failed.");
    return false;
  }

  TaskHandle_t createdTaskHandle = xTaskCreateStaticPinnedToCore(
      taskEntry,
      "i2cTask",
      taskStackSize,
      this,
      taskPriority,
      i2cTaskStackBuffer,
      &i2cTaskControlBlock,
      tskNO_AFFINITY);
  if (createdTaskHandle == nullptr) {
    appLogError("i2cService::startTask failed. xTaskCreateStaticPinnedToCore returned null.");
    return false;
  }
  appLogInfo("i2cService task created.");
  return true;
}

/**
 * @brief LCD表示要求をキューへ積む。
 * @param line1 1行目文字列。
 * @param line2 2行目文字列。
 * @param holdMs 表示保持時間(ms)。
 * @return キュー投入成功時true、失敗時false。
 */
bool i2cService::requestLcdText(const char* line1, const char* line2, uint32_t holdMs) {
  if (i2cRequestQueue == nullptr) {
    appLogError("requestLcdText failed. queue is null. call startTask first.");
    return false;
  }

  i2cDisplayRequest request{};
  if (line1 != nullptr) {
    strncpy(request.line1, line1, sizeof(request.line1) - 1);
  }
  request.line1[sizeof(request.line1) - 1] = '\0';
  if (line2 != nullptr) {
    strncpy(request.line2, line2, sizeof(request.line2) - 1);
  }
  request.line2[sizeof(request.line2) - 1] = '\0';
  request.holdMs = holdMs;

  BaseType_t sendResult = xQueueSend(i2cRequestQueue, &request, pdMS_TO_TICKS(200));
  if (sendResult != pdTRUE) {
    appLogError("requestLcdText failed. xQueueSend timeout. line1=%s line2=%s holdMs=%lu",
                request.line1,
                request.line2,
                static_cast<unsigned long>(request.holdMs));
    return false;
  }
  appLogInfo("requestLcdText queued. line1=%s line2=%s holdMs=%lu",
             request.line1,
             request.line2,
             static_cast<unsigned long>(request.holdMs));
  return true;
}

/**
 * @brief FreeRTOSタスクエントリ。
 * @param taskParameter thisポインタ。
 */
void i2cService::taskEntry(void* taskParameter) {
  i2cService* self = static_cast<i2cService*>(taskParameter);
  self->runLoop();
}

/**
 * @brief I2C表示要求を処理する常駐ループ。
 * @details
 * - [重要] I2Cアクセスは必ず本ループ内で実行する。
 * - [推奨] 将来のI2Cデバイス追加時も同じ直列化方針を維持する。
 */
void i2cService::runLoop() {
  appLogInfo("i2cService loop started.");
  for (;;) {
    i2cDisplayRequest request{};
    BaseType_t receiveResult = xQueueReceive(i2cRequestQueue, &request, pdMS_TO_TICKS(100));
    if (receiveResult == pdTRUE) {
      appLogInfo("i2cService dequeued request. line1=%s line2=%s holdMs=%lu",
                 request.line1,
                 request.line2,
                 static_cast<unsigned long>(request.holdMs));
      bool renderResult = renderLcdText(request);
      if (!renderResult) {
        appLogError("i2cService render failed. line1=%s line2=%s", request.line1, request.line2);
      }
      if (request.holdMs > 0) {
        vTaskDelay(pdMS_TO_TICKS(request.holdMs));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
