/**
 * @file migrateKUserToSecretCore.mjs
 * @description TS版 keyStore.json から k-user を抽出し、SecretCore へ import_k_user で送信するマイグレーションスクリプト。
 * @remarks
 * - [重要] 本スクリプトは Phase2 移行時に1回だけ実行する。
 * - [厳守] k-user の raw 値はコンソールに表示しない（fingerprintのみ表示）。
 * - [厳守] 実行前に LocalServer を停止し、SecretCore が起動中であること。
 * - 変更日: 2026-03-15 新規作成。理由: TS版 k-user を SecretCore(Rust) へ移植するため。
 *
 * 使用方法:
 *   cd IoT/LocalServer
 *   node scripts/migrateKUserToSecretCore.mjs
 */

import crypto from "crypto";
import fs from "fs";
import net from "net";
import path from "path";
import { fileURLToPath } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const projectRoot = path.resolve(__dirname, "..");

const PIPE_NAME = "\\\\.\\pipe\\iot-secret-core-ipc";

/**
 * @description .env ファイルから環境変数を読み込む。
 * @param {string} envFilePath .envファイルパス。
 * @returns {Record<string, string>} キーバリュー。
 */
function loadEnvFile(envFilePath) {
  const envText = fs.readFileSync(envFilePath, "utf8");
  /** @type {Record<string, string>} */
  const envMap = {};
  for (const line of envText.split("\n")) {
    const trimmedLine = line.trim();
    if (trimmedLine.length === 0 || trimmedLine.startsWith("#")) continue;
    const equalsIndex = trimmedLine.indexOf("=");
    if (equalsIndex < 0) continue;
    const key = trimmedLine.slice(0, equalsIndex).trim();
    const value = trimmedLine.slice(equalsIndex + 1).trim();
    envMap[key] = value;
  }
  return envMap;
}

/**
 * @description TS版 keyStore.json を復号して k-user (Base64) を返す。
 * @param {string} kUserAppIdentifier アプリ識別子。
 * @param {string} sourceId ソースID。
 * @param {string} keyStoreFilePath keyStore.jsonパス。
 * @returns {string} k-user の Base64文字列。
 */
function extractKUserBase64(kUserAppIdentifier, sourceId, keyStoreFilePath) {
  if (!fs.existsSync(keyStoreFilePath)) {
    throw new Error(`keyStore.json が見つかりません: ${keyStoreFilePath}`);
  }

  const fileText = fs.readFileSync(keyStoreFilePath, "utf8");
  const envelope = JSON.parse(fileText);
  const encPayload = envelope.encryptedPayload;

  const saltText = `localserver-key-store:${sourceId}`;
  const masterKey = crypto.scryptSync(kUserAppIdentifier, saltText, 32);

  const ivBuffer = Buffer.from(encPayload.ivBase64, "base64");
  const tagBuffer = Buffer.from(encPayload.tagBase64, "base64");
  const cipherBuffer = Buffer.from(encPayload.cipherBase64, "base64");

  const decipher = crypto.createDecipheriv("aes-256-gcm", masterKey, ivBuffer);
  decipher.setAuthTag(tagBuffer);
  const plainBuffer = Buffer.concat([decipher.update(cipherBuffer), decipher.final()]);
  const payload = JSON.parse(plainBuffer.toString("utf8"));

  if (!payload.kUserBase64 || payload.kUserBase64.length === 0) {
    throw new Error("keyStore.json に k-user が含まれていません（未発行状態）。");
  }

  return payload.kUserBase64;
}

/**
 * @description SecretCore IPC へリクエストを送信し、レスポンスを返す。
 * @param {string} command コマンド名。
 * @param {object|undefined} payload ペイロード。
 * @returns {Promise<any>} レスポンスデータ。
 */
function sendSecretCoreRequest(command, payload) {
  return new Promise((resolve, reject) => {
    const client = net.createConnection(PIPE_NAME, () => {
      const req = { command, payload: payload || null };
      client.write(JSON.stringify(req));
    });
    let responseData = "";
    client.on("data", (chunk) => {
      responseData += chunk.toString();
      client.end();
    });
    client.on("end", () => {
      try {
        const parsed = JSON.parse(responseData);
        if (parsed.status === "ok") {
          resolve(parsed.data);
        } else {
          reject(new Error(`IPC Error: ${parsed.error}`));
        }
      } catch (e) {
        reject(new Error(`IPC レスポンスの解析に失敗: ${e}`));
      }
    });
    client.on("error", (err) => {
      reject(new Error(`IPC 接続エラー: ${err.message}`));
    });
  });
}

