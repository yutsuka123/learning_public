/**
 * @file i2c.cpp
 * @brief I2Cアクセス直列化サービス実装。
 * @details
 * - [重要] I2Cアクセスは専用タスクでのみ実行し、同時送信を禁止する。
 * - [推奨] LCDアドレスや初期化条件は本ファイル先頭定数で管理する。
 */

#include "i2c.h"

#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
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
/** @brief BME280の第1候補I2Cアドレス。@type uint8_t */
constexpr uint8_t bme280AddressCandidate1 = 0x76;
/** @brief BME280の第2候補I2Cアドレス。@type uint8_t */
constexpr uint8_t bme280AddressCandidate2 = 0x77;
/** @brief 表示要求キュー長。@type uint32_t */
constexpr uint32_t requestQueueLength = 8;
/** @brief ESP32側SDAピン番号。@type uint8_t */
constexpr uint8_t i2cSdaPin = 8;
/** @brief ESP32側SCLピン番号。@type uint8_t */
constexpr uint8_t i2cSclPin = 9;
/** @brief BME280読み取り応答待機既定時間(ms)。@type uint32_t */
constexpr uint32_t defaultEnvironmentResponseTimeoutMs = 1500;

/**
 * @brief I2Cキュー要求種別。
 * @details
 * - [重要] LCD表示とBME280読み取りを同じI2C専用タスクへ直列化する。
 */
enum class i2cRequestType : uint8_t {
  kUnknown = 0,
  kDisplayText = 1,
  kReadEnvironment = 2,
};

/**
 * @brief BME280読み取り応答データ。
 */
struct i2cEnvironmentResponse {
  /** @brief 読み取り成功フラグ。@type bool */
  bool success;
  /** @brief 取得スナップショット。@type i2cEnvironmentSnapshot */
  i2cEnvironmentSnapshot snapshot;
};

/**
 * @brief I2Cタスクへ投入する要求データ。
 */
struct i2cTaskRequest {
  /** @brief 要求種別。@type i2cRequestType */
  i2cRequestType requestType;
  /** @brief LCD表示要求。@type i2cDisplayRequest */
  i2cDisplayRequest displayRequest;
  /** @brief 応答返却用Queue。不要時はnullptr。@type QueueHandle_t */
  QueueHandle_t responseQueue;
};

/** @brief i2cTaskの静的スタック領域。 */
StackType_t* i2cTaskStackBuffer = nullptr;
/** @brief i2cTaskの静的制御ブロック。 */
StaticTask_t i2cTaskControlBlock;
/** @brief I2C要求を受けるFreeRTOSキュー。 */
QueueHandle_t i2cRequestQueue = nullptr;
/** @brief hd44780 I2C LCDドライバインスタンス。 */
hd44780_I2Cexp i2cLcd;
/** @brief BME280ドライバインスタンス。 */
Adafruit_BME280 bme280Device;
/** @brief I2Cバス初期化済みフラグ。 */
bool isI2cInitialized = false;
/** @brief LCD初期化済みフラグ。 */
bool isLcdInitialized = false;
/** @brief 検出済みLCDアドレス。 */
uint8_t detectedLcdAddress = 0;
/** @brief BME280初期化済みフラグ。 */
bool isBme280Initialized = false;
/** @brief 検出済みBME280アドレス。 */
uint8_t detectedBme280Address = 0;
/** @brief 起動済みI2Cサービス実体。 */
i2cService* activeI2cServiceInstance = nullptr;

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
 * @brief BME280のI2Cアドレスを検出する。
 * @param detectedAddressOut 検出アドレス出力先。
 * @return 検出成功時true、失敗時false。
 */
