/**
 * @file mqttPayloadSecurity.cpp
 * @brief MQTT payload全文暗号化の共通処理実装。
 * @details
 * - [重要] エンベロープ形式は `security.mode=k-device-a256gcm-v1` 固定とする。
 * - [厳守] `enc.alg=A256GCM` 以外は受理しない。
 * - [禁止] 復号失敗時に平文推定で処理継続しない。
 */

#include "mqttPayloadSecurity.h"

#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>

#include "common.h"
#include "jsonService.h"
#include "log.h"

namespace {

bool decodeBase64Text(const String& inputBase64, std::vector<uint8_t>* outputBufferOut) {
  if (outputBufferOut == nullptr) {
    appLogError("mqttPayloadSecurity::decodeBase64Text failed. outputBufferOut is null.");
    return false;
  }
  outputBufferOut->clear();
  size_t outputLength = 0;
  int32_t firstResult = mbedtls_base64_decode(nullptr,
                                              0,
                                              &outputLength,
                                              reinterpret_cast<const unsigned char*>(inputBase64.c_str()),
                                              inputBase64.length());
  if (firstResult != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && firstResult != 0) {
    appLogError("mqttPayloadSecurity::decodeBase64Text failed. probe decode result=%ld inputLength=%ld",
                static_cast<long>(firstResult),
                static_cast<long>(inputBase64.length()));
    return false;
  }
  outputBufferOut->resize(outputLength);
  if (outputLength == 0) {
    return true;
  }
  int32_t secondResult = mbedtls_base64_decode(outputBufferOut->data(),
                                               outputBufferOut->size(),
                                               &outputLength,
                                               reinterpret_cast<const unsigned char*>(inputBase64.c_str()),
                                               inputBase64.length());
  if (secondResult != 0) {
    appLogError("mqttPayloadSecurity::decodeBase64Text failed. decode result=%ld inputLength=%ld",
                static_cast<long>(secondResult),
                static_cast<long>(inputBase64.length()));
    outputBufferOut->clear();
    return false;
  }
  outputBufferOut->resize(outputLength);
  return true;
}

bool encodeBase64Text(const std::vector<uint8_t>& inputBuffer, String* outputBase64Out) {
  if (outputBase64Out == nullptr) {
    appLogError("mqttPayloadSecurity::encodeBase64Text failed. outputBase64Out is null.");
    return false;
  }
  size_t encodedLength = 0;
  int32_t probeResult = mbedtls_base64_encode(nullptr, 0, &encodedLength, inputBuffer.data(), inputBuffer.size());
  if (probeResult != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && probeResult != 0) {
    appLogError("mqttPayloadSecurity::encodeBase64Text failed. probe result=%ld inputSize=%ld",
                static_cast<long>(probeResult),
                static_cast<long>(inputBuffer.size()));
    return false;
  }
  std::vector<uint8_t> outputBytes(encodedLength + 1, 0);
  int32_t encodeResult = mbedtls_base64_encode(outputBytes.data(),
                                               outputBytes.size(),
                                               &encodedLength,
                                               inputBuffer.data(),
                                               inputBuffer.size());
  if (encodeResult != 0) {
    appLogError("mqttPayloadSecurity::encodeBase64Text failed. encode result=%ld inputSize=%ld",
                static_cast<long>(encodeResult),
                static_cast<long>(inputBuffer.size()));
    return false;
  }
  outputBase64Out->reserve(encodedLength);
  outputBase64Out->remove(0);
  for (size_t index = 0; index < encodedLength; ++index) {
    *outputBase64Out += static_cast<char>(outputBytes[index]);
  }
  return true;
}

bool decryptAesGcm(const std::vector<uint8_t>& keyBytes,
                   const std::vector<uint8_t>& ivBytes,
                   const std::vector<uint8_t>& cipherBytes,
                   const std::vector<uint8_t>& tagBytes,
                   String* plainTextOut) {
  if (plainTextOut == nullptr) {
    appLogError("mqttPayloadSecurity::decryptAesGcm failed. plainTextOut is null.");
    return false;
  }
  if (keyBytes.size() != 32 || ivBytes.size() != 12 || tagBytes.size() != 16) {
    appLogError("mqttPayloadSecurity::decryptAesGcm failed. invalid size. key=%ld iv=%ld tag=%ld",
                static_cast<long>(keyBytes.size()),
                static_cast<long>(ivBytes.size()),
                static_cast<long>(tagBytes.size()));
    return false;
  }
  std::vector<uint8_t> plainBytes(cipherBytes.size());
  mbedtls_gcm_context gcmContext;
  mbedtls_gcm_init(&gcmContext);
  int32_t keyResult = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, keyBytes.data(), 256);
  if (keyResult != 0) {
    mbedtls_gcm_free(&gcmContext);
    appLogError("mqttPayloadSecurity::decryptAesGcm failed. setkey result=%ld", static_cast<long>(keyResult));
    return false;
  }
  int32_t decryptResult = mbedtls_gcm_auth_decrypt(&gcmContext,
                                                   cipherBytes.size(),
                                                   ivBytes.data(),
                                                   ivBytes.size(),
                                                   nullptr,
                                                   0,
                                                   tagBytes.data(),
                                                   tagBytes.size(),
                                                   cipherBytes.data(),
                                                   plainBytes.data());
  mbedtls_gcm_free(&gcmContext);
  if (decryptResult != 0) {
    appLogError("mqttPayloadSecurity::decryptAesGcm failed. auth_decrypt result=%ld", static_cast<long>(decryptResult));
    return false;
  }
  plainTextOut->remove(0);
  plainTextOut->reserve(plainBytes.size());
  for (size_t index = 0; index < plainBytes.size(); ++index) {
    *plainTextOut += static_cast<char>(plainBytes[index]);
  }
  return true;
}

