/**
 * @file main.cpp
 * @brief IoT ESP32の起動エントリ。mainTaskから各機能タスクを起動するためのひな形。
 * @details 今回はタスク起動箇所をコメント化し、将来の実装着手点のみ用意する。
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "certification.h"
#include "display.h"
#include "externalDevice.h"
#include "filesystem.h"
#include "http.h"
#include "input.h"
#include "led.h"
#include "log.h"
#include "mqtt.h"
#include "ota.h"
#include "tcpip.h"
#include "wifi.h"

namespace {

constexpr uint32_t serialBaudRate = 115200;
constexpr uint32_t mainTaskStackSize = 4096;
constexpr UBaseType_t mainTaskPriority = 1;
constexpr uint32_t mainTaskIntervalMs = 1000;

wifiTask wifiService;
mqttTask mqttService;
httpTask httpService;
tcpipTask tcpipService;
otaTask otaService;
externalDeviceTask externalDeviceService;
displayTask displayService;
ledTask ledService;
inputTask inputService;

certificationService certificationModule;
filesystemService filesystemModule;

/**
 * @brief メインタスク本体。将来ここから各機能タスクを起動する。
 * @param taskParameter タスク引数（未使用）。
 * @return なし（無限ループ）。
 */
void mainTaskEntry(void* taskParameter) {
  (void)taskParameter;

  appLogInfo("mainTask started.");

  // TODO: 以下は将来有効化する。現在は要求どおりコメント化。
  // wifiService.startTask();
  // mqttService.startTask();
  // httpService.startTask();
  // tcpipService.startTask();  // 必要時のみ有効化
  // otaService.startTask();
  // externalDeviceService.startTask();
  // displayService.startTask();
  // ledService.startTask();
  // inputService.startTask();

  for (;;) {
    appLogDebug("mainTask heartbeat.");
    vTaskDelay(pdMS_TO_TICKS(mainTaskIntervalMs));
  }
}

}  // namespace

/**
 * @brief Arduino初期化。mainTaskのみ起動する。
 * @return なし。
 */
void setup() {
  Serial.begin(serialBaudRate);
  delay(200);
  initializeLogLevel();

  certificationModule.initialize();
  filesystemModule.initialize();

  BaseType_t createTaskResult = xTaskCreatePinnedToCore(
      mainTaskEntry,
      "mainTask",
      mainTaskStackSize,
      nullptr,
      mainTaskPriority,
      nullptr,
      ARDUINO_RUNNING_CORE);

  if (createTaskResult != pdPASS) {
    appLogFatal("setup failed. xTaskCreatePinnedToCore(mainTask) failed.");
    return;
  }

  appLogInfo("setup completed. mainTask launched.");
}

/**
 * @brief Arduinoループ。タスク駆動のため待機のみ行う。
 * @return なし。
 */
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
