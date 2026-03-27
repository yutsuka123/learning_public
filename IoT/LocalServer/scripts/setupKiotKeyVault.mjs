/**
 * @file setupKiotKeyVault.mjs
 * @description
 * `k-iot` 鍵素材を生成し、`ESP32/data/sensitiveData.json` の `kiotKeyVault` へ
 * AES-256-GCM 暗号化データとして保存する初期化スクリプト。
 *
 * 主な仕様:
 * - FE鍵(32byte), NVS HMAC鍵(32byte), SB署名鍵PEM(RSA3072)を生成する。
 * - 平文鍵は最終的にプロジェクトへ残さない（tmpを削除）。
 * - 復号鍵(`KIOT_KEY_VAULT_MASTER_KEY_B64`)が未設定なら新規生成して
 *   `LocalServer/.env` へ追記する。
 * - 復号実行は `unlockKiotKeyVault.mjs` が担当する。
 *
 * 制限事項:
 * - [重要] 本方式は「暗号文と解錠情報を同一プロジェクトに置く」開発/検証向け運用。
 * - [禁止] 本番恒久運用で同一方式をそのまま採用しない。
 * - [厳守] `.env` / `sensitiveData.json` は Git にコミットしない。
 */

import crypto from "crypto";
import fs from "fs";
import os from "os";
import path from "path";
import { spawnSync } from "child_process";
import { fileURLToPath } from "url";

const scriptFilePath = fileURLToPath(import.meta.url);
const localServerRootPath = path.resolve(path.dirname(scriptFilePath), "..");
const projectRootPath = path.resolve(localServerRootPath, "..");
const esp32SensitiveDataPath = path.resolve(projectRootPath, "ESP32", "data", "sensitiveData.json");
const localServerEnvPath = path.resolve(localServerRootPath, ".env");

/**
 * @param {string} text
 * @returns {Record<string, string>}
 */
function parseEnvText(text) {
  /** @type {Record<string, string>} */
  const map = {};
  const lines = String(text ?? "").split(/\r?\n/);
  for (const line of lines) {
    const trimmed = line.trim();
    if (trimmed.length === 0 || trimmed.startsWith("#")) {
      continue;
    }
    const equalIndex = trimmed.indexOf("=");
    if (equalIndex <= 0) {
      continue;
    }
    const key = trimmed.slice(0, equalIndex).trim();
    const value = trimmed.slice(equalIndex + 1);
    map[key] = value;
  }
  return map;
}

/**
 * @param {string} filePath
 * @returns {Record<string, string>}
 */
function readEnvMap(filePath) {
  if (!fs.existsSync(filePath)) {
    return {};
  }
  return parseEnvText(fs.readFileSync(filePath, "utf-8"));
}

/**
 * @param {string} filePath
 * @param {Record<string, string>} existingMap
 * @param {Record<string, string>} nextMap
 */
function appendMissingEnvVariables(filePath, existingMap, nextMap) {
  const appendLines = [];
  for (const [key, value] of Object.entries(nextMap)) {
    if (String(existingMap[key] ?? "").trim().length > 0) {
      continue;
    }
    appendLines.push(`${key}=${value}`);
  }
  if (appendLines.length === 0) {
    return;
  }
  const block =
    "\n# ------------------------------------------------------------\n" +
    "# k-iot key vault (development B mode)\n" +
    "# ------------------------------------------------------------\n" +
    appendLines.join("\n") +
    "\n";
  fs.appendFileSync(filePath, block, "utf-8");
}

/**
 * @param {Buffer} plainBuffer
 * @param {Buffer} masterKeyBuffer
 * @param {string} entryName
 */
function encryptBuffer(plainBuffer, masterKeyBuffer, entryName) {
  const ivBuffer = crypto.randomBytes(12);
  const cipher = crypto.createCipheriv("aes-256-gcm", masterKeyBuffer, ivBuffer);
  cipher.setAAD(Buffer.from(entryName, "utf-8"));
  const encryptedBuffer = Buffer.concat([cipher.update(plainBuffer), cipher.final()]);
  const tagBuffer = cipher.getAuthTag();
  return {
    algorithm: "aes-256-gcm",
    ivB64: ivBuffer.toString("base64"),
    tagB64: tagBuffer.toString("base64"),
    ciphertextB64: encryptedBuffer.toString("base64")
  };
}

/**
 * @param {string} sbPemPath
 */