bool encryptAesGcm(const std::vector<uint8_t>& keyBytes,
                   const String& plainText,
                   std::vector<uint8_t>* ivBytesOut,
                   std::vector<uint8_t>* cipherBytesOut,
                   std::vector<uint8_t>* tagBytesOut) {
  if (ivBytesOut == nullptr || cipherBytesOut == nullptr || tagBytesOut == nullptr) {
    appLogError("mqttPayloadSecurity::encryptAesGcm failed. output parameter is null.");
    return false;
  }
  if (keyBytes.size() != 32) {
    appLogError("mqttPayloadSecurity::encryptAesGcm failed. invalid key size=%ld", static_cast<long>(keyBytes.size()));
    return false;
  }
  ivBytesOut->assign(12, 0);
  for (size_t index = 0; index < ivBytesOut->size(); ++index) {
    (*ivBytesOut)[index] = static_cast<uint8_t>(esp_random() & 0xFF);
  }
  cipherBytesOut->assign(plainText.length(), 0);
  tagBytesOut->assign(16, 0);
  mbedtls_gcm_context gcmContext;
  mbedtls_gcm_init(&gcmContext);
  int32_t keyResult = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, keyBytes.data(), 256);
  if (keyResult != 0) {
    mbedtls_gcm_free(&gcmContext);
    appLogError("mqttPayloadSecurity::encryptAesGcm failed. setkey result=%ld", static_cast<long>(keyResult));
    return false;
  }
  int32_t encryptResult = mbedtls_gcm_crypt_and_tag(&gcmContext,
                                                    MBEDTLS_GCM_ENCRYPT,
                                                    plainText.length(),
                                                    ivBytesOut->data(),
                                                    ivBytesOut->size(),
                                                    nullptr,
                                                    0,
                                                    reinterpret_cast<const unsigned char*>(plainText.c_str()),
                                                    cipherBytesOut->data(),
                                                    tagBytesOut->size(),
                                                    tagBytesOut->data());
  mbedtls_gcm_free(&gcmContext);
  if (encryptResult != 0) {
    appLogError("mqttPayloadSecurity::encryptAesGcm failed. crypt_and_tag result=%ld", static_cast<long>(encryptResult));
    return false;
  }
  return true;
}

}  // namespace