bool detectBme280Address(uint8_t* detectedAddressOut) {
  if (detectedAddressOut == nullptr) {
    appLogError("detectBme280Address failed. detectedAddressOut is null.");
    return false;
  }

  uint8_t testResult76 = getI2cAddressTestResult(bme280AddressCandidate1);
  appLogInfo("detectBme280Address test. address=0x%02X result=%u",
             static_cast<unsigned>(bme280AddressCandidate1),
             static_cast<unsigned>(testResult76));
  if (testResult76 == 0) {
    *detectedAddressOut = bme280AddressCandidate1;
    appLogInfo("detectBme280Address success. address=0x%02X", static_cast<unsigned>(*detectedAddressOut));
    return true;
  }

  uint8_t testResult77 = getI2cAddressTestResult(bme280AddressCandidate2);
  appLogInfo("detectBme280Address test. address=0x%02X result=%u",
             static_cast<unsigned>(bme280AddressCandidate2),
             static_cast<unsigned>(testResult77));
  if (testResult77 == 0) {
    *detectedAddressOut = bme280AddressCandidate2;
    appLogInfo("detectBme280Address success. address=0x%02X", static_cast<unsigned>(*detectedAddressOut));
    return true;
  }

  appLogWarn("detectBme280Address failed. tried=0x%02X,0x%02X",
             static_cast<unsigned>(bme280AddressCandidate1),
             static_cast<unsigned>(bme280AddressCandidate2));
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

  // [重要] 一部LCDモジュールではbegin直後のコマンド連続送信で表示が不安定になるため待機する。
  delay(50);
  i2cLcd.backlight();
  delay(10);
  i2cLcd.clear();
  delay(5);
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
 * @brief BME280を初期化する。
 * @return 初期化成功時true、失敗時false。
 * @details
 * - [重要] BME280未接続時はfalseを返し、呼び出し元でMQTT/シリアルへ異常を通知する。
 * - [重要] I2Cモード前提のため、基板の `CSB` は 3.3V 固定を想定する。
 */
bool initializeBme280Device() {
  if (isBme280Initialized) {
    return true;
  }
  if (!initializeI2cBus()) {
    appLogError("initializeBme280Device failed. initializeI2cBus returned false.");
    return false;
  }

  uint8_t bme280Address = 0;
  if (!detectBme280Address(&bme280Address)) {
    appLogWarn("initializeBme280Device skipped. BME280 address not detected.");
    return false;
  }

  detectedBme280Address = bme280Address;
  const bool beginResult = bme280Device.begin(static_cast<uint8_t>(detectedBme280Address), &Wire);
  if (!beginResult) {
    appLogError("initializeBme280Device failed. bme280Device.begin returned false. address=0x%02X",
                static_cast<unsigned>(detectedBme280Address));
    return false;
  }

  // [重要] 5分ごとのオンデマンド取得が主用途のため、forced modeで必要時のみ測定する。
  bme280Device.setSampling(Adafruit_BME280::MODE_FORCED,
                           Adafruit_BME280::SAMPLING_X1,
                           Adafruit_BME280::SAMPLING_X1,
                           Adafruit_BME280::SAMPLING_X1,
                           Adafruit_BME280::FILTER_OFF);
  isBme280Initialized = true;
  appLogInfo("initializeBme280Device success. address=0x%02X",
             static_cast<unsigned>(detectedBme280Address));
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
  delay(2);
  i2cLcd.setCursor(0, 0);
  i2cLcd.print(request.line1);
  i2cLcd.setCursor(0, 1);
  i2cLcd.print(request.line2);
  i2cLcd.display();
  appLogInfo("renderLcdText success. line1=%s line2=%s holdMs=%lu",
             request.line1,
             request.line2,
             static_cast<unsigned long>(request.holdMs));
  return true;
}

/**
 * @brief BME280から環境値を読み取る。
 * @param snapshotOut 読み取り結果出力先。
 * @return 成功時true、失敗時false。
 */
bool readEnvironmentSnapshot(i2cEnvironmentSnapshot* snapshotOut) {
  if (snapshotOut == nullptr) {
    appLogError("readEnvironmentSnapshot failed. snapshotOut is null.");
    return false;
  }

  *snapshotOut = i2cEnvironmentSnapshot{};
  if (!initializeBme280Device()) {
    snapshotOut->isValid = false;
    snapshotOut->isSensorDetected = false;
    snapshotOut->sensorAddress = 0;
    appLogWarn("readEnvironmentSnapshot failed. initializeBme280Device returned false.");
    return false;
  }

  if (!bme280Device.takeForcedMeasurement()) {
    appLogWarn("readEnvironmentSnapshot: takeForcedMeasurement returned false. continue with current sampled values.");
  }

  const float temperatureC = bme280Device.readTemperature();
  const float humidityRh = bme280Device.readHumidity();
  const float pressureHpa = bme280Device.readPressure() / 100.0F;
  if (isnan(temperatureC) || isnan(humidityRh) || isnan(pressureHpa)) {
    appLogError("readEnvironmentSnapshot failed. invalid measurement. temperature=%f humidity=%f pressure=%f",
                static_cast<double>(temperatureC),
                static_cast<double>(humidityRh),
                static_cast<double>(pressureHpa));
    snapshotOut->isValid = false;
    snapshotOut->isSensorDetected = true;
    snapshotOut->sensorAddress = detectedBme280Address;
    return false;
  }

  snapshotOut->isValid = true;
  snapshotOut->isSensorDetected = true;
  snapshotOut->sensorAddress = detectedBme280Address;
  snapshotOut->temperatureC = temperatureC;
  snapshotOut->humidityRh = humidityRh;
  snapshotOut->pressureHpa = pressureHpa;
  appLogInfo("readEnvironmentSnapshot success. address=0x%02X temperature=%.2f humidity=%.2f pressure=%.2f",
             static_cast<unsigned>(snapshotOut->sensorAddress),
             static_cast<double>(snapshotOut->temperatureC),
             static_cast<double>(snapshotOut->humidityRh),
             static_cast<double>(snapshotOut->pressureHpa));
  return true;
}

/**
 * @brief LCD表示文字列を16文字制約に合わせて整形する。
 * @param rawText 元文字列（null可）。
 * @return 16文字以内の整形済み文字列。
 * @details
 * - [重要] 長い語は意味が分かる略語へ変換してから詰める。
 * - [厳守] 16文字を超える場合は末尾を `~` にして省略を明示する。
 */
String normalizeLcdLine(const char* rawText) {
  String normalizedText = (rawText == nullptr) ? "" : String(rawText);

  normalizedText.replace("INITIALIZATION", "INIT");
  normalizedText.replace("INITIALIZING", "INIT");
  normalizedText.replace("SEARCHING", "SRCH");
  normalizedText.replace("CONNECTED", "CONN");
  normalizedText.replace("PUBLISHING", "PUBL");
  normalizedText.replace("PUBLISH", "PUB");
  normalizedText.replace("HEARTBEAT", "HEART");
  normalizedText.replace("SERVER", "SRV");
  normalizedText.replace("FAILED", "FAIL");
  normalizedText.replace("TIMEOUT", "TMOUT");

  while (normalizedText.indexOf("  ") >= 0) {
    normalizedText.replace("  ", " ");
  }
  normalizedText.trim();

  if (normalizedText.length() > 16) {
    normalizedText = normalizedText.substring(0, 15) + "~";
  }
  return normalizedText;
}
}  // namespace

