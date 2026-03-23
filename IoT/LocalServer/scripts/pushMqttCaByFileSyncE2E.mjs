/**
 * @file pushMqttCaByFileSyncE2E.mjs
 * @description `fileSyncPlan` / `fileSyncChunk` / `fileSyncCommit` を順次送信し、ESP32 の `/certs/mqtt-ca.pem` を更新するE2Eスクリプト。
 *
 * 主仕様:
 * - [重要] MQTT payload暗号化モード `strict` を前提とし、送受信は AES-256-GCM エンベロープを使用する。
 * - [厳守] 送信payloadは `signature` を除いたJSONを HMAC-SHA256(k-device) で署名する。
 * - [厳守] 証明書更新先は `/certs/mqtt-ca.pem` 固定とし、`targetArea=certs` のみ扱う。
 * - [重要] `fileSyncStatus` 通知の `phase/result/errorCode` を監視し、失敗理由を詳細表示する。
 *
 * 制限事項:
 * - ESP32 側が `fileSync*` と strict payload を実装済みであること。
 * - LocalServer の管理者API (`/api/admin/auth/login`, `/api/admin/keys/k-device/issue`) が有効であること。
 */

import fs from "fs";
import path from "path";
import crypto from "crypto";
import mqtt from "mqtt";
import { fileURLToPath } from "url";
import { loadConfig } from "../dist/config.js";
import { keyService } from "../dist/keyService.js";
import { mqttPayloadSecurityService, resolveMqttPayloadEncryptionMode } from "../dist/mqttPayloadSecurity.js";

const scriptFilePath = fileURLToPath(import.meta.url);
const scriptDirectoryPath = path.dirname(scriptFilePath);

/**
 * @typedef {{
 *   targetName: string;
 *   certPath: string;
 *   timeoutMs: number;
 * }} argumentSet
 */

/**
 * @description `--key value` 形式の引数を辞書化する。
 * @param {string[]} argv 引数配列。
 * @returns {Map<string, string>} 引数辞書。
 */
function parseArgumentMap(argv) {
  const argMap = new Map();
  for (let index = 0; index < argv.length; index += 1) {
    const current = argv[index];
    if (!current.startsWith("--")) {
      continue;
    }
    const key = current.slice(2);
    const value = argv[index + 1] ?? "";
    argMap.set(key, value);
    index += 1;
  }
  return argMap;
}

/**
 * @description 必須引数を取得する。
 * @param {Map<string, string>} argMap 引数辞書。
 * @param {string} key キー名。
 * @returns {string} 値。
 */
function requireArg(argMap, key) {
  const value = (argMap.get(key) || "").trim();
  if (value.length === 0) {
    throw new Error(`requireArg failed. --${key} is required.`);
  }
  return value;
}

/**
 * @description 管理者ログインしてトークンを取得する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @returns {Promise<string>} トークン。
 */
async function loginAdmin(config) {
  const response = await fetch(`http://127.0.0.1:${config.httpPort}/api/admin/auth/login`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      username: config.adminUsername,
      password: config.adminPassword
    })
  });
  const responseJson = await response.json();
  if (!response.ok || String(responseJson?.result || "NG") !== "OK") {
    throw new Error(`loginAdmin failed. status=${response.status} detail=${String(responseJson?.detail ?? "")}`);
  }
  const token = String(responseJson?.token ?? "");
  if (token.length === 0) {
    throw new Error("loginAdmin failed. token is empty.");
  }
  return token;
}

/**
 * @description `k-device` を再発行して対象ESP32へ投入する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {string} adminToken 管理者トークン。
 * @param {string} targetName デバイス名。
 */
async function issueAndPushKDevice(config, adminToken, targetName) {
  const response = await fetch(`http://127.0.0.1:${config.httpPort}/api/admin/keys/k-device/issue`, {
    method: "POST",
    headers: {
      "Authorization": `Bearer ${adminToken}`,
      "Content-Type": "application/json"
    },
    body: JSON.stringify({ targetDeviceName: targetName })
  });
  if (!response.ok) {
    const responseText = await response.text();
    throw new Error(`issueAndPushKDevice failed. status=${response.status} detail=${responseText}`);
  }
}

