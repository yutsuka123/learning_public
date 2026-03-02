/**
 * @file jsonService.h
 * @brief MQTT payload(JSON文字列)のキー/値 set/get を提供するサービス定義。
 * @details
 * - [重要] 本サービスは cJSON を使い、`String` の payload を直接更新・参照する。
 * - [推奨] MQTT送受信の直前直後に本サービスを利用し、手書きJSON連結を減らす。
 * - [推奨] keyPath（例: `args.network.wifiSSID`）で入れ子オブジェクトを扱う。
 * - [重要] 基本APIは `const char*` `short` `long` `bool` を対象とする。
 * - [将来対応] doubleや配列オブジェクトの直接マージは用途確定後に拡張する。
 */

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 一括設定で扱う値型。
 */
enum class jsonValueType : uint8_t {
  kString = 1,
  kShort = 2,
  kLong = 3,
  kBool = 4,
};

/**
 * @brief 一括設定用のキー/値データ。
 * @details
 * - valueType に応じた値メンバーを使用する。
 * - 文字列値は stringValue を使用する（null不可）。
 */
struct jsonKeyValueItem {
  const char* keyPath;
  jsonValueType valueType;
  const char* stringValue;
  short shortValue;
  long longValue;
  bool boolValue;
};

/**
 * @brief MQTT payload用JSONサービス。
 */
class jsonService {
 public:
  /**
   * @brief 指定キーへ文字列値を設定する。
   * @param payloadInOut 入出力payload（null不可）。空文字なら新規JSONを作成する。
   * @param key 設定対象キー（null不可）。
   * @param value 設定する文字列値（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool setValueByKey(String* payloadInOut, const char* key, const char* value);

  /**
   * @brief 指定キーへshort値を設定する。
   * @param payloadInOut 入出力payload（null不可）。空文字なら新規JSONを作成する。
   * @param key 設定対象キー（null不可）。
   * @param value 設定するshort値。
   * @return 成功時true、失敗時false。
   */
  bool setValueByKey(String* payloadInOut, const char* key, short value);

  /**
   * @brief 指定キーへlong値を設定する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param key 設定対象キー（null不可）。
   * @param value 設定するlong値。
   * @return 成功時true、失敗時false。
   */
  bool setValueByKey(String* payloadInOut, const char* key, long value);

  /**
   * @brief 指定キーへbool値を設定する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param key 設定対象キー（null不可）。
   * @param value 設定するbool値。
   * @return 成功時true、失敗時false。
   */
  bool setValueByKey(String* payloadInOut, const char* key, bool value);

  /**
   * @brief keyPath（入れ子対応）へ文字列値を設定する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param keyPath 設定対象パス（例: args.network.wifiSSID）。
   * @param value 設定する文字列値（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool setValueByPath(String* payloadInOut, const char* keyPath, const char* value);

  /**
   * @brief keyPath（入れ子対応）へshort値を設定する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param keyPath 設定対象パス。
   * @param value 設定するshort値。
   * @return 成功時true、失敗時false。
   */
  bool setValueByPath(String* payloadInOut, const char* keyPath, short value);

  /**
   * @brief keyPath（入れ子対応）へlong値を設定する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param keyPath 設定対象パス。
   * @param value 設定するlong値。
   * @return 成功時true、失敗時false。
   */
  bool setValueByPath(String* payloadInOut, const char* keyPath, long value);

  /**
   * @brief keyPath（入れ子対応）へbool値を設定する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param keyPath 設定対象パス。
   * @param value 設定するbool値。
   * @return 成功時true、失敗時false。
   */
  bool setValueByPath(String* payloadInOut, const char* keyPath, bool value);

