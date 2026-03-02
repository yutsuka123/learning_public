/**
 * @file jsonService.cpp
 * @brief MQTT payload(JSON文字列)のキー/値 set/get を提供するサービス実装。
 * @details
 * - [重要] payload は「1つのJSONオブジェクト文字列」であることを前提にする。
 * - [推奨] keyPath形式（例: `args.network.wifiSSID`）で入れ子データを操作する。
 * - [厳守] 失敗時は関数名・キー/パス・入力条件をログ出力する。
 * - [将来対応] 想定payloadは `common.h` のキー定義更新に追従して拡張する。
 */

#include "jsonService.h"

#include <cJSON.h>
#include <limits.h>
#include <string.h>

#include "log.h"

namespace {

constexpr size_t kMaxPathLength = 192;
constexpr size_t kMaxSegmentLength = 64;

/**
 * @brief payload文字列からJSONルートオブジェクトを生成する。
 * @param payload JSON文字列。
 * @param functionName 呼び出し元関数名。
 * @return 生成したJSONオブジェクト。失敗時はnullptr。
 */
cJSON* parseOrCreateRootObject(const String& payload, const char* functionName) {
  if (functionName == nullptr) {
    appLogError("parseOrCreateRootObject failed. functionName is null.");
    return nullptr;
  }

  if (payload.length() <= 0) {
    cJSON* newRootObject = cJSON_CreateObject();
    if (newRootObject == nullptr) {
      appLogError("%s failed. cJSON_CreateObject returned null for empty payload.", functionName);
    }
    return newRootObject;
  }

  cJSON* parsedRootObject = cJSON_Parse(payload.c_str());
  if (parsedRootObject == nullptr || !cJSON_IsObject(parsedRootObject)) {
    appLogError("%s failed. payload parse error. payloadLength=%d", functionName, payload.length());
    cJSON_Delete(parsedRootObject);
    return nullptr;
  }
  return parsedRootObject;
}

/**
 * @brief JSONオブジェクトを文字列化してpayloadへ反映する。
 * @param rootObject 文字列化対象オブジェクト。
 * @param payloadInOut 出力先payload（null不可）。
 * @param functionName 呼び出し元関数名。
 * @return 成功時true、失敗時false。
 */
bool writeRootObjectToPayload(cJSON* rootObject, String* payloadInOut, const char* functionName) {
  if (rootObject == nullptr || payloadInOut == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. rootObject=%p payloadInOut=%p", functionName, rootObject, payloadInOut);
    return false;
  }

  char* serializedText = cJSON_PrintUnformatted(rootObject);
  if (serializedText == nullptr) {
    appLogError("%s failed. cJSON_PrintUnformatted returned null.", functionName);
    return false;
  }

  *payloadInOut = String(serializedText);
  cJSON_free(serializedText);
  return true;
}

/**
 * @brief keyPathを「親パス」と「末尾キー」に分解する。
 * @param keyPath 入力パス（例: args.network.wifiSSID）。
 * @param parentPathOut 親パス出力バッファ。
 * @param parentPathOutSize 親パス出力バッファ長。
 * @param leafKeyOut 末尾キー出力バッファ。
 * @param leafKeyOutSize 末尾キー出力バッファ長。
 * @param functionName 呼び出し元関数名。
 * @return 成功時true、失敗時false。
 */
bool splitKeyPath(const char* keyPath,
                  char* parentPathOut,
                  size_t parentPathOutSize,
                  char* leafKeyOut,
                  size_t leafKeyOutSize,
                  const char* functionName) {
  if (keyPath == nullptr || parentPathOut == nullptr || leafKeyOut == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. keyPath=%p parentPathOut=%p leafKeyOut=%p",
                functionName,
                keyPath,
                parentPathOut,
                leafKeyOut);
    return false;
  }

  size_t keyPathLength = strlen(keyPath);
  if (keyPathLength == 0 || keyPathLength >= kMaxPathLength) {
    appLogError("%s failed. invalid keyPath length. keyPath=%s length=%u",
                functionName,
                (keyPath == nullptr ? "(null)" : keyPath),
                static_cast<unsigned>(keyPathLength));
    return false;
  }

