/**
 * @file util.cpp
 * @brief 汎用ユーティリティ関数の実装。
 */

#include "util.h"

#include <mbedtls/sha256.h>

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

}  // namespace appUtil
