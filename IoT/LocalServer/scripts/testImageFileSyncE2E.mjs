/**
 * @file testImageFileSyncE2E.mjs
 * @description 画像を ZIP 化せずに `fileSyncPlan` / `fileSyncChunk` / `fileSyncCommit` で1ファイルずつ更新する E2E スクリプト。
 *
 * 主仕様:
 * - [重要] 画像更新は `targetArea=images` で実行し、`destinationDir` 配下へ 1 ファイルずつ反映する。
 * - [厳守] 署名対象は `signature` を除いた JSON 全体とし、`HMAC-SHA256(k-device)` で検証可能な形式で送信する。
 * - [重要] ファイルサイズが `splitThresholdBytes`（既定 100KB）以上の場合は分割送信する。
 * - [重要] 1チャンクの送信サイズは MQTT パケット上限（既定 4096byte）を超えないよう動的に調整する。
 * - [禁止] TLS 検証を無効化する暫定手段（`rejectUnauthorized=false`）は使用しない。
 *
 * 制限事項:
 * - ESP32 側で `fileSync*` / strict payload / HMAC 署名検証が有効であること。
 * - LocalServer の `dist` が生成済みであり、`config` / `keyService` / `mqttPayloadSecurity` を import 可能であること。
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
 *   imagesDir: string;
 *   destinationDir: string;
 *   timeoutMs: number;
 *   splitThresholdBytes: number;
 *   chunkSizeBytes: number;
 *   mqttPacketSizeBytes: number;
 *   mqttPacketReserveBytes: number;
 *   abnormalMode: string;
 * }} argumentSet
 */

/**
 * @typedef {{
 *   localFilePath: string;
 *   remoteFilePath: string;
 *   size: number;
 *   sha256: string;
 *   fileBytes: Buffer;
 * }} filePlan
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
 * @description 先頭 `/` を維持した POSIX パスへ正規化する。
 * @param {string} inputPath 入力パス。
 * @returns {string} 正規化パス。
 */
function normalizePosixPath(inputPath) {
  const replaced = inputPath.replace(/\\/g, "/");
  const normalized = path.posix.normalize(replaced);
  return normalized.startsWith("/") ? normalized : `/${normalized}`;
}

/**
 * @description 画像ディレクトリ配下の全ファイルを列挙する。
 * @param {string} rootDir ルートディレクトリ。
 * @returns {string[]} ファイル一覧（絶対パス）。
 */
function listFilesRecursively(rootDir) {
  /** @type {string[]} */
  const result = [];
  /** @param {string} currentDir */
  const walk = (currentDir) => {
    const entries = fs.readdirSync(currentDir, { withFileTypes: true });
    for (const entry of entries) {
      const entryPath = path.join(currentDir, entry.name);
      if (entry.isDirectory()) {
        walk(entryPath);
        continue;
      }
      if (entry.isFile()) {
        result.push(entryPath);
      }
    }
  };
  walk(rootDir);
  return result;
}

/**
 * @description 送信対象とする画像ファイルか判定する。
 * @param {string} filePath ファイルパス。
 * @returns {boolean} 画像として送信対象なら true。
 */