  const char* lastDot = strrchr(keyPath, '.');
  if (lastDot == nullptr) {
    parentPathOut[0] = '\0';
    strncpy(leafKeyOut, keyPath, leafKeyOutSize - 1);
    leafKeyOut[leafKeyOutSize - 1] = '\0';
    return (strlen(leafKeyOut) > 0);
  }

  size_t parentLength = static_cast<size_t>(lastDot - keyPath);
  const char* leafPart = lastDot + 1;
  if (parentLength == 0 || strlen(leafPart) == 0 || parentLength >= parentPathOutSize) {
    appLogError("%s failed. invalid keyPath format. keyPath=%s", functionName, keyPath);
    return false;
  }

  memcpy(parentPathOut, keyPath, parentLength);
  parentPathOut[parentLength] = '\0';
  strncpy(leafKeyOut, leafPart, leafKeyOutSize - 1);
  leafKeyOut[leafKeyOutSize - 1] = '\0';
  return true;
}

/**
 * @brief オブジェクトパスを辿ってオブジェクトを取得する（必要なら生成）。
 * @param rootObject ルートオブジェクト。
 * @param objectPath 対象パス（空文字ならrootObject）。
 * @param createIfMissing 不足ノードを生成するか。
 * @param functionName 呼び出し元関数名。
 * @return 取得したオブジェクト。失敗時nullptr。
 */
cJSON* getObjectByPath(cJSON* rootObject, const char* objectPath, bool createIfMissing, const char* functionName) {
  if (rootObject == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. rootObject=%p", functionName, rootObject);
    return nullptr;
  }
  if (objectPath == nullptr || strlen(objectPath) == 0) {
    return rootObject;
  }

  char pathBuffer[kMaxPathLength];
  strncpy(pathBuffer, objectPath, sizeof(pathBuffer) - 1);
  pathBuffer[sizeof(pathBuffer) - 1] = '\0';

  cJSON* currentObject = rootObject;
  char* segmentStart = pathBuffer;
  while (segmentStart != nullptr && *segmentStart != '\0') {
    char* dotPosition = strchr(segmentStart, '.');
    if (dotPosition != nullptr) {
      *dotPosition = '\0';
    }

    if (strlen(segmentStart) == 0 || strlen(segmentStart) >= kMaxSegmentLength) {
      appLogError("%s failed. invalid segment in objectPath=%s", functionName, objectPath);
      return nullptr;
    }

    cJSON* nextObject = cJSON_GetObjectItemCaseSensitive(currentObject, segmentStart);
    if (nextObject == nullptr) {
      if (!createIfMissing) {
        appLogError("%s failed. path segment not found. objectPath=%s segment=%s", functionName, objectPath, segmentStart);
        return nullptr;
      }
      nextObject = cJSON_AddObjectToObject(currentObject, segmentStart);
      if (nextObject == nullptr) {
        appLogError("%s failed. cJSON_AddObjectToObject failed. objectPath=%s segment=%s", functionName, objectPath, segmentStart);
        return nullptr;
      }
    }

    if (!cJSON_IsObject(nextObject)) {
      appLogError("%s failed. path segment is not object. objectPath=%s segment=%s", functionName, objectPath, segmentStart);
      return nullptr;
    }

    currentObject = nextObject;
    segmentStart = (dotPosition == nullptr) ? nullptr : (dotPosition + 1);
  }

  return currentObject;
}

/**
 * @brief keyPathで対象アイテムを取得する。
 * @param rootObject ルートオブジェクト。
 * @param keyPath キーパス。
 * @param functionName 呼び出し元関数名。
 * @return 対象アイテム。未取得時nullptr。
 */
cJSON* getItemByPath(cJSON* rootObject, const char* keyPath, const char* functionName) {
  char parentPath[kMaxPathLength];
  char leafKey[kMaxSegmentLength];
  if (!splitKeyPath(keyPath, parentPath, sizeof(parentPath), leafKey, sizeof(leafKey), functionName)) {
    return nullptr;
  }

  cJSON* parentObject = getObjectByPath(rootObject, parentPath, false, functionName);
  if (parentObject == nullptr) {
    return nullptr;
  }
  return cJSON_GetObjectItemCaseSensitive(parentObject, leafKey);
}

/**
 * @brief keyPathへ文字列値を設定する内部共通処理。
 */