/**
 * @description 署名付きMQTTコマンドを構築する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {string} targetName デバイス名。
 * @param {string} subCommand サブコマンド。
 * @param {Record<string, unknown>} args 引数。
 * @param {Buffer} keyBuffer k-deviceバイト列。
 * @returns {Record<string, unknown>} 署名付きpayload。
 */
function buildSignedCommand(config, targetName, subCommand, args, keyBuffer) {
  const unsignedPayload = {
    v: "1",
    DstID: targetName,
    SrcID: config.sourceId,
    id: `${subCommand}-${Date.now()}-${Math.floor(Math.random() * 100000)}`,
    ts: new Date().toISOString(),
    op: "call",
    sub: subCommand,
    args
  };
  const normalizedPayload = JSON.stringify(unsignedPayload);
  const signature = crypto.createHmac("sha256", keyBuffer).update(normalizedPayload).digest("base64");
  return {
    ...unsignedPayload,
    signature
  };
}

/**
 * @description ファイル同期用セッションを実行する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {argumentSet} args 引数。
 * @param {string} keyDeviceBase64 k-device(base64)。
 * @returns {Promise<{sessionId:string, completedMessage:Record<string, unknown>}>} 実行結果。
 */
function runFileSyncSession(config, args, keyDeviceBase64) {
  return new Promise((resolve, reject) => {
    const keyBuffer = Buffer.from(keyDeviceBase64, "base64");
    if (keyBuffer.length !== 32) {
      reject(new Error(`runFileSyncSession failed. k-device length must be 32. actual=${keyBuffer.length}`));
      return;
    }
    const localKeyService = new keyService(config);
    const encryptionMode = resolveMqttPayloadEncryptionMode();
    const payloadSecurity = new mqttPayloadSecurityService(localKeyService, encryptionMode);
    const certText = fs.readFileSync(args.certPath, "utf8");
    const certBytes = Buffer.from(certText, "utf8");
    const certSha256 = crypto.createHash("sha256").update(certBytes).digest("hex");
    const certBase64 = certBytes.toString("base64");
    const sessionId = `filesync-cert-${Date.now()}`;

    const planPayload = buildSignedCommand(
      config,
      args.targetName,
      "fileSyncPlan",
      {
        sessionId,
        targetArea: "certs",
        deleteMode: "none",
        files: [
          {
            path: "/certs/mqtt-ca.pem",
            action: "upsert",
            size: certBytes.length,
            sha256: certSha256
          }
        ]
      },
      keyBuffer
    );
    const chunkPayload = buildSignedCommand(
      config,
      args.targetName,
      "fileSyncChunk",
      {
        sessionId,
        targetArea: "certs",
        path: "/certs/mqtt-ca.pem",
        chunkIndex: 0,
        chunkCount: 1,
        dataBase64: certBase64,
        isLast: true
      },
      keyBuffer
    );
    const commitPayload = buildSignedCommand(
      config,
      args.targetName,
      "fileSyncCommit",
      {
        sessionId,
        targetArea: "certs",
        deleteMode: "none"
      },
      keyBuffer
    );

    const statusTopic = "esp32lab/notice/fileSyncStatus/+";
    /** @type {"waitingPlan"|"waitingChunk"|"waitingCommit"|"completed"} */
    let state = "waitingPlan";
    let timeoutHandle = null;

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

    const cleanup = () => {
      if (timeoutHandle !== null) {
        clearTimeout(timeoutHandle);
      }
      client.end(true);
    };

    const publishEncrypted = (subCommand, payloadObject) => {
      const callTopic = `esp32lab/call/${subCommand}/${args.targetName}`;
      const encryptedPayload = payloadSecurity.encodeOutgoingPayload(args.targetName, JSON.stringify(payloadObject));
      client.publish(callTopic, encryptedPayload, { qos: 1, retain: false }, (publishError) => {
        if (publishError) {
          cleanup();
          reject(new Error(`publishEncrypted failed. subCommand=${subCommand} topic=${callTopic} detail=${publishError.message}`));
        }
      });
    };

    timeoutHandle = setTimeout(() => {
      cleanup();
      reject(new Error(`runFileSyncSession timeout. state=${state} sessionId=${sessionId} timeoutMs=${args.timeoutMs}`));
    }, args.timeoutMs);

    client.on("error", (error) => {
      cleanup();
      reject(new Error(`runFileSyncSession mqtt error. message=${error.message}`));
    });

    client.on("connect", () => {
      client.subscribe(statusTopic, { qos: 1 }, (subscribeError) => {
        if (subscribeError) {
          cleanup();
          reject(new Error(`runFileSyncSession subscribe failed. topic=${statusTopic} detail=${subscribeError.message}`));
          return;
        }
        publishEncrypted("fileSyncPlan", planPayload);
      });
    });

    client.on("message", (topic, payloadBuffer) => {
      if (!topic.startsWith("esp32lab/notice/fileSyncStatus/")) {
        return;
      }
      let decoded;
      try {
        decoded = payloadSecurity.decodeIncomingPayload(args.targetName, payloadBuffer.toString("utf8"));
      } catch {
        return;
      }
      let json;
      try {
        json = JSON.parse(decoded.plainPayloadText);
      } catch {
        return;
      }
      if (String(json?.sessionId ?? "") !== sessionId) {
        return;
      }
      const phase = String(json?.phase ?? "");
      const result = String(json?.result ?? "");
      if (result === "NG" || phase === "failed") {
        cleanup();
        reject(
          new Error(
            `runFileSyncSession failed status. state=${state} phase=${phase} result=${result} errorCode=${String(json?.errorCode ?? "")} detail=${String(json?.detail ?? "")}`
          )
        );
        return;
      }

      if (state === "waitingPlan" && phase === "planning" && result === "OK") {
        state = "waitingChunk";
        publishEncrypted("fileSyncChunk", chunkPayload);
        return;
      }
      if (state === "waitingChunk" && phase === "receiving" && result === "OK") {
        state = "waitingCommit";
        publishEncrypted("fileSyncCommit", commitPayload);
        return;
      }
      if (state === "waitingCommit" && phase === "completed" && result === "OK") {
        state = "completed";
        cleanup();
        resolve({ sessionId, completedMessage: json });
      }
    });
  });
}

