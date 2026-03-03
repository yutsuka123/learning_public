/**
 * @file util.cpp
 * @brief 汎用ユーティリティ関数の実装。
 * @details
 * - [重要] 文字列/数値処理に加えて、タスク間通信の共通送受信関数を提供する。
 * - [厳守] 他タスクフラグの直接更新は例外用途に限定し、通常はメッセージ要求を優先する。
 */

#include "util.h"

#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "log.h"

namespace {

uint8_t hexCharToNibble(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<uint8_t>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<uint8_t>(value - 'A' + 10);
  }
  return 0xFF;
}

int32_t taskIdToIndex(appTaskId taskId) {
  int32_t rawValue = static_cast<int32_t>(taskId);
  if (rawValue < 0 || rawValue >= appDefine::kTaskSlotCount) {
    return -1;
  }
  return rawValue;
}

int32_t flagNameToIndex(appTaskFlagName flagName) {
  int32_t rawValue = static_cast<int32_t>(flagName);
  if (rawValue < 0 || rawValue >= appDefine::kTaskFlagSlotCount) {
    return -1;
  }
  return rawValue;
}

void copyTextField(char* destination, size_t destinationSize, const char* source) {
  if (destination == nullptr || destinationSize == 0) {
    return;
  }
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

bool textFieldEquals(const char* expected, const char* actual) {
  if (expected == nullptr) {
    return true;
  }
  if (actual == nullptr) {
    return false;
  }
  return (strcmp(expected, actual) == 0);
}

bool messageDetailMatches(const appTaskMessage& message, const appUtil::appTaskMessageDetail* waitDetail) {
  if (waitDetail == nullptr) {
    return true;
  }
  if (waitDetail->hasIntValue && message.intValue != waitDetail->intValue) {
    return false;
  }
  if (waitDetail->hasIntValue2 && message.intValue2 != waitDetail->intValue2) {
    return false;
  }
  if (waitDetail->hasBoolValue && message.boolValue != waitDetail->boolValue) {
    return false;
  }
  if (!textFieldEquals(waitDetail->text, message.text)) {
    return false;
  }
  if (!textFieldEquals(waitDetail->text2, message.text2)) {
    return false;
  }
  if (!textFieldEquals(waitDetail->text3, message.text3)) {
    return false;
  }
  if (!textFieldEquals(waitDetail->text4, message.text4)) {
    return false;
  }
  return true;
}

/** @brief タスク別フラグ管理テーブル。 */
bool taskFlagTable[appDefine::kTaskSlotCount][appDefine::kTaskFlagSlotCount] = {};
/** @brief フラグテーブル排他制御用スピンロック。 */
portMUX_TYPE taskFlagLock = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

namespace appUtil {

String createPublicIdFromBaseMac(const uint8_t baseMac[6]) {
  uint8_t hashBuffer[32] = {};
  mbedtls_sha256(baseMac, 6, hashBuffer, 0);

  // 先頭8バイトを16進化して16文字IDとして返す。
  char outputBuffer[17] = {};
  for (uint32_t index = 0; index < 8; ++index) {
    snprintf(&outputBuffer[index * 2], 3, "%02x", hashBuffer[index]);
  }
  outputBuffer[16] = '\0';
  return String(outputBuffer);
}

bool parseBaseMacHex(const String& baseMacHex, uint8_t outBaseMac[6]) {
  String normalized = baseMacHex;
  normalized.replace(":", "");
  normalized.replace("-", "");

  if (normalized.length() != 12) {
    return false;
  }

  for (uint32_t index = 0; index < 6; ++index) {
    const uint8_t high = hexCharToNibble(normalized[index * 2]);
    const uint8_t low = hexCharToNibble(normalized[index * 2 + 1]);
    if (high == 0xFF || low == 0xFF) {
      return false;
    }
    outBaseMac[index] = static_cast<uint8_t>((high << 4U) | low);
  }

  return true;
}

uint32_t secondsToMilliseconds(uint32_t seconds) {
  constexpr uint32_t kMaxSafeSeconds = UINT32_MAX / 1000U;
  if (seconds > kMaxSafeSeconds) {
    return UINT32_MAX;
  }
  return seconds * 1000U;
}

appTaskMessageDetail createEmptyMessageDetail() {
  appTaskMessageDetail detail{};
  detail.hasIntValue = false;
  detail.intValue = 0;
  detail.hasIntValue2 = false;
  detail.intValue2 = 0;
  detail.hasBoolValue = false;
  detail.boolValue = false;
  detail.text = nullptr;
  detail.text2 = nullptr;
  detail.text3 = nullptr;
  detail.text4 = nullptr;
  return detail;
}

bool sendMessage(appTaskId destinationTaskId,
                 appTaskId sourceTaskId,
                 appMessageType requestType,
                 const appTaskMessageDetail* requestDetail,
                 int32_t timeoutMs) {
  appTaskMessage requestMessage{};
  requestMessage.sourceTaskId = sourceTaskId;
  requestMessage.destinationTaskId = destinationTaskId;
  requestMessage.messageType = requestType;

  if (requestDetail != nullptr) {
    if (requestDetail->hasIntValue) {
      requestMessage.intValue = requestDetail->intValue;
    }
    if (requestDetail->hasIntValue2) {
      requestMessage.intValue2 = requestDetail->intValue2;
    }
    if (requestDetail->hasBoolValue) {
      requestMessage.boolValue = requestDetail->boolValue;
    }
    copyTextField(requestMessage.text, sizeof(requestMessage.text), requestDetail->text);
    copyTextField(requestMessage.text2, sizeof(requestMessage.text2), requestDetail->text2);
    copyTextField(requestMessage.text3, sizeof(requestMessage.text3), requestDetail->text3);
    copyTextField(requestMessage.text4, sizeof(requestMessage.text4), requestDetail->text4);
  }

  TickType_t timeoutTicks = pdMS_TO_TICKS((timeoutMs <= 0) ? 0 : timeoutMs);
  bool sendResult = getInterTaskMessageService().sendMessage(requestMessage, timeoutTicks);
  if (!sendResult) {
    appLogError("appUtil::sendMessage failed. destinationTaskId=%ld sourceTaskId=%ld requestType=%ld timeoutMs=%ld",
                static_cast<long>(destinationTaskId),
                static_cast<long>(sourceTaskId),
                static_cast<long>(requestType),
                static_cast<long>(timeoutMs));
    return false;
  }
  return true;
}

bool waitMessage(appTaskId expectedSourceTaskId,
                 appTaskId selfTaskId,
                 appMessageType waitType,
                 const appTaskMessageDetail* waitDetail,
                 int32_t waitTimeMs,
                 appTaskMessage* receivedMessageOut) {
  if (receivedMessageOut == nullptr) {
    appLogError("appUtil::waitMessage failed. receivedMessageOut is null. expectedSourceTaskId=%ld selfTaskId=%ld waitType=%ld waitTimeMs=%ld",
                static_cast<long>(expectedSourceTaskId),
                static_cast<long>(selfTaskId),
                static_cast<long>(waitType),
                static_cast<long>(waitTimeMs));
    return false;
  }

  if (waitTimeMs == 0) {
    appTaskMessage polledMessage{};
    bool receiveResult = getInterTaskMessageService().receiveMessage(selfTaskId, &polledMessage, 0);
    if (!receiveResult) {
      return false;
    }
    if (polledMessage.sourceTaskId == expectedSourceTaskId &&
        polledMessage.messageType == waitType &&
        messageDetailMatches(polledMessage, waitDetail)) {
      *receivedMessageOut = polledMessage;
      return true;
    }
    return false;
  }

  bool isInfiniteWait = (waitTimeMs < 0);
  TickType_t startTick = xTaskGetTickCount();
  TickType_t timeoutTick = isInfiniteWait ? 0 : pdMS_TO_TICKS(waitTimeMs);

  for (;;) {
    appTaskMessage currentMessage{};
    bool receiveResult = getInterTaskMessageService().receiveMessage(selfTaskId, &currentMessage, pdMS_TO_TICKS(100));
    if (receiveResult) {
      if (currentMessage.sourceTaskId == expectedSourceTaskId &&
          currentMessage.messageType == appMessageType::kTaskError) {
        appLogError("appUtil::waitMessage task error. expectedSourceTaskId=%ld selfTaskId=%ld detail=%s",
                    static_cast<long>(expectedSourceTaskId),
                    static_cast<long>(selfTaskId),
                    currentMessage.text);
        return false;
      }
      if (currentMessage.sourceTaskId == expectedSourceTaskId &&
          currentMessage.messageType == waitType &&
          messageDetailMatches(currentMessage, waitDetail)) {
        *receivedMessageOut = currentMessage;
        return true;
      }
    }

    if (!isInfiniteWait && (xTaskGetTickCount() - startTick) >= timeoutTick) {
      appLogError("appUtil::waitMessage timeout. expectedSourceTaskId=%ld selfTaskId=%ld waitType=%ld waitTimeMs=%ld",
                  static_cast<long>(expectedSourceTaskId),
                  static_cast<long>(selfTaskId),
                  static_cast<long>(waitType),
                  static_cast<long>(waitTimeMs));
      return false;
    }
  }
}

bool getFlagOtherTask(appTaskId taskId, appTaskFlagName flagName, bool* flagValueOut) {
  if (flagValueOut == nullptr) {
    appLogError("appUtil::getFlagOtherTask failed. flagValueOut is null. taskId=%ld flagName=%ld",
                static_cast<long>(taskId),
                static_cast<long>(flagName));
    return false;
  }
  int32_t taskIndex = taskIdToIndex(taskId);
  int32_t flagIndex = flagNameToIndex(flagName);
  if (taskIndex < 0 || flagIndex < 0) {
    appLogError("appUtil::getFlagOtherTask failed. invalid argument. taskId=%ld flagName=%ld",
                static_cast<long>(taskId),
                static_cast<long>(flagName));
    return false;
  }

  portENTER_CRITICAL(&taskFlagLock);
  bool currentValue = taskFlagTable[taskIndex][flagIndex];
  portEXIT_CRITICAL(&taskFlagLock);
  *flagValueOut = currentValue;
  return true;
}

bool setFlagOtherTask(appTaskId taskId, appTaskFlagName flagName, bool flagValue) {
  int32_t taskIndex = taskIdToIndex(taskId);
  int32_t flagIndex = flagNameToIndex(flagName);
  if (taskIndex < 0 || flagIndex < 0) {
    appLogError("appUtil::setFlagOtherTask failed. invalid argument. taskId=%ld flagName=%ld flagValue=%d",
                static_cast<long>(taskId),
                static_cast<long>(flagName),
                static_cast<int>(flagValue));
    return false;
  }

  // [新規利用禁止] 通常運用では送信先タスク自身が自分のフラグを更新すること。
  appLogWarn("appUtil::setFlagOtherTask called directly. taskId=%ld flagName=%ld flagValue=%d",
             static_cast<long>(taskId),
             static_cast<long>(flagName),
             static_cast<int>(flagValue));
  portENTER_CRITICAL(&taskFlagLock);
  taskFlagTable[taskIndex][flagIndex] = flagValue;
  portEXIT_CRITICAL(&taskFlagLock);
  return true;
}

}  // namespace appUtil
