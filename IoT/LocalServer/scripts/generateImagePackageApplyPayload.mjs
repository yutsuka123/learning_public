/**
 * @file generateImagePackageApplyPayload.mjs
 * @description imagePackageApply の署名付きMQTT payloadを生成し、署名自己検証まで行うCLI。
 * @details
 * - [重要] ESP32側 `buildSignedPayloadForVerification` と同等に、`signature` を除いたJSON文字列を HMAC-SHA256 入力にする。
 * - [厳守] `kDeviceBase64` は 32byte 復号できる値を必須とする。
 * - [厳守] `destinationDir` は `/images` 配下のみ許可し、`/logs` や `..` を拒否する。
 * - [制限] 本スクリプトは payload 生成専用。MQTT publish は行わない。
 */

import crypto from "crypto";

/**
 * @typedef {Object} imagePackageArgs
 * @property {string} receiverName MQTT topic の receiverName。
 * @property {string} sourceId Payload の SrcID。
 * @property {string} messageId Payload の id。
 * @property {string} sessionId args.sessionId。
 * @property {string} packageUrl args.packageUrl。
 * @property {string} packageSha256 args.packageSha256。
 * @property {string} destinationDir args.destinationDir。
 * @property {boolean} overwrite args.overwrite。
 * @property {string} kDeviceBase64 署名鍵(Base64)。
 */

/**
 * @description コマンドライン引数をキー・値へ展開する。
 * @param {string[]} argumentList Node引数配列。
 * @returns {Map<string, string>} 解析済み引数マップ。
 */
function parseArgumentMap(argumentList) {
  const argumentMap = new Map();
  for (let index = 0; index < argumentList.length; index += 1) {
    const currentArgument = argumentList[index];
    if (!currentArgument.startsWith("--")) {
      continue;
    }
    const argumentKey = currentArgument.substring(2);
    const nextArgument = argumentList[index + 1];
    if (!nextArgument || nextArgument.startsWith("--")) {
      argumentMap.set(argumentKey, "true");
      continue;
    }
    argumentMap.set(argumentKey, nextArgument);
    index += 1;
  }
  return argumentMap;
}

/**
 * @description 必須引数を取得する。
 * @param {Map<string, string>} argumentMap 引数マップ。
 * @param {string} key 取得キー。
 * @returns {string} 値。
 */
function requireArgument(argumentMap, key) {
  const value = (argumentMap.get(key) || "").trim();
  if (value.length === 0) {
    throw new Error(`requireArgument failed. key=${key} is required.`);
  }
  return value;
}

/**
 * @description 文字列のSHA256表記を検証して正規化する。
 * @param {string} sha256Text 検証対象。
 * @returns {string} 小文字化済みSHA256。
 */
function normalizeSha256Hex(sha256Text) {
  const normalizedValue = sha256Text.trim().toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(normalizedValue)) {
    throw new Error(`normalizeSha256Hex failed. invalid sha256 format. value=${sha256Text}`);
  }
  return normalizedValue;
}

/**
 * @description destinationDir を検証する。
 * @param {string} destinationDir 展開先ディレクトリ。
 * @returns {string} 正規化済みディレクトリ。
 */
function normalizeDestinationDir(destinationDir) {
  let normalizedValue = destinationDir.trim();
  if (normalizedValue.length === 0) {
    throw new Error("normalizeDestinationDir failed. destinationDir is empty.");
  }
  if (!normalizedValue.startsWith("/")) {
    normalizedValue = `/${normalizedValue}`;
  }
  normalizedValue = normalizedValue.replace(/\/{2,}/g, "/");
  if (normalizedValue.includes("..")) {
    throw new Error(`normalizeDestinationDir failed. traversal is prohibited. destinationDir=${normalizedValue}`);
  }
  if (normalizedValue === "/logs" || normalizedValue.startsWith("/logs/")) {
    throw new Error(`normalizeDestinationDir failed. /logs is prohibited. destinationDir=${normalizedValue}`);
  }
  if (!(normalizedValue === "/images" || normalizedValue.startsWith("/images/"))) {
    throw new Error(`normalizeDestinationDir failed. destinationDir must be under /images. destinationDir=${normalizedValue}`);
  }
  return normalizedValue;
}

