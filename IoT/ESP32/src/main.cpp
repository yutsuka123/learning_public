/*
 * ファイル概要:
 *   IoT/ESP32 向けのエントリポイントです。
 *   ESP32-S3 で FreeRTOS タスクを起動し、基本情報を定期出力します。
 *
 * 主な仕様:
 *   - Arduino フレームワーク上で動作
 *   - FreeRTOS タスク（deviceInfoTask）を生成
 *   - ESP_LOGx マクロでチップ情報・ヒープ情報を定期出力
 *   - DEBUG / INFO / WARN / ERROR / FATAL をマクロ指定で利用
 *   - オンボードRGB LED（GPIO38/39/40）を1秒間隔で色変更
 *
 * 制限事項:
 *   - MQTT/TLS 実装は未接続（第3段階の基盤コードのみ）
 *   - 実運用時は認証情報をソースへ直書きしないこと
 */

#include <Arduino.h>
#include <FastLED.h>
#include <esp_chip_info.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kTaskStackSize = 4096;
constexpr UBaseType_t kTaskPriority = 1;
constexpr uint32_t kLogIntervalMs = 5000;
constexpr uint32_t kLedIntervalMs = 1000;
constexpr const char* kLogTag = "iotMain";
constexpr int32_t kLedCount = 1;
constexpr uint8_t kLedPin = 48;
constexpr uint8_t kLedBrightness = 32;
constexpr EOrder kLedColorOrder = GRB;

CRGB ledBuffer[kLedCount];

/**
 * @brief FNK0099のオンボードNeoPixel(WS2812)へ色を設定する。
 * @param red 赤成分（0-255）。
 * @param green 緑成分（0-255）。
 * @param blue 青成分（0-255）。
 * @return なし。
 */
void setOnboardNeoPixelColor(uint8_t red, uint8_t green, uint8_t blue) {
  for (int32_t ledIndex = 0; ledIndex < kLedCount; ++ledIndex) {
    ledBuffer[ledIndex] = CRGB(red, green, blue);
  }
  FastLED.show();
}

/**
 * @brief ログ出力を統一するためのマクロ群。
 * @details ESP_LOGx をラップし、ログレベルを明示する。
 */
#define APP_LOG_DEBUG(format, ...) ESP_LOGD(kLogTag, "[DEBUG] " format, ##__VA_ARGS__)
#define APP_LOG_INFO(format, ...) ESP_LOGI(kLogTag, "[INFO ] " format, ##__VA_ARGS__)
#define APP_LOG_WARN(format, ...) ESP_LOGW(kLogTag, "[WARN ] " format, ##__VA_ARGS__)
#define APP_LOG_ERROR(format, ...) ESP_LOGE(kLogTag, "[ERROR] " format, ##__VA_ARGS__)
#define APP_LOG_FATAL(format, ...) ESP_LOGE(kLogTag, "[FATAL] " format, ##__VA_ARGS__)

/**
 * @brief 起動後にLED色を1秒間隔で切り替えるFreeRTOSタスク。
 * @param taskParameter タスク引数（未使用）。
 * @return なし（無限ループタスクのため戻らない）。
 */
void ledPatternTask(void* taskParameter) {
  (void)taskParameter;

  static const uint8_t ledColors[][3] = {
      {255, 0, 0},     // Red
      {0, 255, 0},     // Green
      {0, 0, 255},     // Blue
      {255, 255, 255}, // White
      {0, 0, 0}        // Off
  };
  constexpr size_t kColorCount = sizeof(ledColors) / sizeof(ledColors[0]);
  size_t colorIndex = 0;

  for (;;) {
    const uint8_t red = ledColors[colorIndex][0];
    const uint8_t green = ledColors[colorIndex][1];
    const uint8_t blue = ledColors[colorIndex][2];
    setOnboardNeoPixelColor(red, green, blue);

    APP_LOG_INFO("led color changed. index=%u rgb=(%u,%u,%u)",
                 static_cast<unsigned>(colorIndex),
                 static_cast<unsigned>(red),
                 static_cast<unsigned>(green),
                 static_cast<unsigned>(blue));

    colorIndex = (colorIndex + 1) % kColorCount;
    vTaskDelay(pdMS_TO_TICKS(kLedIntervalMs));
  }
}

