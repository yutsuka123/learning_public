/**
 * @file testImagePackageApplyE2E.mjs
 * @description imagePackageApply の実送信と `imagePackageStatus` 受信確認を行うE2E試験スクリプト。
 * @details
 * - [重要] LocalServer の `.env` 設定を読み込み、MQTT/TLS条件を自動適用する。
 * - [厳守] 送信payloadは `signature` を除いたJSONを HMAC-SHA256(k-device) で署名する。
 * - [厳守] MQTT payload暗号化モードは strict 前提で、送信時は AES-256-GCM エンベロープを使用する。
 * - [厳守] `imagePackageStatus` は `sessionId` 一致で追跡し、`phase=completed` かつ `result=OK` を成功判定とする。
 * - [制限] 本スクリプトはESP32ファイル実体の直接読取りは行わず、MQTT通知とシリアルログで反映を確認する。
 */

import fs from "fs";
import path from "path";
import { createRequire } from "module";

const require = createRequire(import.meta.url);
const mqtt = require("mqtt");
const { loadConfig } = require("../dist/config.js");
const { keyService } = require("../dist/keyService.js");
const { mqttPayloadSecurityService, resolveMqttPayloadEncryptionMode } = require("../dist/mqttPayloadSecurity.js");

/**
 * @typedef {Object} argumentSet
 * @property {string} targetName 対象ESP32名。
 * @property {string} packageUrl ZIP取得URL。
 * @property {string} packageSha256 ZIP SHA-256。
 * @property {string} destinationDir 展開先。
 * @property {boolean} overwrite 上書き可否。
 * @property {number} timeoutMs タイムアウト(ms)。
 */

/**
 * @description 引数配列をMapに変換する。
 * @param {string[]} args 解析対象引数。
 * @returns {Map<string, string>} 解析済み引数。
 */
function parseArgumentMap(args) {
  const map = new Map();
  for (let index = 0; index < args.length; index += 1) {
    const current = args[index];
    if (!current.startsWith("--")) {
      continue;
    }
    const key = current.substring(2);
    const next = args[index + 1];
    if (!next || next.startsWith("--")) {
      map.set(key, "true");
      continue;
    }
    map.set(key, next);
    index += 1;
  }
  return map;
}

/**
 * @description 必須引数を取得する。
 * @param {Map<string, string>} argMap 引数マップ。
 * @param {string} key 引数キー。
 * @returns {string} 値。
 */
function requireArg(argMap, key) {
  const value = (argMap.get(key) || "").trim();
  if (value.length === 0) {
    throw new Error(`requireArg failed. key=${key} is required.`);
  }
  return value;
}

/**
 * @description 文字列SHA-256を正規化・検証する。
 * @param {string} shaText 入力SHA。
 * @returns {string} 小文字SHA。
 */
function normalizeSha256(shaText) {
  const normalized = shaText.trim().toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(normalized)) {
    throw new Error(`normalizeSha256 failed. invalid format. value=${shaText}`);
  }
  return normalized;
}

/**
 * @description destinationDir を検証する。
 * @param {string} destinationDir 展開先。
 * @returns {string} 正規化済み展開先。
 */
function normalizeDestinationDir(destinationDir) {
  let normalized = destinationDir.trim();
  if (normalized.length === 0) {
    throw new Error("normalizeDestinationDir failed. destinationDir is empty.");
  }
  if (!normalized.startsWith("/")) {
    normalized = `/${normalized}`;
  }
  normalized = normalized.replace(/\/{2,}/g, "/");
  if (normalized.includes("..")) {
    throw new Error(`normalizeDestinationDir failed. traversal is prohibited. destinationDir=${normalized}`);
  }
  if (normalized === "/logs" || normalized.startsWith("/logs/")) {
    throw new Error(`normalizeDestinationDir failed. /logs is prohibited. destinationDir=${normalized}`);
  }
  if (!(normalized === "/images" || normalized.startsWith("/images/"))) {
    throw new Error(`normalizeDestinationDir failed. destinationDir must be under /images. destinationDir=${normalized}`);
  }
  return normalized;
}

/**
 * @description 署名対象payloadを組み立てる。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {argumentSet} args 試験引数。
 * @param {string} sessionId セッションID。
 * @returns {Record<string, unknown>} 署名前payload。
 */