bool i2cService::startTask() {
  if (i2cRequestQueue == nullptr) {
    i2cRequestQueue = xQueueCreate(requestQueueLength, sizeof(i2cTaskRequest));
  }
  if (i2cRequestQueue == nullptr) {
    appLogError("i2cService::startTask failed. xQueueCreate returned null.");
    return false;
  }
  activeI2cServiceInstance = this;

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

  i2cTaskRequest request{};
  request.requestType = i2cRequestType::kDisplayText;
  request.responseQueue = nullptr;
  String normalizedLine1 = normalizeLcdLine(line1);
  String normalizedLine2 = normalizeLcdLine(line2);
  strncpy(request.displayRequest.line1, normalizedLine1.c_str(), sizeof(request.displayRequest.line1) - 1);
  request.displayRequest.line1[sizeof(request.displayRequest.line1) - 1] = '\0';
  strncpy(request.displayRequest.line2, normalizedLine2.c_str(), sizeof(request.displayRequest.line2) - 1);
  request.displayRequest.line2[sizeof(request.displayRequest.line2) - 1] = '\0';
  request.displayRequest.holdMs = holdMs;

  BaseType_t sendResult = xQueueSend(i2cRequestQueue, &request, pdMS_TO_TICKS(200));
  if (sendResult != pdTRUE) {
    appLogError("requestLcdText failed. xQueueSend timeout. line1=%s line2=%s holdMs=%lu",
                request.displayRequest.line1,
                request.displayRequest.line2,
                static_cast<unsigned long>(request.displayRequest.holdMs));
    return false;
  }
  appLogInfo("requestLcdText queued. line1=%s line2=%s holdMs=%lu",
             request.displayRequest.line1,
             request.displayRequest.line2,
             static_cast<unsigned long>(request.displayRequest.holdMs));
  return true;
}