/**
 * @brief デバイス情報を定期的に出力するFreeRTOSタスク。
 * @param taskParameter タスク引数（未使用）。
 * @return なし（無限ループタスクのため戻らない）。
 */
void deviceInfoTask(void* taskParameter) {
  (void)taskParameter;

  esp_chip_info_t chipInfo{};
  esp_chip_info(&chipInfo);

  for (;;) {
    APP_LOG_INFO("----- device info begin -----");
    APP_LOG_INFO("idf version: %s", esp_get_idf_version());
    APP_LOG_INFO("chip model=%d cores=%d revision=%d features=0x%08x",
                 chipInfo.model, chipInfo.cores, chipInfo.revision, chipInfo.features);
    APP_LOG_DEBUG("free heap=%u bytes", static_cast<unsigned>(ESP.getFreeHeap()));

    if (ESP.getFreeHeap() < 20000U) {
      APP_LOG_WARN("free heap is low. freeHeap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
    }

    APP_LOG_INFO("----- device info end -----");

    vTaskDelay(pdMS_TO_TICKS(kLogIntervalMs));
  }
}

}  // namespace

/**
 * @brief Arduino初期化関数。FreeRTOSタスクを起動する。
 * @param なし。
 * @return なし。
 */
void setup() {
  Serial.begin(kSerialBaudRate);
  delay(200);
  esp_log_level_set("*", ESP_LOG_DEBUG);
  APP_LOG_INFO("setup started. serialBaudRate=%lu", static_cast<unsigned long>(kSerialBaudRate));

  FastLED.addLeds<WS2812, kLedPin, kLedColorOrder>(ledBuffer, kLedCount);
  FastLED.setBrightness(kLedBrightness);
  FastLED.clear(true);

  APP_LOG_INFO("onboard neopixel initialized. ledCount=%ld pin=%u brightness=%u order=GRB",
               static_cast<long>(kLedCount),
               static_cast<unsigned>(kLedPin),
               static_cast<unsigned>(kLedBrightness));

  BaseType_t createInfoTaskResult = xTaskCreatePinnedToCore(
      deviceInfoTask,
      "deviceInfoTask",
      kTaskStackSize,
      nullptr,
      kTaskPriority,
      nullptr,
      ARDUINO_RUNNING_CORE);

  if (createInfoTaskResult != pdPASS) {
    APP_LOG_FATAL("xTaskCreatePinnedToCore failed. taskName=%s stack=%lu priority=%u core=%d",
                  "deviceInfoTask",
                  static_cast<unsigned long>(kTaskStackSize),
                  static_cast<unsigned>(kTaskPriority),
                  ARDUINO_RUNNING_CORE);
    return;
  }

  BaseType_t createLedTaskResult = xTaskCreatePinnedToCore(
      ledPatternTask,
      "ledPatternTask",
      kTaskStackSize,
      nullptr,
      kTaskPriority,
      nullptr,
      ARDUINO_RUNNING_CORE);

  if (createLedTaskResult != pdPASS) {
    APP_LOG_FATAL("xTaskCreatePinnedToCore failed. taskName=%s stack=%lu priority=%u core=%d",
                  "ledPatternTask",
                  static_cast<unsigned long>(kTaskStackSize),
                  static_cast<unsigned>(kTaskPriority),
                  ARDUINO_RUNNING_CORE);
    return;
  }

  APP_LOG_INFO("setup completed. deviceInfoTask and ledPatternTask launched successfully.");
}

/**
 * @brief Arduinoメインループ。現在は監視待機のみ行う。
 * @param なし。
 * @return なし。
 */
void loop() {
  APP_LOG_DEBUG("loop heartbeat");
  vTaskDelay(pdMS_TO_TICKS(1000));
}