function buildUnsignedPayload(config, args, sessionId) {
  return {
    v: 1,
    DstID: args.targetName,
    SrcID: config.sourceId,
    id: `${config.sourceId}-${Date.now()}`,
    ts: new Date().toISOString(),
    op: "call",
    sub: "imagePackageApply",
    sigAlg: "HMAC-SHA256",
    args: {
      sessionId,
      packageUrl: args.packageUrl,
      packageSha256: args.packageSha256,
      destinationDir: args.destinationDir,
      overwrite: args.overwrite
    }
  };
}

/**
 * @description ローカル管理APIへログインして管理者トークンを取得する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @returns {Promise<string>} 管理者トークン。
 */
async function loginAdmin(config) {
  const response = await fetch(`http://127.0.0.1:${config.httpPort}/api/admin/auth/login`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      username: config.adminUsername,
      password: config.adminPassword
    })
  });
  if (!response.ok) {
    throw new Error(`loginAdmin failed. status=${response.status}`);
  }
  const json = await response.json();
  const token = String(json?.token ?? "");
  if (token.length === 0) {
    throw new Error("loginAdmin failed. token is empty.");
  }
  return token;
}

/**
 * @description 対象ESP32へ k-device を再投入する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {string} adminToken 管理者トークン。
 * @param {string} targetName 対象名。
 * @returns {Promise<void>}
 */