  /**
   * @brief 指定キーの文字列値を取得する。
   * @param payload 入力payload（JSON文字列）。
   * @param key 取得対象キー（null不可）。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getValueByKey(const String& payload, const char* key, String* valueOut);

  /**
   * @brief 指定キーのshort値を取得する。
   * @param payload 入力payload（JSON文字列）。
   * @param key 取得対象キー（null不可）。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getValueByKey(const String& payload, const char* key, short* valueOut);

  /**
   * @brief 指定キーのlong値を取得する。
   * @param payload 入力payload。
   * @param key 取得対象キー（null不可）。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getValueByKey(const String& payload, const char* key, long* valueOut);

  /**
   * @brief 指定キーのbool値を取得する。
   * @param payload 入力payload。
   * @param key 取得対象キー（null不可）。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getValueByKey(const String& payload, const char* key, bool* valueOut);

  /**
   * @brief keyPath（入れ子対応）の文字列値を取得する。
   * @param payload 入力payload。
   * @param keyPath 取得対象パス。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getValueByPath(const String& payload, const char* keyPath, String* valueOut);

  /**
   * @brief keyPath（入れ子対応）のshort値を取得する。
   * @param payload 入力payload。
   * @param keyPath 取得対象パス。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getValueByPath(const String& payload, const char* keyPath, short* valueOut);

  /**
   * @brief keyPath（入れ子対応）のlong値を取得する。
   * @param payload 入力payload。
   * @param keyPath 取得対象パス。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getValueByPath(const String& payload, const char* keyPath, long* valueOut);

  /**
   * @brief keyPath（入れ子対応）のbool値を取得する。
   * @param payload 入力payload。
   * @param keyPath 取得対象パス。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getValueByPath(const String& payload, const char* keyPath, bool* valueOut);

  /**
   * @brief 指定パスに空オブジェクトを生成する（既存時はそのまま）。
   * @param payloadInOut 入出力payload（null不可）。
   * @param objectPath 生成対象パス。
   * @return 成功時true、失敗時false。
   */
  bool createObjectByPath(String* payloadInOut, const char* objectPath);

  /**
   * @brief 指定パスに空配列を生成する（既存時はそのまま）。
   * @param payloadInOut 入出力payload（null不可）。
   * @param arrayPath 生成対象パス。
   * @return 成功時true、失敗時false。
   */
  bool createArrayByPath(String* payloadInOut, const char* arrayPath);

  /**
   * @brief 配列パスへ文字列を追加する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param arrayPath 配列パス。
   * @param value 追加値（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool appendArrayValueByPath(String* payloadInOut, const char* arrayPath, const char* value);

  /**
   * @brief 配列パスへshort値を追加する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param arrayPath 配列パス。
   * @param value 追加値。
   * @return 成功時true、失敗時false。
   */
  bool appendArrayValueByPath(String* payloadInOut, const char* arrayPath, short value);

  /**
   * @brief 配列パスへlong値を追加する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param arrayPath 配列パス。
   * @param value 追加値。
   * @return 成功時true、失敗時false。
   */
  bool appendArrayValueByPath(String* payloadInOut, const char* arrayPath, long value);

  /**
   * @brief 配列パスへbool値を追加する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param arrayPath 配列パス。
   * @param value 追加値。
   * @return 成功時true、失敗時false。
   */
  bool appendArrayValueByPath(String* payloadInOut, const char* arrayPath, bool value);

  /**
   * @brief 配列サイズを取得する。
   * @param payload 入力payload。
   * @param arrayPath 配列パス。
   * @param sizeOut 配列サイズ出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getArraySizeByPath(const String& payload, const char* arrayPath, int32_t* sizeOut);

  /**
   * @brief 配列要素（文字列）を取得する。
   * @param payload 入力payload。
   * @param arrayPath 配列パス。
   * @param index 取得インデックス（0始まり）。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getArrayValueByPath(const String& payload, const char* arrayPath, int32_t index, String* valueOut);

  /**
   * @brief 配列要素（short）を取得する。
   * @param payload 入力payload。
   * @param arrayPath 配列パス。
   * @param index 取得インデックス（0始まり）。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getArrayValueByPath(const String& payload, const char* arrayPath, int32_t index, short* valueOut);

  /**
   * @brief 配列要素（long）を取得する。
   * @param payload 入力payload。
   * @param arrayPath 配列パス。
   * @param index 取得インデックス（0始まり）。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getArrayValueByPath(const String& payload, const char* arrayPath, int32_t index, long* valueOut);

  /**
   * @brief 配列要素（bool）を取得する。
   * @param payload 入力payload。
   * @param arrayPath 配列パス。
   * @param index 取得インデックス（0始まり）。
   * @param valueOut 取得値出力先（null不可）。
   * @return 成功時true、失敗時false。
   */
  bool getArrayValueByPath(const String& payload, const char* arrayPath, int32_t index, bool* valueOut);

  /**
   * @brief keyPath指定で複数値を一括設定する。
   * @param payloadInOut 入出力payload（null不可）。
   * @param itemList 設定項目配列（null不可）。
   * @param itemCount 配列要素数（1以上）。
   * @return すべて成功時true、途中失敗でfalse。
   */
  bool setValuesByPath(String* payloadInOut, const jsonKeyValueItem* itemList, size_t itemCount);
};
