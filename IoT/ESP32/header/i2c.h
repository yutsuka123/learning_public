/**
 * @file i2c.h
 * @brief I2Cバスアクセスを単一タスクへ集約するサービス定義。
 * @details
 * - [重要] I2Cデバイスを複数接続する前提で、同時アクセス競合を防止する。
 * - [厳守] I2Cデバイス操作は本サービスのキュー経由で実行する。
 * - [将来対応] LCD以外のI2Cデバイス要求（センサー等）を同一キューへ統合する。
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

/**
 * @brief I2C表示要求データ。
 * @details
 * - [制限] 各行は終端文字を含めて17バイト固定（表示は最大16文字）。
 */
struct i2cDisplayRequest {
  /** @brief 1行目表示文字列バッファ。@type char[17] */
  char line1[17];
  /** @brief 2行目表示文字列バッファ。@type char[17] */
  char line2[17];
  /** @brief 表示後の維持時間(ms)。0なら遅延なし。@type uint32_t */
  uint32_t holdMs;
};

/**
 * @brief BME280から取得した環境センサースナップショット。
 * @details
 * - [重要] 単位は `temperatureC`=`摂氏`、`humidityRh`=`相対湿度%`、`pressureHpa`=`hPa` とする。
 * - [厳守] `isValid=false` の場合は数値を利用しない。
 */
struct i2cEnvironmentSnapshot {
  /** @brief 読み取り成功フラグ。@type bool */
  bool isValid;
  /** @brief BME280応答検出フラグ。@type bool */
  bool isSensorDetected;
  /** @brief 検出済みI2Cアドレス。未検出時は0。@type uint8_t */
  uint8_t sensorAddress;
  /** @brief 温度[degC]。@type float */
  float temperatureC;
  /** @brief 湿度[%RH]。@type float */
  float humidityRh;
  /** @brief 気圧[hPa]。@type float */
  float pressureHpa;
};

/**
 * @brief I2Cアクセス直列化サービス。
 */
class i2cService {
 public:
  /**
   * @brief I2C専用タスクを開始する。
   * @return 開始成功時true、失敗時false。
   */
  bool startTask();

  /**
   * @brief LCDへ2行テキスト表示要求を送信する。
   * @param line1 1行目文字列（16文字まで）。
   * @param line2 2行目文字列（16文字まで）。
   * @param holdMs 表示維持時間ms（0の場合は待機なし）。
   * @return キュー投入成功時true、失敗時false。
   */
  bool requestLcdText(const char* line1, const char* line2, uint32_t holdMs);

  /**
   * @brief BME280の温度・湿度・気圧を読み取る。
   * @param snapshotOut 読み取り結果出力先（null不可）。
   * @param timeoutMs 応答待機時間ms。
   * @return 読み取り成功時true、失敗時false。
   * @details
   * - [重要] I2Cアクセスは内部キュー経由でI2C専用タスクへ委譲する。
   * - [厳守] LCD表示と同時に呼ばれても、直接 `Wire` へアクセスしない。
   */
  bool requestEnvironmentSnapshot(i2cEnvironmentSnapshot* snapshotOut, uint32_t timeoutMs);

 private:
  /**
   * @brief FreeRTOSタスクエントリ。
   */
  static void taskEntry(void* taskParameter);

  /**
   * @brief I2C専用ループ処理。
   */
  void runLoop();

  /** @brief I2C専用タスクのスタックサイズ。@type uint32_t */
  static constexpr uint32_t taskStackSize = 4096;
  /** @brief I2C専用タスクの優先度。@type UBaseType_t */
  static constexpr UBaseType_t taskPriority = 2;
};

/**
 * @brief 起動済みのI2Cサービス実体を返す。
 * @return 起動済みインスタンス。未起動時はnullptr。
 * @details
 * - [重要] `MQTT` 等の他タスクは本関数経由で `I2C` サービスへアクセスする。
 */
i2cService* getI2cServiceInstance();