/**
 * @description エントリポイント。
 */
async function main() {
  const argMap = parseArgumentMap(process.argv.slice(2));
  /** @type {argumentSet} */
  const args = {
    targetName: requireArg(argMap, "targetName"),
    certPath: path.resolve(argMap.get("certPath") || path.resolve(scriptDirectoryPath, "../../ESP32/src/MQTT/ca.crt")),
    timeoutMs: Number(argMap.get("timeoutMs") || "120000")
  };
  if (!fs.existsSync(args.certPath)) {
    throw new Error(`main failed. certPath does not exist. certPath=${args.certPath}`);
  }
  if (!Number.isFinite(args.timeoutMs) || args.timeoutMs <= 0) {
    throw new Error(`main failed. timeoutMs is invalid. value=${argMap.get("timeoutMs")}`);
  }

  const config = loadConfig();
  const adminToken = await loginAdmin(config);
  await issueAndPushKDevice(config, adminToken, args.targetName);
  const localKeyService = new keyService(config);
  const keyDeviceBase64 = localKeyService.getKDeviceBase64(args.targetName);
  if ((keyDeviceBase64 || "").trim().length === 0) {
    throw new Error(`main failed. k-device is empty. targetName=${args.targetName}`);
  }

  const result = await runFileSyncSession(config, args, keyDeviceBase64);
  console.log(
    `[OK] fileSync cert push completed. sessionId=${result.sessionId} phase=${String(result.completedMessage.phase || "")} detail=${String(result.completedMessage.detail || "")}`
  );
}

main().catch((error) => {
  const message = error instanceof Error ? error.message : String(error);
  console.error(`[ERROR] ${message}`);
  process.exit(1);
});
