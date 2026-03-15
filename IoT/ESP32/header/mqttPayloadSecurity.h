/**
 * @file mqttPayloadSecurity.h
 * @brief MQTT payload全文暗号化の共通処理宣言。
 * @details
 * - [重要] トピックは平文のまま、payload本文のみをAES-256-GCMで暗号化する。
 * - [厳守] 復号時は envelope の `security.mode` と `enc.alg` を検証する。
 * - [推奨] 平文運用へ戻す場合は `payloadSecurityMode` を `kPlain` にする。
 */

#pragma once

#include <Arduino.h>
#include <vector>

namespace mqttPayloadSecurity {

enum class payloadSecurityMode : uint8_t {
  kPlain = 0,
  kCompat = 1,
  kStrict = 2,
};

constexpr const char* kEnvelopeSecurityMode = "k-device-a256gcm-v1";
constexpr const char* kEnvelopeAlgorithm = "A256GCM";

/**
 * @brief 平文payloadを暗号化エンベロープJSONへ変換する。
 * @param keyBytes 暗号鍵（32バイト）。
 * @param plainPayloadText 平文payload(JSON文字列)。
 * @param encryptedEnvelopeOut 暗号化後エンベロープJSON出力先。
 * @return 成功時true、失敗時false。
 */
bool encodeEncryptedEnvelope(const std::vector<uint8_t>& keyBytes,
                             const String& plainPayloadText,
                             String* encryptedEnvelopeOut);

/**
 * @brief 受信payloadが暗号化エンベロープなら復号する。
 * @param keyBytes 復号鍵（32バイト）。
 * @param payloadText 受信payload文字列。
 * @param isEncryptedEnvelopeOut エンベロープ判定結果。
 * @param plainPayloadOut 復号平文（エンベロープでない場合は未変更）。
 * @return
 * - true: 正常（エンベロープでない場合を含む）
 * - false: エンベロープだったが復号失敗
 */
bool decodeEncryptedEnvelopeIfPresent(const std::vector<uint8_t>& keyBytes,
                                      const String& payloadText,
                                      bool* isEncryptedEnvelopeOut,
                                      String* plainPayloadOut);

}  // namespace mqttPayloadSecurity