async function main() {
  console.log("=== k-user マイグレーション: TS版 → SecretCore ===");
  console.log("");

  const envFilePath = path.join(projectRoot, ".env");
  if (!fs.existsSync(envFilePath)) {
    console.error(`ERROR: .env ファイルが見つかりません: ${envFilePath}`);
    process.exit(1);
  }

  const envMap = loadEnvFile(envFilePath);
  const kUserAppIdentifier = envMap["K_USER_APP_IDENTIFIER"];
  const sourceId = envMap["LOCAL_SERVER_SOURCE_ID"];

  if (!kUserAppIdentifier || !sourceId) {
    console.error("ERROR: .env に K_USER_APP_IDENTIFIER または LOCAL_SERVER_SOURCE_ID が未定義です。");
    process.exit(1);
  }

  console.log(`sourceId: ${sourceId}`);
  console.log(`kUserAppIdentifier: ${kUserAppIdentifier.slice(0, 4)}****`);
  console.log("");

  const keyStoreFilePath = path.join(projectRoot, "data", "keyStore.json");
  console.log("Step 1: TS版 keyStore.json から k-user を抽出中...");

  let kUserBase64;
  try {
    kUserBase64 = extractKUserBase64(kUserAppIdentifier, sourceId, keyStoreFilePath);
  } catch (extractError) {
    console.error(`ERROR: k-user 抽出失敗: ${extractError.message}`);
    process.exit(1);
  }

  const kUserBuffer = Buffer.from(kUserBase64, "base64");
  const fingerprint = crypto.createHash("sha256").update(kUserBuffer).digest("hex").slice(0, 16);
  console.log(`  k-user 抽出成功。length=${kUserBuffer.length}bytes fingerprint=${fingerprint}`);
  console.log("");

  console.log("Step 2: SecretCore health check...");
  try {
    const healthResult = await sendSecretCoreRequest("health");
    console.log(`  SecretCore 応答OK。version=${healthResult?.version || "unknown"}`);
  } catch (healthError) {
    console.error(`ERROR: SecretCore に接続できません: ${healthError.message}`);
    console.error("SecretCore が起動中であることを確認してください。");
    process.exit(1);
  }
  console.log("");

  console.log("Step 3: SecretCore へ import_k_user を送信中...");
  try {
    const importResult = await sendSecretCoreRequest("import_k_user", { kUserBase64 });
    console.log(`  インポート成功。`, importResult);
  } catch (importError) {
    console.error(`ERROR: import_k_user 失敗: ${importError.message}`);
    process.exit(1);
  }
  console.log("");

  console.log("Step 4: 検証 - SecretCore の k-device が TS版と一致するか確認...");
  const testDeviceName = "IoT_F0D0F94EB580";
  const tsKDeviceBuffer = crypto.createHmac("sha256", kUserBuffer).update(testDeviceName).digest();
  const tsKDeviceBase64 = tsKDeviceBuffer.toString("base64");
  const tsKDeviceFingerprint = crypto.createHash("sha256").update(tsKDeviceBuffer).digest("hex").slice(0, 16);

  try {
    const scResult = await sendSecretCoreRequest("get_k_device", { targetDeviceName: testDeviceName });
    const scKDeviceBase64 = scResult?.keyDeviceBase64;
    if (scKDeviceBase64 === tsKDeviceBase64) {
      console.log(`  検証成功! device=${testDeviceName}`);
      console.log(`  TS版  k-device fingerprint: ${tsKDeviceFingerprint}`);
      console.log(`  Rust版 k-device fingerprint: ${crypto.createHash("sha256").update(Buffer.from(scKDeviceBase64, "base64")).digest("hex").slice(0, 16)}`);
    } else {
      console.error(`  検証失敗! k-device が一致しません。`);
      console.error(`  TS版  fingerprint: ${tsKDeviceFingerprint}`);
      const scFp = crypto.createHash("sha256").update(Buffer.from(scKDeviceBase64, "base64")).digest("hex").slice(0, 16);
      console.error(`  Rust版 fingerprint: ${scFp}`);
      process.exit(1);
    }
  } catch (verifyError) {
    console.error(`ERROR: k-device 検証失敗: ${verifyError.message}`);
    process.exit(1);
  }
  console.log("");

  console.log("=== マイグレーション完了 ===");
  console.log("server.ts の USE_SECRET_CORE を true に戻して LocalServer を再起動してください。");
}

main().catch((err) => {
  console.error(`予期しないエラー: ${err.message}`);
  process.exit(1);
});