async function issueAndPushKDevice(config, adminToken, targetName) {
  const response = await fetch(`http://127.0.0.1:${config.httpPort}/api/admin/keys/k-device/issue`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${adminToken}`
    },
    body: JSON.stringify({
      targetDeviceName: targetName,
      pushToDevice: true
    })
  });
  if (!response.ok) {
    const detailText = await response.text();
    throw new Error(`issueAndPushKDevice failed. status=${response.status} detail=${detailText}`);
  }
}

/**
 * @description MQTTへ imagePackageApply を送信し、完了通知を待機する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {argumentSet} args 試験引数。
 * @param {string} keyDeviceBase64 k-device。
 * @param {string} sessionId セッションID。
 * @returns {Promise<{statusMessage: Record<string, unknown>, encryptedIncoming: number}>} 最終通知と暗号化受信回数。
 */
function publishAndWaitStatus(config, args, keyDeviceBase64, sessionId) {
  return new Promise((resolve, reject) => {
    const keyBuffer = Buffer.from(keyDeviceBase64, "base64");
    if (keyBuffer.length !== 32) {
      reject(new Error(`publishAndWaitStatus failed. k-device length must be 32. actual=${keyBuffer.length}`));
      return;
    }
    const localKeyService = new keyService(config);
    const encryptionMode = resolveMqttPayloadEncryptionMode();
    const payloadSecurity = new mqttPayloadSecurityService(localKeyService, encryptionMode);

    const unsignedPayload = buildUnsignedPayload(config, args, sessionId);
    const normalizedPayload = JSON.stringify(unsignedPayload);
    const signature = require("crypto").createHmac("sha256", keyBuffer).update(normalizedPayload).digest("base64");
    const signedPayload = {
      ...unsignedPayload,
      signature
    };

    const callTopic = `esp32lab/call/imagePackageApply/${args.targetName}`;
    const statusTopic = "esp32lab/notice/imagePackageStatus/+";

    /** @type {import("mqtt").IClientOptions} */
    const connectOptions = {
      protocol: config.mqttProtocol,
      host: config.mqttHostName,
      port: config.mqttPort,
      username: config.mqttUsername,
      password: config.mqttPassword,
      reconnectPeriod: 0,
      connectTimeout: config.mqttConnectTimeoutMs,
      rejectUnauthorized: true
    };
    if (config.mqttProtocol === "mqtts" && config.mqttCaPath.length > 0) {
      connectOptions.ca = fs.readFileSync(config.mqttCaPath);
    }

    const client = mqtt.connect(connectOptions);
    let timeoutHandle = null;
    let encryptedIncomingCount = 0;

    const cleanup = () => {
      if (timeoutHandle !== null) {
        clearTimeout(timeoutHandle);
      }
      client.end(true);
    };

    timeoutHandle = setTimeout(() => {
      cleanup();
      reject(new Error(`publishAndWaitStatus timeout. sessionId=${sessionId} timeoutMs=${args.timeoutMs}`));
    }, args.timeoutMs);

    client.on("error", (error) => {
      cleanup();
      reject(new Error(`publishAndWaitStatus mqtt error. message=${error.message}`));
    });

    client.on("connect", () => {
      client.subscribe(statusTopic, { qos: 1 }, (subscribeError) => {
        if (subscribeError) {
          cleanup();
          reject(new Error(`publishAndWaitStatus subscribe failed. topic=${statusTopic} detail=${subscribeError.message}`));
          return;
        }

        const encryptedPayloadText = payloadSecurity.encodeOutgoingPayload(args.targetName, JSON.stringify(signedPayload));
        client.publish(callTopic, encryptedPayloadText, { qos: 1, retain: false }, (publishError) => {
          if (publishError) {
            cleanup();
            reject(new Error(`publishAndWaitStatus publish failed. topic=${callTopic} detail=${publishError.message}`));
          }
        });
      });
    });

    client.on("message", (topic, payloadBuffer) => {
      if (!topic.startsWith("esp32lab/notice/imagePackageStatus/")) {
        return;
      }
      const incomingPayloadText = payloadBuffer.toString("utf8");
      let decoded;
      try {
        decoded = payloadSecurity.decodeIncomingPayload(args.targetName, incomingPayloadText);
      } catch (decodeError) {
        const detail = decodeError instanceof Error ? decodeError.message : String(decodeError);
        console.warn(`[WARN] status decode skipped. topic=${topic} detail=${detail}`);
        return;
      }
      if (decoded.encrypted) {
        encryptedIncomingCount += 1;
      }
      let json;
      try {
        json = JSON.parse(decoded.plainPayloadText);
      } catch (parseError) {
        const detail = parseError instanceof Error ? parseError.message : String(parseError);
        console.warn(`[WARN] status parse skipped. topic=${topic} detail=${detail}`);
        return;
      }
      if (String(json?.sessionId ?? "") !== sessionId) {
        return;
      }
      const phase = String(json?.phase ?? "");
      const result = String(json?.result ?? "");
      if (phase === "failed" || result === "NG") {
        cleanup();
        reject(new Error(`publishAndWaitStatus failed status. phase=${phase} result=${result} errorCode=${String(json?.errorCode ?? "")} detail=${String(json?.detail ?? "")}`));
        return;
      }
      if (phase === "completed" && result === "OK") {
        cleanup();
        resolve({
          statusMessage: json,
          encryptedIncoming: encryptedIncomingCount
        });
      }
    });
  });
}

/**
 * @description 入口処理。
 */
async function main() {
  const argMap = parseArgumentMap(process.argv.slice(2));
  /** @type {argumentSet} */
  const args = {
    targetName: requireArg(argMap, "targetName"),
    packageUrl: (argMap.get("packageUrl") || "https://mqtt.esplab.home.arpa:4443/assets/images-pack.zip").trim(),
    packageSha256: normalizeSha256(requireArg(argMap, "packageSha256")),
    destinationDir: normalizeDestinationDir(argMap.get("destinationDir") || "/images/top"),
    overwrite: (argMap.get("overwrite") || "true").toLowerCase() === "true",
    timeoutMs: Number(argMap.get("timeoutMs") || "90000")
  };
  if (!Number.isFinite(args.timeoutMs) || args.timeoutMs <= 0) {
    throw new Error(`main failed. timeoutMs is invalid. value=${argMap.get("timeoutMs")}`);
  }

  const config = loadConfig();
  const adminToken = await loginAdmin(config);
  await issueAndPushKDevice(config, adminToken, args.targetName);

  const localKeyService = new keyService(config);
  const keyDeviceBase64 = localKeyService.getKDeviceBase64(args.targetName);
  const sessionId = `imgpkg-e2e-${Date.now()}`;
  const statusResult = await publishAndWaitStatus(config, args, keyDeviceBase64, sessionId);
  const statusJsonText = JSON.stringify(statusResult.statusMessage);
  console.log(`[OK] imagePackageApply completed. sessionId=${sessionId}`);
  console.log(`[OK] encryptedIncoming=${statusResult.encryptedIncoming}`);
  console.log(`[OK] finalStatus=${statusJsonText}`);
}

main().catch((error) => {
  const detail = error instanceof Error ? error.message : String(error);
  console.error(`[ERROR] ${detail}`);
  process.exit(1);
});