bool setStringByPathInternal(cJSON* rootObject, const char* keyPath, const char* value, const char* functionName) {
  if (rootObject == nullptr || keyPath == nullptr || value == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. rootObject=%p keyPath=%p value=%p", functionName, rootObject, keyPath, value);
    return false;
  }

  char parentPath[kMaxPathLength];
  char leafKey[kMaxSegmentLength];
  if (!splitKeyPath(keyPath, parentPath, sizeof(parentPath), leafKey, sizeof(leafKey), functionName)) {
    return false;
  }

  cJSON* parentObject = getObjectByPath(rootObject, parentPath, true, functionName);
  if (parentObject == nullptr) {
    return false;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(parentObject, leafKey);
  cJSON* addedItem = cJSON_AddStringToObject(parentObject, leafKey, value);
  if (addedItem == nullptr) {
    appLogError("%s failed. cJSON_AddStringToObject keyPath=%s", functionName, keyPath);
    return false;
  }
  return true;
}

/**
 * @brief keyPathへ数値値を設定する内部共通処理。
 */
bool setNumberByPathInternal(cJSON* rootObject, const char* keyPath, long value, const char* functionName) {
  if (rootObject == nullptr || keyPath == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. rootObject=%p keyPath=%p", functionName, rootObject, keyPath);
    return false;
  }

  char parentPath[kMaxPathLength];
  char leafKey[kMaxSegmentLength];
  if (!splitKeyPath(keyPath, parentPath, sizeof(parentPath), leafKey, sizeof(leafKey), functionName)) {
    return false;
  }

  cJSON* parentObject = getObjectByPath(rootObject, parentPath, true, functionName);
  if (parentObject == nullptr) {
    return false;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(parentObject, leafKey);
  cJSON* addedItem = cJSON_AddNumberToObject(parentObject, leafKey, static_cast<double>(value));
  if (addedItem == nullptr) {
    appLogError("%s failed. cJSON_AddNumberToObject keyPath=%s value=%ld", functionName, keyPath, value);
    return false;
  }
  return true;
}

/**
 * @brief keyPathへbool値を設定する内部共通処理。
 */
bool setBoolByPathInternal(cJSON* rootObject, const char* keyPath, bool value, const char* functionName) {
  if (rootObject == nullptr || keyPath == nullptr || functionName == nullptr) {
    appLogError("%s failed. invalid parameter. rootObject=%p keyPath=%p", functionName, rootObject, keyPath);
    return false;
  }

  char parentPath[kMaxPathLength];
  char leafKey[kMaxSegmentLength];
  if (!splitKeyPath(keyPath, parentPath, sizeof(parentPath), leafKey, sizeof(leafKey), functionName)) {
    return false;
  }

  cJSON* parentObject = getObjectByPath(rootObject, parentPath, true, functionName);
  if (parentObject == nullptr) {
    return false;
  }

  cJSON_DeleteItemFromObjectCaseSensitive(parentObject, leafKey);
  cJSON* addedItem = cJSON_AddBoolToObject(parentObject, leafKey, value);
  if (addedItem == nullptr) {
    appLogError("%s failed. cJSON_AddBoolToObject keyPath=%s value=%d", functionName, keyPath, static_cast<int>(value));
    return false;
  }
  return true;
}

}  // namespace

bool jsonService::setValueByKey(String* payloadInOut, const char* key, const char* value) {
  return setValueByPath(payloadInOut, key, value);
}

bool jsonService::setValueByKey(String* payloadInOut, const char* key, short value) {
  return setValueByPath(payloadInOut, key, value);
}

bool jsonService::setValueByKey(String* payloadInOut, const char* key, long value) {
  return setValueByPath(payloadInOut, key, value);
}

bool jsonService::setValueByKey(String* payloadInOut, const char* key, bool value) {
  return setValueByPath(payloadInOut, key, value);
}

bool jsonService::setValueByPath(String* payloadInOut, const char* keyPath, const char* value) {
  constexpr const char* functionName = "jsonService::setValueByPath(const char*)";
  if (payloadInOut == nullptr || keyPath == nullptr || value == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p keyPath=%p value=%p", functionName, payloadInOut, keyPath, value);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  bool setResult = setStringByPathInternal(rootObject, keyPath, value, functionName);
  if (!setResult) {
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::setValueByPath(String* payloadInOut, const char* keyPath, short value) {
  constexpr const char* functionName = "jsonService::setValueByPath(short)";
  if (payloadInOut == nullptr || keyPath == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p keyPath=%p value=%d", functionName, payloadInOut, keyPath, value);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  bool setResult = setNumberByPathInternal(rootObject, keyPath, static_cast<long>(value), functionName);
  if (!setResult) {
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::setValueByPath(String* payloadInOut, const char* keyPath, long value) {
  constexpr const char* functionName = "jsonService::setValueByPath(long)";
  if (payloadInOut == nullptr || keyPath == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p keyPath=%p value=%ld", functionName, payloadInOut, keyPath, value);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  bool setResult = setNumberByPathInternal(rootObject, keyPath, value, functionName);
  if (!setResult) {
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::setValueByPath(String* payloadInOut, const char* keyPath, bool value) {
  constexpr const char* functionName = "jsonService::setValueByPath(bool)";
  if (payloadInOut == nullptr || keyPath == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p keyPath=%p value=%d", functionName, payloadInOut, keyPath, static_cast<int>(value));
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  bool setResult = setBoolByPathInternal(rootObject, keyPath, value, functionName);
  if (!setResult) {
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::getValueByKey(const String& payload, const char* key, String* valueOut) {
  return getValueByPath(payload, key, valueOut);
}

bool jsonService::getValueByKey(const String& payload, const char* key, short* valueOut) {
  return getValueByPath(payload, key, valueOut);
}

bool jsonService::getValueByKey(const String& payload, const char* key, long* valueOut) {
  return getValueByPath(payload, key, valueOut);
}

bool jsonService::getValueByKey(const String& payload, const char* key, bool* valueOut) {
  return getValueByPath(payload, key, valueOut);
}

bool jsonService::getValueByPath(const String& payload, const char* keyPath, String* valueOut) {
  constexpr const char* functionName = "jsonService::getValueByPath(String*)";
  if (keyPath == nullptr || valueOut == nullptr) {
    appLogError("%s failed. invalid parameter. keyPath=%p valueOut=%p payloadLength=%d", functionName, keyPath, valueOut, payload.length());
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(payload, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  cJSON* foundItem = getItemByPath(rootObject, keyPath, functionName);
  if (!cJSON_IsString(foundItem)) {
    appLogError("%s failed. keyPath not found or type mismatch. keyPath=%s expected=string", functionName, keyPath);
    cJSON_Delete(rootObject);
    return false;
  }

  *valueOut = String(foundItem->valuestring);
  cJSON_Delete(rootObject);
  return true;
}

bool jsonService::getValueByPath(const String& payload, const char* keyPath, short* valueOut) {
  constexpr const char* functionName = "jsonService::getValueByPath(short*)";
  if (keyPath == nullptr || valueOut == nullptr) {
    appLogError("%s failed. invalid parameter. keyPath=%p valueOut=%p payloadLength=%d", functionName, keyPath, valueOut, payload.length());
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(payload, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  cJSON* foundItem = getItemByPath(rootObject, keyPath, functionName);
  if (!cJSON_IsNumber(foundItem)) {
    appLogError("%s failed. keyPath not found or type mismatch. keyPath=%s expected=number", functionName, keyPath);
    cJSON_Delete(rootObject);
    return false;
  }

  long numberValue = static_cast<long>(foundItem->valuedouble);
  if (numberValue < SHRT_MIN || numberValue > SHRT_MAX) {
    appLogError("%s failed. value out of short range. keyPath=%s value=%ld", functionName, keyPath, numberValue);
    cJSON_Delete(rootObject);
    return false;
  }

  *valueOut = static_cast<short>(numberValue);
  cJSON_Delete(rootObject);
  return true;
}

bool jsonService::getValueByPath(const String& payload, const char* keyPath, long* valueOut) {
  constexpr const char* functionName = "jsonService::getValueByPath(long*)";
  if (keyPath == nullptr || valueOut == nullptr) {
    appLogError("%s failed. invalid parameter. keyPath=%p valueOut=%p payloadLength=%d", functionName, keyPath, valueOut, payload.length());
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(payload, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  cJSON* foundItem = getItemByPath(rootObject, keyPath, functionName);
  if (!cJSON_IsNumber(foundItem)) {
    appLogError("%s failed. keyPath not found or type mismatch. keyPath=%s expected=number", functionName, keyPath);
    cJSON_Delete(rootObject);
    return false;
  }

  *valueOut = static_cast<long>(foundItem->valuedouble);
  cJSON_Delete(rootObject);
  return true;
}

bool jsonService::getValueByPath(const String& payload, const char* keyPath, bool* valueOut) {
  constexpr const char* functionName = "jsonService::getValueByPath(bool*)";
  if (keyPath == nullptr || valueOut == nullptr) {
    appLogError("%s failed. invalid parameter. keyPath=%p valueOut=%p payloadLength=%d", functionName, keyPath, valueOut, payload.length());
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(payload, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  cJSON* foundItem = getItemByPath(rootObject, keyPath, functionName);
  if (!cJSON_IsBool(foundItem)) {
    appLogError("%s failed. keyPath not found or type mismatch. keyPath=%s expected=bool", functionName, keyPath);
    cJSON_Delete(rootObject);
    return false;
  }

  *valueOut = cJSON_IsTrue(foundItem);
  cJSON_Delete(rootObject);
  return true;
}

bool jsonService::createObjectByPath(String* payloadInOut, const char* objectPath) {
  constexpr const char* functionName = "jsonService::createObjectByPath";
  if (payloadInOut == nullptr || objectPath == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p objectPath=%p", functionName, payloadInOut, objectPath);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  cJSON* objectNode = getObjectByPath(rootObject, objectPath, true, functionName);
  if (objectNode == nullptr || !cJSON_IsObject(objectNode)) {
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::createArrayByPath(String* payloadInOut, const char* arrayPath) {
  constexpr const char* functionName = "jsonService::createArrayByPath";
  if (payloadInOut == nullptr || arrayPath == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p arrayPath=%p", functionName, payloadInOut, arrayPath);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  char parentPath[kMaxPathLength];
  char leafKey[kMaxSegmentLength];
  if (!splitKeyPath(arrayPath, parentPath, sizeof(parentPath), leafKey, sizeof(leafKey), functionName)) {
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* parentObject = getObjectByPath(rootObject, parentPath, true, functionName);
  if (parentObject == nullptr) {
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* arrayNode = cJSON_GetObjectItemCaseSensitive(parentObject, leafKey);
  if (arrayNode == nullptr) {
    arrayNode = cJSON_AddArrayToObject(parentObject, leafKey);
  }
  if (arrayNode == nullptr || !cJSON_IsArray(arrayNode)) {
    appLogError("%s failed. path is not array. arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::appendArrayValueByPath(String* payloadInOut, const char* arrayPath, const char* value) {
  constexpr const char* functionName = "jsonService::appendArrayValueByPath(const char*)";
  if (payloadInOut == nullptr || arrayPath == nullptr || value == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p arrayPath=%p value=%p", functionName, payloadInOut, arrayPath, value);
    return false;
  }

  if (!createArrayByPath(payloadInOut, arrayPath)) {
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }
  cJSON* arrayNode = getItemByPath(rootObject, arrayPath, functionName);
  if (arrayNode == nullptr || !cJSON_IsArray(arrayNode)) {
    appLogError("%s failed. array not found. arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* newItem = cJSON_CreateString(value);
  if (newItem == nullptr) {
    appLogError("%s failed. cJSON_CreateString arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }
  cJSON_AddItemToArray(arrayNode, newItem);

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::appendArrayValueByPath(String* payloadInOut, const char* arrayPath, short value) {
  return appendArrayValueByPath(payloadInOut, arrayPath, static_cast<long>(value));
}

bool jsonService::appendArrayValueByPath(String* payloadInOut, const char* arrayPath, long value) {
  constexpr const char* functionName = "jsonService::appendArrayValueByPath(long)";
  if (payloadInOut == nullptr || arrayPath == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p arrayPath=%p value=%ld", functionName, payloadInOut, arrayPath, value);
    return false;
  }

  if (!createArrayByPath(payloadInOut, arrayPath)) {
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }
  cJSON* arrayNode = getItemByPath(rootObject, arrayPath, functionName);
  if (arrayNode == nullptr || !cJSON_IsArray(arrayNode)) {
    appLogError("%s failed. array not found. arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* newItem = cJSON_CreateNumber(static_cast<double>(value));
  if (newItem == nullptr) {
    appLogError("%s failed. cJSON_CreateNumber arrayPath=%s value=%ld", functionName, arrayPath, value);
    cJSON_Delete(rootObject);
    return false;
  }
  cJSON_AddItemToArray(arrayNode, newItem);

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::appendArrayValueByPath(String* payloadInOut, const char* arrayPath, bool value) {
  constexpr const char* functionName = "jsonService::appendArrayValueByPath(bool)";
  if (payloadInOut == nullptr || arrayPath == nullptr) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p arrayPath=%p value=%d", functionName, payloadInOut, arrayPath, static_cast<int>(value));
    return false;
  }

  if (!createArrayByPath(payloadInOut, arrayPath)) {
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }
  cJSON* arrayNode = getItemByPath(rootObject, arrayPath, functionName);
  if (arrayNode == nullptr || !cJSON_IsArray(arrayNode)) {
    appLogError("%s failed. array not found. arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* newItem = cJSON_CreateBool(value);
  if (newItem == nullptr) {
    appLogError("%s failed. cJSON_CreateBool arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }
  cJSON_AddItemToArray(arrayNode, newItem);

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}

bool jsonService::getArraySizeByPath(const String& payload, const char* arrayPath, int32_t* sizeOut) {
  constexpr const char* functionName = "jsonService::getArraySizeByPath";
  if (arrayPath == nullptr || sizeOut == nullptr) {
    appLogError("%s failed. invalid parameter. arrayPath=%p sizeOut=%p", functionName, arrayPath, sizeOut);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(payload, functionName);
  if (rootObject == nullptr) {
    return false;
  }
  cJSON* arrayNode = getItemByPath(rootObject, arrayPath, functionName);
  if (arrayNode == nullptr || !cJSON_IsArray(arrayNode)) {
    appLogError("%s failed. path is not array. arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }

  *sizeOut = cJSON_GetArraySize(arrayNode);
  cJSON_Delete(rootObject);
  return true;
}

bool jsonService::getArrayValueByPath(const String& payload, const char* arrayPath, int32_t index, String* valueOut) {
  constexpr const char* functionName = "jsonService::getArrayValueByPath(String*)";
  if (arrayPath == nullptr || valueOut == nullptr || index < 0) {
    appLogError("%s failed. invalid parameter. arrayPath=%p index=%ld valueOut=%p",
                functionName,
                arrayPath,
                static_cast<long>(index),
                valueOut);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(payload, functionName);
  if (rootObject == nullptr) {
    return false;
  }
  cJSON* arrayNode = getItemByPath(rootObject, arrayPath, functionName);
  if (arrayNode == nullptr || !cJSON_IsArray(arrayNode)) {
    appLogError("%s failed. path is not array. arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* itemNode = cJSON_GetArrayItem(arrayNode, index);
  if (!cJSON_IsString(itemNode)) {
    appLogError("%s failed. item type mismatch. arrayPath=%s index=%ld expected=string",
                functionName,
                arrayPath,
                static_cast<long>(index));
    cJSON_Delete(rootObject);
    return false;
  }

  *valueOut = String(itemNode->valuestring);
  cJSON_Delete(rootObject);
  return true;
}

bool jsonService::getArrayValueByPath(const String& payload, const char* arrayPath, int32_t index, short* valueOut) {
  constexpr const char* functionName = "jsonService::getArrayValueByPath(short*)";
  long longValue = 0;
  bool readResult = getArrayValueByPath(payload, arrayPath, index, &longValue);
  if (!readResult) {
    return false;
  }
  if (valueOut == nullptr || longValue < SHRT_MIN || longValue > SHRT_MAX) {
    appLogError("%s failed. value out of short range or null out. arrayPath=%s index=%ld value=%ld valueOut=%p",
                functionName,
                arrayPath,
                static_cast<long>(index),
                longValue,
                valueOut);
    return false;
  }
  *valueOut = static_cast<short>(longValue);
  return true;
}

bool jsonService::getArrayValueByPath(const String& payload, const char* arrayPath, int32_t index, long* valueOut) {
  constexpr const char* functionName = "jsonService::getArrayValueByPath(long*)";
  if (arrayPath == nullptr || valueOut == nullptr || index < 0) {
    appLogError("%s failed. invalid parameter. arrayPath=%p index=%ld valueOut=%p",
                functionName,
                arrayPath,
                static_cast<long>(index),
                valueOut);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(payload, functionName);
  if (rootObject == nullptr) {
    return false;
  }
  cJSON* arrayNode = getItemByPath(rootObject, arrayPath, functionName);
  if (arrayNode == nullptr || !cJSON_IsArray(arrayNode)) {
    appLogError("%s failed. path is not array. arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* itemNode = cJSON_GetArrayItem(arrayNode, index);
  if (!cJSON_IsNumber(itemNode)) {
    appLogError("%s failed. item type mismatch. arrayPath=%s index=%ld expected=number",
                functionName,
                arrayPath,
                static_cast<long>(index));
    cJSON_Delete(rootObject);
    return false;
  }

  *valueOut = static_cast<long>(itemNode->valuedouble);
  cJSON_Delete(rootObject);
  return true;
}

bool jsonService::getArrayValueByPath(const String& payload, const char* arrayPath, int32_t index, bool* valueOut) {
  constexpr const char* functionName = "jsonService::getArrayValueByPath(bool*)";
  if (arrayPath == nullptr || valueOut == nullptr || index < 0) {
    appLogError("%s failed. invalid parameter. arrayPath=%p index=%ld valueOut=%p",
                functionName,
                arrayPath,
                static_cast<long>(index),
                valueOut);
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(payload, functionName);
  if (rootObject == nullptr) {
    return false;
  }
  cJSON* arrayNode = getItemByPath(rootObject, arrayPath, functionName);
  if (arrayNode == nullptr || !cJSON_IsArray(arrayNode)) {
    appLogError("%s failed. path is not array. arrayPath=%s", functionName, arrayPath);
    cJSON_Delete(rootObject);
    return false;
  }

  cJSON* itemNode = cJSON_GetArrayItem(arrayNode, index);
  if (!cJSON_IsBool(itemNode)) {
    appLogError("%s failed. item type mismatch. arrayPath=%s index=%ld expected=bool",
                functionName,
                arrayPath,
                static_cast<long>(index));
    cJSON_Delete(rootObject);
    return false;
  }

  *valueOut = cJSON_IsTrue(itemNode);
  cJSON_Delete(rootObject);
  return true;
}

bool jsonService::setValuesByPath(String* payloadInOut, const jsonKeyValueItem* itemList, size_t itemCount) {
  constexpr const char* functionName = "jsonService::setValuesByPath";
  if (payloadInOut == nullptr || itemList == nullptr || itemCount == 0) {
    appLogError("%s failed. invalid parameter. payloadInOut=%p itemList=%p itemCount=%u",
                functionName,
                payloadInOut,
                itemList,
                static_cast<unsigned>(itemCount));
    return false;
  }

  cJSON* rootObject = parseOrCreateRootObject(*payloadInOut, functionName);
  if (rootObject == nullptr) {
    return false;
  }

  for (size_t index = 0; index < itemCount; ++index) {
    const jsonKeyValueItem& currentItem = itemList[index];
    bool setResult = false;
    switch (currentItem.valueType) {
      case jsonValueType::kString:
        setResult = setStringByPathInternal(rootObject, currentItem.keyPath, currentItem.stringValue, functionName);
        break;
      case jsonValueType::kShort:
        setResult = setNumberByPathInternal(rootObject, currentItem.keyPath, static_cast<long>(currentItem.shortValue), functionName);
        break;
      case jsonValueType::kLong:
        setResult = setNumberByPathInternal(rootObject, currentItem.keyPath, currentItem.longValue, functionName);
        break;
      case jsonValueType::kBool:
        setResult = setBoolByPathInternal(rootObject, currentItem.keyPath, currentItem.boolValue, functionName);
        break;
      default:
        appLogError("%s failed. unsupported valueType. index=%u valueType=%d",
                    functionName,
                    static_cast<unsigned>(index),
                    static_cast<int>(currentItem.valueType));
        setResult = false;
        break;
    }

    if (!setResult) {
      appLogError("%s failed. set item error. index=%u keyPath=%s", functionName, static_cast<unsigned>(index), currentItem.keyPath);
      cJSON_Delete(rootObject);
      return false;
    }
  }

  bool writeResult = writeRootObjectToPayload(rootObject, payloadInOut, functionName);
  cJSON_Delete(rootObject);
  return writeResult;
}