/**
 * @description imagePackageApply 用の署名対象payloadを組み立てる。
 * @param {imagePackageArgs} args 入力値。
 * @returns {Record<string, unknown>} 署名前payload。
 */
function buildUnsignedPayload(args) {
  return {
    v: 1,
    DstID: args.receiverName,
    SrcID: args.sourceId,
    id: args.messageId,
    ts: new Date().toISOString(),
    op: "call",
    sub: "imagePackageApply",
    sigAlg: "HMAC-SHA256",
    args: {
      sessionId: args.sessionId,
      packageUrl: args.packageUrl,
      packageSha256: args.packageSha256,
      destinationDir: args.destinationDir,
      overwrite: args.overwrite
    }
  };
}

/**
 * @description HMAC-SHA256署名を計算する。
 * @param {Buffer} keyBuffer k-device鍵。
 * @param {string} messageText 入力テキスト。
 * @returns {string} Base64署名。
 */
function computeSignatureBase64(keyBuffer, messageText) {
  return crypto.createHmac("sha256", keyBuffer).update(messageText).digest("base64");
}

/**
 * @description payload署名を自己検証する。
 * @param {Buffer} keyBuffer k-device鍵。
 * @param {string} normalizedPayloadText signature除外のJSON文字列。
 * @param {string} signatureBase64 署名。
 */
function verifySignatureOrThrow(keyBuffer, normalizedPayloadText, signatureBase64) {
  const expectedSignature = computeSignatureBase64(keyBuffer, normalizedPayloadText);
  if (expectedSignature !== signatureBase64) {
    throw new Error("verifySignatureOrThrow failed. recomputed signature does not match.");
  }
}

/**
 * @description スクリプト入口。
 */
function main() {
  const argumentMap = parseArgumentMap(process.argv.slice(2));
  const receiverName = requireArgument(argumentMap, "receiverName");
  const sourceId = (argumentMap.get("sourceId") || "server-001").trim();
  const messageId = (argumentMap.get("messageId") || `server-001-${Date.now()}`).trim();
  const sessionId = (argumentMap.get("sessionId") || `imgpkg-${Date.now()}`).trim();
  const packageUrl = requireArgument(argumentMap, "packageUrl");
  const packageSha256 = normalizeSha256Hex(requireArgument(argumentMap, "packageSha256"));
  const destinationDir = normalizeDestinationDir(requireArgument(argumentMap, "destinationDir"));
  const overwrite = (argumentMap.get("overwrite") || "true").toLowerCase() === "true";
  const kDeviceBase64 = requireArgument(argumentMap, "kDeviceBase64");
  const keyBuffer = Buffer.from(kDeviceBase64, "base64");
  if (keyBuffer.length !== 32) {
    throw new Error(`main failed. k-device must decode to 32 bytes. actual=${keyBuffer.length}`);
  }

  /** @type {imagePackageArgs} */
  const args = {
    receiverName,
    sourceId,
    messageId,
    sessionId,
    packageUrl,
    packageSha256,
    destinationDir,
    overwrite,
    kDeviceBase64
  };

  const unsignedPayload = buildUnsignedPayload(args);
  const normalizedPayloadText = JSON.stringify(unsignedPayload);
  const signatureBase64 = computeSignatureBase64(keyBuffer, normalizedPayloadText);
  verifySignatureOrThrow(keyBuffer, normalizedPayloadText, signatureBase64);
  const signedPayload = {
    ...unsignedPayload,
    signature: signatureBase64
  };

  console.log(`TOPIC=esp32lab/call/imagePackageApply/${receiverName}`);
  console.log(`PAYLOAD=${JSON.stringify(signedPayload)}`);
  console.log(`NORMALIZED_PAYLOAD=${normalizedPayloadText}`);
  console.log("SELF_VERIFY=OK");
}

try {
  main();
} catch (error) {
  const errorMessage = error instanceof Error ? error.message : String(error);
  console.error(`[ERROR] ${errorMessage}`);
  process.exit(1);
}

