/**
 * @file unlockKiotKeyVault.mjs
 * @description
 * `ESP32/data/sensitiveData.json` の `kiotKeyVault` を復号し、
 * `LocalServer/data/secure-key-cache` 配下へ一時鍵ファイルを出力する。
 *
 * 主な仕様:
 * - `.env` の `KIOT_KEY_VAULT_MASTER_KEY_B64` を利用して AES-256-GCM 復号する。
 * - `KIOT_KEY_VAULT_AUTO_UNLOCK=true` の場合に処理を行う。
 * - 出力は FE鍵, HMAC鍵, SB秘密鍵PEM の3ファイル。
 * - 標準出力には平文鍵を表示しない（SHA-256 の指紋のみ）。
 *
 * 制限事項:
 * - [重要] キャッシュ先はローカル専用。Gitへコミットしない。
 * - [禁止] このスクリプトを本番運用の長期鍵管理の唯一根拠にしない。
 */

import crypto from "crypto";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const scriptFilePath = fileURLToPath(import.meta.url);
const localServerRootPath = path.resolve(path.dirname(scriptFilePath), "..");
const projectRootPath = path.resolve(localServerRootPath, "..");
const localServerEnvPath = path.resolve(localServerRootPath, ".env");
const esp32SensitiveDataPath = path.resolve(projectRootPath, "ESP32", "data", "sensitiveData.json");
const outputDirectoryPath = path.resolve(localServerRootPath, "data", "secure-key-cache");

/**
 * @param {string} text
 * @returns {Record<string, string>}
 */
function parseEnvText(text) {
  /** @type {Record<string, string>} */
  const map = {};
  for (const line of String(text ?? "").split(/\r?\n/)) {
    const trimmed = line.trim();
    if (trimmed.length === 0 || trimmed.startsWith("#")) {
      continue;
    }
    const separatorIndex = trimmed.indexOf("=");
    if (separatorIndex <= 0) {
      continue;
    }
    map[trimmed.slice(0, separatorIndex).trim()] = trimmed.slice(separatorIndex + 1);
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
 * @param {{ ivB64: string, tagB64: string, ciphertextB64: string }} encryptedEntry
 * @param {Buffer} masterKeyBuffer
 * @param {string} entryName
 * @returns {Buffer}
 */
function decryptEntry(encryptedEntry, masterKeyBuffer, entryName) {
  const ivBuffer = Buffer.from(String(encryptedEntry.ivB64 ?? ""), "base64");
  const tagBuffer = Buffer.from(String(encryptedEntry.tagB64 ?? ""), "base64");
  const ciphertextBuffer = Buffer.from(String(encryptedEntry.ciphertextB64 ?? ""), "base64");
  const decipher = crypto.createDecipheriv("aes-256-gcm", masterKeyBuffer, ivBuffer);
  decipher.setAAD(Buffer.from(entryName, "utf-8"));
  decipher.setAuthTag(tagBuffer);
  return Buffer.concat([decipher.update(ciphertextBuffer), decipher.final()]);
}

/**
 * @param {string} filePath
 * @param {Buffer} dataBuffer
 */
function writeFileAtomic(filePath, dataBuffer) {
  const tempPath = `${filePath}.tmp`;
  fs.writeFileSync(tempPath, dataBuffer);
  fs.renameSync(tempPath, filePath);
}

function main() {
  const envMap = readEnvMap(localServerEnvPath);
  const autoUnlockEnabled = String(envMap.KIOT_KEY_VAULT_AUTO_UNLOCK ?? "false").toLowerCase() === "true";
  if (!autoUnlockEnabled) {
    console.log("unlockKiotKeyVault skipped. reason=KIOT_KEY_VAULT_AUTO_UNLOCK is not true");
    return;
  }
  const masterKeyB64 = String(envMap.KIOT_KEY_VAULT_MASTER_KEY_B64 ?? "").trim();
  const masterKeyBuffer = Buffer.from(masterKeyB64, "base64");
  if (masterKeyBuffer.length !== 32) {
    throw new Error(
      `main failed. KIOT_KEY_VAULT_MASTER_KEY_B64 must decode to 32 bytes. actual=${masterKeyBuffer.length}`
    );
  }
  if (!fs.existsSync(esp32SensitiveDataPath)) {
    throw new Error(`main failed. sensitiveData.json not found. path=${esp32SensitiveDataPath}`);
  }
  const sensitiveJson = JSON.parse(fs.readFileSync(esp32SensitiveDataPath, "utf-8"));
  const vault = sensitiveJson.kiotKeyVault;
  if (!vault || !vault.entries) {
    throw new Error("main failed. kiotKeyVault entries not found in sensitiveData.json");
  }

  const flashEncryptionKeyBuffer = decryptEntry(vault.entries.flashEncryptionKey, masterKeyBuffer, "flashEncryptionKey");
  const nvsHmacKeyBuffer = decryptEntry(vault.entries.nvsHmacKey, masterKeyBuffer, "nvsHmacKey");
  const secureBootPemBuffer = decryptEntry(
    vault.entries.secureBootSigningKeyPem,
    masterKeyBuffer,
    "secureBootSigningKeyPem"
  );

  fs.mkdirSync(outputDirectoryPath, { recursive: true });
  const flashEncryptionFilePath = path.join(outputDirectoryPath, "k-iot-flash-encryption.bin");
  const nvsHmacFilePath = path.join(outputDirectoryPath, "k-iot-hmac-nvs.bin");
  const secureBootPemFilePath = path.join(outputDirectoryPath, "k-iot-secure-boot-signing.pem");
  writeFileAtomic(flashEncryptionFilePath, flashEncryptionKeyBuffer);
  writeFileAtomic(nvsHmacFilePath, nvsHmacKeyBuffer);
  writeFileAtomic(secureBootPemFilePath, secureBootPemBuffer);

  const manifest = {
    generatedAt: new Date().toISOString(),
    keyRef: String(vault.keyRef ?? ""),
    files: {
      flashEncryptionKey: {
        path: flashEncryptionFilePath,
        sha256: crypto.createHash("sha256").update(flashEncryptionKeyBuffer).digest("hex")
      },
      nvsHmacKey: {
        path: nvsHmacFilePath,
        sha256: crypto.createHash("sha256").update(nvsHmacKeyBuffer).digest("hex")
      },
      secureBootSigningKeyPem: {
        path: secureBootPemFilePath,
        sha256: crypto.createHash("sha256").update(secureBootPemBuffer).digest("hex")
      }
    }
  };
  writeFileAtomic(path.join(outputDirectoryPath, "manifest.json"), Buffer.from(JSON.stringify(manifest, null, 2) + "\n"));
  console.log("unlockKiotKeyVault completed.");
  console.log(`outputDirectoryPath=${outputDirectoryPath}`);
  console.log(`flashEncryption.sha256=${manifest.files.flashEncryptionKey.sha256}`);
  console.log(`nvsHmac.sha256=${manifest.files.nvsHmacKey.sha256}`);
  console.log(`secureBootPem.sha256=${manifest.files.secureBootSigningKeyPem.sha256}`);
}

try {
  main();
} catch (error) {
  console.error(`unlockKiotKeyVault failed. error=${error instanceof Error ? error.message : String(error)}`);
  process.exit(1);
}
