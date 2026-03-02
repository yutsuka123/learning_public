/**
 * @file interTaskMessage.cpp
 * @brief FreeRTOS Queueを用いたタスク間メッセージ伝達サービス実装。
 * @details
 * - [重要] 宛先タスク専用Queueに対して送信することで、一般的で分かりやすい配送を行う。
 * - [厳守] Queue未登録のタスクへは送信しない。
 */

#include "interTaskMessage.h"

#include "log.h"

namespace {

/** @brief 管理対象タスクスロット数。appTaskIdの最大値+1で管理する。@type uint8_t */
constexpr uint8_t taskSlotCount = 11;
/** @brief タスクIDを添字として保持するQueueテーブル。 */
QueueHandle_t taskQueueTable[taskSlotCount] = {nullptr};
/** @brief サービス初期化済みフラグ。 */
bool isInitialized = false;

/**
 * @brief タスクIDをQueueテーブルの添字へ変換する。
 * @param taskId タスクID。
 * @return 有効時は0以上の添字、無効時は-1。
 */
int32_t taskIdToIndex(appTaskId taskId) {
  int32_t rawValue = static_cast<int32_t>(taskId);
  if (rawValue <= 0 || rawValue >= taskSlotCount) {
    return -1;
  }
  return rawValue;
}

}  // namespace

bool interTaskMessageService::initialize() {
  if (isInitialized) {
    return true;
  }

  for (uint8_t index = 0; index < taskSlotCount; ++index) {
    taskQueueTable[index] = nullptr;
  }

  isInitialized = true;
  appLogInfo("interTaskMessageService initialized.");
  return true;
}

/**
 * @brief 指定タスクIDへ受信Queueを登録する。
 * @param taskId 登録対象タスクID。
 * @param queueLength Queue長。
 * @return 成功時true、失敗時false。
 */
bool interTaskMessageService::registerTaskQueue(appTaskId taskId, UBaseType_t queueLength) {
  if (!isInitialized) {
    appLogError("interTaskMessageService::registerTaskQueue failed. service not initialized.");
    return false;
  }
  if (queueLength <= 0) {
    appLogError("interTaskMessageService::registerTaskQueue failed. invalid queueLength=%lu", static_cast<unsigned long>(queueLength));
    return false;
  }

  int32_t queueIndex = taskIdToIndex(taskId);
  if (queueIndex < 0) {
    appLogError("interTaskMessageService::registerTaskQueue failed. invalid taskId=%ld", static_cast<long>(taskId));
    return false;
  }
  if (taskQueueTable[queueIndex] != nullptr) {
    appLogWarn("interTaskMessageService::registerTaskQueue skipped. already registered taskId=%ld", static_cast<long>(taskId));
    return true;
  }

  taskQueueTable[queueIndex] = xQueueCreate(queueLength, sizeof(appTaskMessage));
  if (taskQueueTable[queueIndex] == nullptr) {
    appLogError("interTaskMessageService::registerTaskQueue failed. xQueueCreate taskId=%ld, queueLength=%lu",
                static_cast<long>(taskId),
                static_cast<unsigned long>(queueLength));
    return false;
  }

  appLogInfo("interTaskMessageService queue registered. taskId=%ld, queueLength=%lu",
             static_cast<long>(taskId),
             static_cast<unsigned long>(queueLength));
  return true;
}

/**
 * @brief 宛先タスクQueueへメッセージを送信する。
 * @param message 送信メッセージ。
 * @param timeoutTicks 送信待機Tick。
 * @return 成功時true、失敗時false。
 */
bool interTaskMessageService::sendMessage(const appTaskMessage& message, TickType_t timeoutTicks) {
  if (!isInitialized) {
    appLogError("interTaskMessageService::sendMessage failed. service not initialized.");
    return false;
  }

  int32_t destinationIndex = taskIdToIndex(message.destinationTaskId);
  if (destinationIndex < 0) {
    appLogError("interTaskMessageService::sendMessage failed. invalid destinationTaskId=%ld",
                static_cast<long>(message.destinationTaskId));
    return false;
  }
  if (taskQueueTable[destinationIndex] == nullptr) {
    appLogError("interTaskMessageService::sendMessage failed. destination queue not found. destinationTaskId=%ld",
                static_cast<long>(message.destinationTaskId));
    return false;
  }

  BaseType_t sendResult = xQueueSend(taskQueueTable[destinationIndex], &message, timeoutTicks);
  if (sendResult != pdPASS) {
    appLogError("interTaskMessageService::sendMessage failed. queue full or timeout. destinationTaskId=%ld, timeoutTicks=%lu",
                static_cast<long>(message.destinationTaskId),
                static_cast<unsigned long>(timeoutTicks));
    return false;
  }
  return true;
}

/**
 * @brief 指定タスクQueueからメッセージを受信する。
 * @param taskId 受信対象タスクID。
 * @param messageOut 受信メッセージ出力先。
 * @param timeoutTicks 受信待機Tick。
 * @return 受信成功時true、失敗/タイムアウト時false。
 */
bool interTaskMessageService::receiveMessage(appTaskId taskId, appTaskMessage* messageOut, TickType_t timeoutTicks) {
  if (!isInitialized) {
    appLogError("interTaskMessageService::receiveMessage failed. service not initialized.");
    return false;
  }
  if (messageOut == nullptr) {
    appLogError("interTaskMessageService::receiveMessage failed. messageOut is null.");
    return false;
  }

  int32_t queueIndex = taskIdToIndex(taskId);
  if (queueIndex < 0) {
    appLogError("interTaskMessageService::receiveMessage failed. invalid taskId=%ld", static_cast<long>(taskId));
    return false;
  }
  if (taskQueueTable[queueIndex] == nullptr) {
    appLogError("interTaskMessageService::receiveMessage failed. queue not registered. taskId=%ld", static_cast<long>(taskId));
    return false;
  }

  BaseType_t receiveResult = xQueueReceive(taskQueueTable[queueIndex], messageOut, timeoutTicks);
  return (receiveResult == pdPASS);
}

/**
 * @brief プロセス内共通メッセージサービスを返す。
 * @return interTaskMessageServiceのシングルトン参照。
 */
interTaskMessageService& getInterTaskMessageService() {
  static interTaskMessageService messageService;
  return messageService;
}