function generateSecureBootKeyFile(sbPemPath) {
  const result = spawnSync(
    "python",
    ["-m", "espsecure", "generate_signing_key", "--version", "2", "--scheme", "rsa3072", sbPemPath],
    { shell: false, stdio: "pipe", encoding: "utf-8" }
  );
  if (result.status !== 0) {
    throw new Error(
      `generateSecureBootKeyFile failed. status=${result.status} stdout=${result.stdout} stderr=${result.stderr}`
    );
  }
}

function main() {
  if (!fs.existsSync(esp32SensitiveDataPath)) {
    throw new Error(`main failed. sensitiveDataPath not found. path=${esp32SensitiveDataPath}`);
  }
  const envMap = readEnvMap(localServerEnvPath);
  const masterKeyB64 =
    String(envMap.KIOT_KEY_VAULT_MASTER_KEY_B64 ?? "").trim().length > 0
      ? String(envMap.KIOT_KEY_VAULT_MASTER_KEY_B64 ?? "").trim()
      : crypto.randomBytes(32).toString("base64");
  const masterKeyBuffer = Buffer.from(masterKeyB64, "base64");
  if (masterKeyBuffer.length !== 32) {
    throw new Error(
      `main failed. KIOT_KEY_VAULT_MASTER_KEY_B64 must decode to 32 bytes. actual=${masterKeyBuffer.length}`
    );
  }

  const tempDirectoryPath = fs.mkdtempSync(path.join(os.tmpdir(), "kiot-vault-"));
  const secureBootPemPath = path.join(tempDirectoryPath, "sb_signing_key.pem");
  generateSecureBootKeyFile(secureBootPemPath);

  const feKeyBuffer = crypto.randomBytes(32);
  const hmacKeyBuffer = crypto.randomBytes(32);
  const sbPemBuffer = fs.readFileSync(secureBootPemPath);

  const nowIsoText = new Date().toISOString();
  const sensitiveJson = JSON.parse(fs.readFileSync(esp32SensitiveDataPath, "utf-8"));
  sensitiveJson.kiotKeyVault = {
    version: "dev-b-v1",
    createdAt: nowIsoText,
    keyRef: "local-dev-b-v1",
    entries: {
      flashEncryptionKey: encryptBuffer(feKeyBuffer, masterKeyBuffer, "flashEncryptionKey"),
      nvsHmacKey: encryptBuffer(hmacKeyBuffer, masterKeyBuffer, "nvsHmacKey"),
      secureBootSigningKeyPem: encryptBuffer(sbPemBuffer, masterKeyBuffer, "secureBootSigningKeyPem")
    },
    fingerprints: {
      flashEncryptionKeySha256: crypto.createHash("sha256").update(feKeyBuffer).digest("hex"),
      nvsHmacKeySha256: crypto.createHash("sha256").update(hmacKeyBuffer).digest("hex"),
      secureBootSigningKeyPemSha256: crypto.createHash("sha256").update(sbPemBuffer).digest("hex")
    }
  };
  fs.writeFileSync(esp32SensitiveDataPath, JSON.stringify(sensitiveJson, null, 2) + "\n", "utf-8");

  appendMissingEnvVariables(localServerEnvPath, envMap, {
    KIOT_KEY_VAULT_AUTO_UNLOCK: "true",
    KIOT_KEY_VAULT_MASTER_KEY_B64: masterKeyB64,
    KIOT_KEY_VAULT_KEY_REF: "local-dev-b-v1"
  });

  fs.rmSync(tempDirectoryPath, { recursive: true, force: true });
  console.log("setupKiotKeyVault completed.");
  console.log(`sensitiveDataPath=${esp32SensitiveDataPath}`);
  console.log(`envPath=${localServerEnvPath}`);
  console.log(`fingerprint.flashEncryption=${sensitiveJson.kiotKeyVault.fingerprints.flashEncryptionKeySha256}`);
  console.log(`fingerprint.nvsHmac=${sensitiveJson.kiotKeyVault.fingerprints.nvsHmacKeySha256}`);
  console.log(`fingerprint.secureBootPem=${sensitiveJson.kiotKeyVault.fingerprints.secureBootSigningKeyPemSha256}`);
}

try {
  main();
} catch (error) {
  console.error(`setupKiotKeyVault failed. error=${error instanceof Error ? error.message : String(error)}`);
  process.exit(1);
}