namespace mqttPayloadSecurity {

bool encodeEncryptedEnvelope(const std::vector<uint8_t>& keyBytes,
                             const String& plainPayloadText,
                             String* encryptedEnvelopeOut) {
  if (encryptedEnvelopeOut == nullptr) {
    appLogError("mqttPayloadSecurity::encodeEncryptedEnvelope failed. encryptedEnvelopeOut is null.");
    return false;
  }
  std::vector<uint8_t> ivBytes;
  std::vector<uint8_t> cipherBytes;
  std::vector<uint8_t> tagBytes;
  if (!encryptAesGcm(keyBytes, plainPayloadText, &ivBytes, &cipherBytes, &tagBytes)) {
    return false;
  }
  String ivBase64;
  String cipherBase64;
  String tagBase64;
  if (!encodeBase64Text(ivBytes, &ivBase64) ||
      !encodeBase64Text(cipherBytes, &cipherBase64) ||
      !encodeBase64Text(tagBytes, &tagBase64)) {
    return false;
  }
  String payloadOut = "{}";
  jsonService payloadJsonService;
  jsonKeyValueItem itemList[] = {
      {"v", jsonValueType::kString, iotCommon::kProtocolVersion, 0, 0, false},
      {"security.mode", jsonValueType::kString, kEnvelopeSecurityMode, 0, 0, false},
      {"enc.alg", jsonValueType::kString, kEnvelopeAlgorithm, 0, 0, false},
      {"enc.iv", jsonValueType::kString, ivBase64.c_str(), 0, 0, false},
      {"enc.ct", jsonValueType::kString, cipherBase64.c_str(), 0, 0, false},
      {"enc.tag", jsonValueType::kString, tagBase64.c_str(), 0, 0, false},
  };
  if (!payloadJsonService.setValuesByPath(&payloadOut, itemList, sizeof(itemList) / sizeof(itemList[0]))) {
    appLogError("mqttPayloadSecurity::encodeEncryptedEnvelope failed. setValuesByPath returned false.");
    return false;
  }
  *encryptedEnvelopeOut = payloadOut;
  return true;
}

bool decodeEncryptedEnvelopeIfPresent(const std::vector<uint8_t>& keyBytes,
                                      const String& payloadText,
                                      bool* isEncryptedEnvelopeOut,
                                      String* plainPayloadOut) {
  if (isEncryptedEnvelopeOut == nullptr || plainPayloadOut == nullptr) {
    appLogError("mqttPayloadSecurity::decodeEncryptedEnvelopeIfPresent failed. output parameter is null.");
    return false;
  }
  *isEncryptedEnvelopeOut = false;
  plainPayloadOut->remove(0);
  jsonService payloadJsonService;
  String securityMode;
  if (!payloadJsonService.getValueByPath(payloadText, "security.mode", &securityMode)) {
    return true;
  }
  *isEncryptedEnvelopeOut = true;
  if (securityMode != kEnvelopeSecurityMode) {
    appLogError("mqttPayloadSecurity::decodeEncryptedEnvelopeIfPresent failed. unsupported security.mode=%s", securityMode.c_str());
    return false;
  }
  String algorithm;
  String ivBase64;
  String cipherBase64;
  String tagBase64;
  payloadJsonService.getValueByPath(payloadText, "enc.alg", &algorithm);
  payloadJsonService.getValueByPath(payloadText, "enc.iv", &ivBase64);
  payloadJsonService.getValueByPath(payloadText, "enc.ct", &cipherBase64);
  payloadJsonService.getValueByPath(payloadText, "enc.tag", &tagBase64);
  if (algorithm != kEnvelopeAlgorithm || ivBase64.length() == 0 || cipherBase64.length() == 0 || tagBase64.length() == 0) {
    appLogError("mqttPayloadSecurity::decodeEncryptedEnvelopeIfPresent failed. encrypted field is missing or invalid.");
    return false;
  }
  std::vector<uint8_t> ivBytes;
  std::vector<uint8_t> cipherBytes;
  std::vector<uint8_t> tagBytes;
  if (!decodeBase64Text(ivBase64, &ivBytes) || !decodeBase64Text(cipherBase64, &cipherBytes) || !decodeBase64Text(tagBase64, &tagBytes)) {
    return false;
  }
  if (!decryptAesGcm(keyBytes, ivBytes, cipherBytes, tagBytes, plainPayloadOut)) {
    return false;
  }
  return true;
}

}  // namespace mqttPayloadSecurity