/**
 * @brief BME280読み取り要求をI2C専用タスクへ送信する。
 * @param snapshotOut 読み取り結果出力先。
 * @param timeoutMs 応答待機ms。
 * @return 読み取り成功時true、失敗時false。
 */
bool i2cService::requestEnvironmentSnapshot(i2cEnvironmentSnapshot* snapshotOut, uint32_t timeoutMs) {
  if (snapshotOut == nullptr) {
    appLogError("requestEnvironmentSnapshot failed. snapshotOut is null.");
    return false;
  }
  if (i2cRequestQueue == nullptr) {
    appLogError("requestEnvironmentSnapshot failed. queue is null. call startTask first.");
    return false;
  }

  QueueHandle_t responseQueue = xQueueCreate(1, sizeof(i2cEnvironmentResponse));
  if (responseQueue == nullptr) {
    appLogError("requestEnvironmentSnapshot failed. xQueueCreate responseQueue returned null.");
    return false;
  }

  i2cTaskRequest request{};
  request.requestType = i2cRequestType::kReadEnvironment;
  request.responseQueue = responseQueue;
  const uint32_t effectiveTimeoutMs = (timeoutMs == 0) ? defaultEnvironmentResponseTimeoutMs : timeoutMs;
  BaseType_t sendResult = xQueueSend(i2cRequestQueue, &request, pdMS_TO_TICKS(200));
  if (sendResult != pdTRUE) {
    appLogError("requestEnvironmentSnapshot failed. xQueueSend timeout. timeoutMs=%lu",
                static_cast<unsigned long>(effectiveTimeoutMs));
    vQueueDelete(responseQueue);
    return false;
  }

  i2cEnvironmentResponse response{};
  BaseType_t receiveResult = xQueueReceive(responseQueue, &response, pdMS_TO_TICKS(effectiveTimeoutMs));
  vQueueDelete(responseQueue);
  if (receiveResult != pdTRUE) {
    appLogError("requestEnvironmentSnapshot failed. response timeout. timeoutMs=%lu",
                static_cast<unsigned long>(effectiveTimeoutMs));
    return false;
  }

  *snapshotOut = response.snapshot;
  if (!response.success) {
    appLogWarn("requestEnvironmentSnapshot completed with sensor read failure. detected=%d address=0x%02X",
               response.snapshot.isSensorDetected ? 1 : 0,
               static_cast<unsigned>(response.snapshot.sensorAddress));
    return false;
  }
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
    i2cTaskRequest request{};
    BaseType_t receiveResult = xQueueReceive(i2cRequestQueue, &request, pdMS_TO_TICKS(100));
    if (receiveResult == pdTRUE) {
      if (request.requestType == i2cRequestType::kDisplayText) {
        appLogInfo("i2cService dequeued LCD request. line1=%s line2=%s holdMs=%lu",
                   request.displayRequest.line1,
                   request.displayRequest.line2,
                   static_cast<unsigned long>(request.displayRequest.holdMs));
        bool renderResult = renderLcdText(request.displayRequest);
        if (!renderResult) {
          appLogError("i2cService render failed. line1=%s line2=%s",
                      request.displayRequest.line1,
                      request.displayRequest.line2);
        }
        if (request.displayRequest.holdMs > 0) {
          vTaskDelay(pdMS_TO_TICKS(request.displayRequest.holdMs));
        }
      } else if (request.requestType == i2cRequestType::kReadEnvironment) {
        i2cEnvironmentResponse response{};
        response.success = readEnvironmentSnapshot(&response.snapshot);
        if (request.responseQueue != nullptr) {
          BaseType_t replyResult = xQueueSend(request.responseQueue, &response, pdMS_TO_TICKS(100));
          if (replyResult != pdTRUE) {
            appLogError("i2cService failed to send environment response.");
          }
        } else {
          appLogWarn("i2cService skipped environment response. responseQueue is null.");
        }
      } else {
        appLogWarn("i2cService skipped unknown request. requestType=%u",
                   static_cast<unsigned>(request.requestType));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

i2cService* getI2cServiceInstance() {
  return activeI2cServiceInstance;
}
