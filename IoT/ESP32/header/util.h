/**
 * @file util.h
 * @brief 汎用ユーティリティ関数の宣言。
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace appUtil {

/**
 * @brief eFuse base_mac配列からpublic_id文字列を生成する。
 * @param baseMac 6バイトのMAC配列。
 * @return public_id（16文字の16進文字列）。
 */
String createPublicIdFromBaseMac(const uint8_t baseMac[6]);

/**
 * @brief 16進MAC文字列をバイト配列へ変換する。
 * @param baseMacHex 12桁またはコロン区切り16進文字列。
 * @param outBaseMac 変換先6バイト配列。
 * @return 変換成功時true。
 */
bool parseBaseMacHex(const String& baseMacHex, uint8_t outBaseMac[6]);

/**
 * @brief 秒をミリ秒へ変換する（オーバーフロー安全）。
 * @param seconds 秒。
 * @return ミリ秒。
 */
uint32_t secondsToMilliseconds(uint32_t seconds);

}  // namespace appUtil