function isImageTargetFile(filePath) {
  const extension = path.extname(filePath).toLowerCase();
  if (extension === ".zip") {
    return false;
  }
  return [".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".svg"].includes(extension);
}

/**
 * @description 画像ルートディレクトリを解決する。
 * @param {Map<string, string>} argMap 引数辞書。
 * @returns {string} 解決済みディレクトリ。
 */
function resolveImagesDir(argMap) {
  const explicitImagesDir = (argMap.get("imagesDir") || "").trim();
  if (explicitImagesDir.length > 0) {
    return path.resolve(explicitImagesDir);
  }
  const preferredDir = path.resolve(scriptDirectoryPath, "../public/assets/images");
  if (fs.existsSync(preferredDir) && fs.statSync(preferredDir).isDirectory()) {
    return preferredDir;
  }
  // [重要] `public/assets/images` が未作成でも、既存の `public/assets` 直下画像で実行可能にする。
  return path.resolve(scriptDirectoryPath, "../public/assets");
}

/**
 * @description 異常系モード文字列を正規化する。
 * @param {string} modeText 入力モード文字列。
 * @returns {"none"|"shaMismatch"|"chunkMissing"} 正規化モード。
 */
function normalizeAbnormalMode(modeText) {
  const normalized = String(modeText || "none").trim().toLowerCase();
  if (normalized === "shamismatch") {
    return "shaMismatch";
  }
  if (normalized === "chunkmissing") {
    return "chunkMissing";
  }
  return "none";
}

/**
 * @description 1ファイル分の送信計画を作成する。
 * @param {string} localFilePath ローカルファイル絶対パス。
 * @param {string} imagesDir 画像ルートディレクトリ。
 * @param {string} destinationDir ESP32 側の配置先ルート（`/images/...`）。
 * @returns {filePlan} 送信計画。
 */
function buildFilePlan(localFilePath, imagesDir, destinationDir) {
  const fileBytes = fs.readFileSync(localFilePath);
  const fileSha256 = crypto.createHash("sha256").update(fileBytes).digest("hex");
  const relativePath = path.relative(imagesDir, localFilePath).replace(/\\/g, "/");
  const remoteFilePath = normalizePosixPath(path.posix.join(destinationDir, relativePath));

  return {
    localFilePath,
    remoteFilePath,
    size: fileBytes.length,
    sha256: fileSha256,
    fileBytes
  };
}

/**
 * @description `fileSyncChunk` 1件を送信したときの推定MQTTパケットサイズを算出する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {argumentSet} args 引数。
 * @param {mqttPayloadSecurityService} payloadSecurity 暗号化サービス。
 * @param {Buffer} keyBuffer `k-device`。
 * @param {string} sessionId セッションID。
 * @param {string} remoteFilePath 配置先パス。
 * @param {number} chunkIndex チャンク番号。
 * @param {number} chunkCount チャンク総数（推定可）。
 * @param {string} dataBase64 チャンクbase64。
 * @param {boolean} isLast 最終チャンクか。
 * @returns {number} 推定MQTTパケットサイズ（byte）。
 */
function estimateChunkPacketSize(
  config,
  args,
  payloadSecurity,
  keyBuffer,
  sessionId,
  remoteFilePath,
  chunkIndex,
  chunkCount,
  dataBase64,
  isLast
) {
  const chunkPayload = buildSignedCommand(
    config,
    args.targetName,
    "fileSyncChunk",
    {
      sessionId,
      targetArea: "images",
      path: remoteFilePath,
      chunkIndex,
      chunkCount,
      dataBase64,
      isLast
    },
    keyBuffer
  );
  const encodedPayload = payloadSecurity.encodeOutgoingPayload(args.targetName, JSON.stringify(chunkPayload));
  const topicText = `esp32lab/call/fileSyncChunk/${args.targetName}`;
  const encodedPayloadBytes = Buffer.byteLength(encodedPayload, "utf8");
  const topicBytes = Buffer.byteLength(topicText, "utf8");
  return encodedPayloadBytes + topicBytes + args.mqttPacketReserveBytes;
}

/**
 * @description MQTT送信サイズ上限を超えないようチャンクキューを生成する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {argumentSet} args 引数。
 * @param {mqttPayloadSecurityService} payloadSecurity 暗号化サービス。
 * @param {Buffer} keyBuffer `k-device`。
 * @param {string} sessionId セッションID。
 * @param {filePlan[]} plans ファイル計画一覧。
 * @returns {Array<{path:string, chunkIndex:number, chunkCount:number, dataBase64:string, isLast:boolean}>} 送信キュー。
 */
function buildAdaptiveChunkQueue(config, args, payloadSecurity, keyBuffer, sessionId, plans) {
  /** @type {Array<{path:string, chunkIndex:number, chunkCount:number, dataBase64:string, isLast:boolean}>} */
  const chunkQueue = [];
  for (const plan of plans) {
    const shouldSplit = plan.size >= args.splitThresholdBytes;
    const preferredChunkBytes = shouldSplit ? Math.max(1, Math.floor(args.chunkSizeBytes)) : plan.size;
    let offset = 0;
    let chunkIndex = 0;
    const temporaryChunkCount = 99999;

    /** @type {Array<{dataBase64:string}>} */
    const perFileChunks = [];
    while (offset < plan.fileBytes.length) {
      let currentChunkBytes = Math.min(preferredChunkBytes, plan.fileBytes.length - offset);
      let acceptedChunkBase64 = "";
      while (currentChunkBytes > 0) {
        const endOffset = offset + currentChunkBytes;
        const dataBase64 = plan.fileBytes.subarray(offset, endOffset).toString("base64");
        const estimatedPacketSize = estimateChunkPacketSize(
          config,
          args,
          payloadSecurity,
          keyBuffer,
          sessionId,
          plan.remoteFilePath,
          chunkIndex,
          temporaryChunkCount,
          dataBase64,
          false
        );
        if (estimatedPacketSize <= args.mqttPacketSizeBytes) {
          acceptedChunkBase64 = dataBase64;
          break;
        }
        currentChunkBytes = Math.floor(currentChunkBytes * 0.75);
      }
      if (acceptedChunkBase64.length === 0) {
        throw new Error(
          `buildAdaptiveChunkQueue failed. chunk does not fit mqtt packet limit. file=${plan.remoteFilePath} mqttPacketSizeBytes=${args.mqttPacketSizeBytes}`
        );
      }
      perFileChunks.push({ dataBase64: acceptedChunkBase64 });
      offset += Buffer.from(acceptedChunkBase64, "base64").length;
      chunkIndex += 1;
    }

    const fileChunkCount = perFileChunks.length;
    for (let fileChunkIndex = 0; fileChunkIndex < fileChunkCount; fileChunkIndex += 1) {
      chunkQueue.push({
        path: plan.remoteFilePath,
        chunkIndex: fileChunkIndex,
        chunkCount: fileChunkCount,
        dataBase64: perFileChunks[fileChunkIndex].dataBase64,
        isLast: fileChunkIndex === fileChunkCount - 1
      });
    }
  }
  return chunkQueue;
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
 * @description 署名付き MQTT コマンドを構築する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {string} targetName デバイス名。
 * @param {string} subCommand サブコマンド。
 * @param {Record<string, unknown>} args 引数オブジェクト。
 * @param {Buffer} keyBuffer `k-device` バイト列。
 * @returns {Record<string, unknown>} 署名付き payload。
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
 * @description fileSync セッションを実行する。
 * @param {ReturnType<typeof loadConfig>} config 設定。
 * @param {argumentSet} args 引数。
 * @param {string} keyDeviceBase64 `k-device` base64。
 * @param {filePlan[]} plans ファイル更新計画。
 * @returns {Promise<{sessionId: string, completedMessage: Record<string, unknown>, totalChunkCount: number}>} 結果。
 */
function runFileSyncSession(config, args, keyDeviceBase64, plans) {
  return new Promise((resolve, reject) => {
    const keyBuffer = Buffer.from(keyDeviceBase64, "base64");
    if (keyBuffer.length !== 32) {
      reject(new Error(`runFileSyncSession failed. k-device length must be 32. actual=${keyBuffer.length}`));
      return;
    }
    const localKeyService = new keyService(config);
    const encryptionMode = resolveMqttPayloadEncryptionMode();
    const payloadSecurity = new mqttPayloadSecurityService(localKeyService, encryptionMode);
    const sessionId = `filesync-image-${Date.now()}`;
    const filesForPlan = plans.map((plan, index) => {
      let plannedSha256 = plan.sha256;
      if (args.abnormalMode === "shaMismatch" && index === 0) {
        // [重要] 異常系試験: 最初の1ファイルだけ期待SHAを故意に不一致へする。
        plannedSha256 = "00".repeat(32);
      }
      return {
        path: plan.remoteFilePath,
        action: "upsert",
        size: plan.size,
        sha256: plannedSha256
      };
    });

    const planPayload = buildSignedCommand(
      config,
      args.targetName,
      "fileSyncPlan",
      {
        sessionId,
        targetArea: "images",
        deleteMode: "none",
        files: filesForPlan
      },
      keyBuffer
    );

    const chunkQueue = buildAdaptiveChunkQueue(config, args, payloadSecurity, keyBuffer, sessionId, plans);
    if (args.abnormalMode === "chunkMissing" && chunkQueue.length > 0) {
      // [重要] 異常系試験: 最終チャンクを意図的に送らず、commit時に整合失敗を発生させる。
      chunkQueue.pop();
    }
    const totalChunkCount = chunkQueue.length;

    const commitPayload = buildSignedCommand(
      config,
      args.targetName,
      "fileSyncCommit",
      {
        sessionId,
        targetArea: "images",
        deleteMode: "none"
      },
      keyBuffer
    );

    /** @type {"waitingPlan"|"sendingChunks"|"waitingCommit"|"completed"} */
    let state = "waitingPlan";
    let ackedChunkCount = 0;
    let nextChunkOffset = 0;
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
    const statusTopic = "esp32lab/notice/fileSyncStatus/+";

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

    const publishNextChunk = () => {
      if (nextChunkOffset >= chunkQueue.length) {
        state = "waitingCommit";
        publishEncrypted("fileSyncCommit", commitPayload);
        return;
      }
      const chunk = chunkQueue[nextChunkOffset];
      nextChunkOffset += 1;
      const chunkPayload = buildSignedCommand(
        config,
        args.targetName,
        "fileSyncChunk",
        {
          sessionId,
          targetArea: "images",
          path: chunk.path,
          chunkIndex: chunk.chunkIndex,
          chunkCount: chunk.chunkCount,
          dataBase64: chunk.dataBase64,
          isLast: chunk.isLast
        },
        keyBuffer
      );
      publishEncrypted("fileSyncChunk", chunkPayload);
    };

    timeoutHandle = setTimeout(() => {
      cleanup();
      reject(
        new Error(
          `runFileSyncSession timeout. state=${state} sessionId=${sessionId} totalChunks=${totalChunkCount} ackedChunks=${ackedChunkCount} timeoutMs=${args.timeoutMs}`
        )
      );
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
        state = "sendingChunks";
        publishNextChunk();
        return;
      }

      if (state === "sendingChunks" && phase === "receiving" && result === "OK") {
        ackedChunkCount += 1;
        publishNextChunk();
        return;
      }

      if (state === "waitingCommit" && phase === "completed" && result === "OK") {
        state = "completed";
        cleanup();
        resolve({ sessionId, completedMessage: json, totalChunkCount });
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
    imagesDir: resolveImagesDir(argMap),
    destinationDir: normalizePosixPath(argMap.get("destinationDir") || "/images/top"),
    timeoutMs: Number(argMap.get("timeoutMs") || "180000"),
    splitThresholdBytes: Number(argMap.get("splitThresholdBytes") || "102400"),
    chunkSizeBytes: Number(argMap.get("chunkSizeBytes") || "102400"),
    mqttPacketSizeBytes: Number(argMap.get("mqttPacketSizeBytes") || "4096"),
    mqttPacketReserveBytes: Number(argMap.get("mqttPacketReserveBytes") || "80"),
    abnormalMode: normalizeAbnormalMode(argMap.get("abnormalMode") || "none")
  };

  if (!fs.existsSync(args.imagesDir) || !fs.statSync(args.imagesDir).isDirectory()) {
    throw new Error(`main failed. imagesDir must be existing directory. imagesDir=${args.imagesDir}`);
  }
  if (!Number.isFinite(args.timeoutMs) || args.timeoutMs <= 0) {
    throw new Error(`main failed. timeoutMs is invalid. value=${argMap.get("timeoutMs")}`);
  }
  if (!Number.isFinite(args.splitThresholdBytes) || args.splitThresholdBytes <= 0) {
    throw new Error(`main failed. splitThresholdBytes is invalid. value=${argMap.get("splitThresholdBytes")}`);
  }
  if (!Number.isFinite(args.chunkSizeBytes) || args.chunkSizeBytes <= 0) {
    throw new Error(`main failed. chunkSizeBytes is invalid. value=${argMap.get("chunkSizeBytes")}`);
  }
  if (!Number.isFinite(args.mqttPacketSizeBytes) || args.mqttPacketSizeBytes < 1024) {
    throw new Error(`main failed. mqttPacketSizeBytes is invalid. value=${argMap.get("mqttPacketSizeBytes")}`);
  }
  if (!Number.isFinite(args.mqttPacketReserveBytes) || args.mqttPacketReserveBytes < 16) {
    throw new Error(`main failed. mqttPacketReserveBytes is invalid. value=${argMap.get("mqttPacketReserveBytes")}`);
  }
  if (!args.destinationDir.startsWith("/images")) {
    throw new Error(`main failed. destinationDir must be under /images. destinationDir=${args.destinationDir}`);
  }

  const localFilePaths = listFilesRecursively(args.imagesDir);
  const imageFilePaths = localFilePaths.filter((filePath) => isImageTargetFile(filePath));
  if (imageFilePaths.length === 0) {
    throw new Error(`main failed. no image files found under imagesDir. imagesDir=${args.imagesDir}`);
  }
  const filePlans = imageFilePaths.map((localFilePath) => buildFilePlan(localFilePath, args.imagesDir, args.destinationDir));

  const config = loadConfig();
  const adminToken = await loginAdmin(config);
  await issueAndPushKDevice(config, adminToken, args.targetName);
  const localKeyService = new keyService(config);
  const keyDeviceBase64 = localKeyService.getKDeviceBase64(args.targetName);
  if ((keyDeviceBase64 || "").trim().length === 0) {
    throw new Error(`main failed. k-device is empty. targetName=${args.targetName}`);
  }

  const result = await runFileSyncSession(config, args, keyDeviceBase64, filePlans);
  const splitFileCount = filePlans.filter((plan) => plan.size >= args.splitThresholdBytes).length;
  console.log(
    `[OK] fileSync image update completed. sessionId=${result.sessionId} files=${filePlans.length} splitFiles=${splitFileCount} totalChunks=${result.totalChunkCount} abnormalMode=${args.abnormalMode} splitThresholdBytes=${args.splitThresholdBytes} chunkSizeBytes=${args.chunkSizeBytes} mqttPacketSizeBytes=${args.mqttPacketSizeBytes} phase=${String(result.completedMessage.phase || "")} detail=${String(result.completedMessage.detail || "")}`
  );
}

main().catch((error) => {
  const message = error instanceof Error ? error.message : String(error);
  console.error(`[ERROR] ${message}`);
  process.exit(1);
});
