/**
 * @file ota.h
 * @brief OTA更新機能のタスクひな形。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#include <stdint.h>

/**
 * @brief OTA開始要求の保持モデル。
 * @details
 * - [重要] MQTT受信時にURLや版数を一時保持し、OTAタスクで安全に参照する。
 * - [厳守] 機密値は保持しない。ファームURLとSHA256のみ扱う。
 */
struct otaStartRequestContext {
  String transactionId;
  String firmwareVersion;
  String firmwareUrl;
  String firmwareSha256;
};

/**
 * @brief OTA開始要求を共有バッファへ保存する。
 * @param requestContext 保存するOTA要求。
 * @return 保存成功時true。
 */
bool storePendingOtaStartRequest(const otaStartRequestContext& requestContext);

class otaTask {
 public:
  bool startTask();

 private:
  static void taskEntry(void* taskParameter);
  void runLoop();

  static constexpr uint32_t taskStackSize = 16384;
  static constexpr UBaseType_t taskPriority = 1;
};
