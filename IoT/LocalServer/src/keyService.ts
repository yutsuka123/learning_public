/**
 * @file keyService.ts
 * @description LocalServerの k-user / k-device 発行と暗号化通信補助を提供する。
 * @remarks
 * - [重要] k-user はランダム32バイトで発行し、暗号化して永続化する。
 * - [厳守] k-device は `HMAC-SHA256(k-user, targetDeviceName)` で導出する。
 * - [禁止] API応答へ raw k-user を返さない。
 */

import fs from "fs";
import path from "path";
import crypto from "crypto";
import { appConfig } from "./config";

interface encryptedBlob {
  ivBase64: string;
  tagBase64: string;
  cipherBase64: string;
}

interface keyStorePayload {
  kUserBase64: string;
  kUserIssuedAt: string;
  deviceKeys: Record<string, { keyDeviceBase64: string; issuedAt: string }>;
}

interface keyStoreEnvelope {
  version: 1;
  encryptedPayload: encryptedBlob;
}

/**
 * @description 鍵発行・暗号化補助サービス。
 */
export class keyService {
  private readonly storeFilePath: string;
  private readonly appConfig: appConfig;

  public constructor(config: appConfig) {
    this.appConfig = config;
    const dataDirectoryPath = path.resolve(process.cwd(), "data");
    if (!fs.existsSync(dataDirectoryPath)) {
      fs.mkdirSync(dataDirectoryPath, { recursive: true });
    }
    this.storeFilePath = path.join(dataDirectoryPath, "keyStore.json");
  }

  /**
   * @description k-user を発行して保存する。
   * @returns 発行結果（指紋のみ）。
   */
  public issueKUser(): { issuedAt: string; keyFingerprint: string } {
    const nextPayload = this.readPayload();
    const issuedAt = new Date().toISOString();
    const kUserBuffer = crypto.randomBytes(32);
    nextPayload.kUserBase64 = kUserBuffer.toString("base64");
    nextPayload.kUserIssuedAt = issuedAt;
    nextPayload.deviceKeys = {};
    this.writePayload(nextPayload);
    return {
      issuedAt,
      keyFingerprint: this.createFingerprintText(kUserBuffer)
    };
  }

  /**
   * @description 対象デバイス向け k-device を導出し保存する。
   * @param targetDeviceName 対象デバイス名。
   * @returns 導出結果。
   */
  public issueKDevice(targetDeviceName: string): { targetDeviceName: string; issuedAt: string; keyDeviceBase64: string; keyFingerprint: string } {
    if (targetDeviceName.trim().length === 0) {
      throw new Error("issueKDevice failed. targetDeviceName is empty.");
    }
    const nextPayload = this.readPayload();
    const kUserBuffer = this.getKUserBufferFromPayload(nextPayload);
    const keyDeviceBuffer = crypto.createHmac("sha256", kUserBuffer).update(targetDeviceName).digest();
    const keyDeviceBase64 = keyDeviceBuffer.toString("base64");
    const issuedAt = new Date().toISOString();
    nextPayload.deviceKeys[targetDeviceName] = {
      keyDeviceBase64,
      issuedAt
    };
    this.writePayload(nextPayload);
    return {
      targetDeviceName,
      issuedAt,
      keyDeviceBase64,
      keyFingerprint: this.createFingerprintText(keyDeviceBuffer)
    };
  }

  /**
   * @description 保存済み k-device を返す。
   * @param targetDeviceName 対象デバイス名。
   * @returns Base64文字列。
   */
  public getKDeviceBase64(targetDeviceName: string): string {
    const loadedPayload = this.readPayload();
    const found = loadedPayload.deviceKeys[targetDeviceName];
    if (found === undefined) {
      throw new Error(`getKDeviceBase64 failed. no keyDevice for targetDeviceName=${targetDeviceName}`);
    }
    return found.keyDeviceBase64;
  }

  /**
   * @description k-device で平文を AES-256-GCM 暗号化する。
   * @param targetDeviceName 対象デバイス名。
   * @param plainText 平文。
   * @returns 暗号化オブジェクト。
   */
  public encryptByKDevice(targetDeviceName: string, plainText: string): { alg: "A256GCM"; ivBase64: string; cipherBase64: string; tagBase64: string } {
    const keyDeviceBuffer = Buffer.from(this.getKDeviceBase64(targetDeviceName), "base64");
    if (keyDeviceBuffer.length !== 32) {
      throw new Error(`encryptByKDevice failed. key length must be 32 bytes. targetDeviceName=${targetDeviceName} actual=${keyDeviceBuffer.length}`);
    }
    const ivBuffer = crypto.randomBytes(12);
    const cipher = crypto.createCipheriv("aes-256-gcm", keyDeviceBuffer, ivBuffer);
    const cipherBuffer = Buffer.concat([cipher.update(plainText, "utf8"), cipher.final()]);
    const tagBuffer = cipher.getAuthTag();
    return {
      alg: "A256GCM",
      ivBase64: ivBuffer.toString("base64"),
      cipherBase64: cipherBuffer.toString("base64"),
      tagBase64: tagBuffer.toString("base64")
    };
  }

  /**
   * @description k-device で AES-256-GCM 復号する。
   * @param targetDeviceName 対象デバイス名。
   * @param encrypted 暗号化オブジェクト。
   * @returns 復号平文。
   */
  public decryptByKDevice(
    targetDeviceName: string,
    encrypted: { ivBase64: string; cipherBase64: string; tagBase64: string }
  ): string {
    const keyDeviceBuffer = Buffer.from(this.getKDeviceBase64(targetDeviceName), "base64");
    if (keyDeviceBuffer.length !== 32) {
      throw new Error(`decryptByKDevice failed. key length must be 32 bytes. targetDeviceName=${targetDeviceName} actual=${keyDeviceBuffer.length}`);
    }
    const ivBuffer = Buffer.from(encrypted.ivBase64, "base64");
    const cipherBuffer = Buffer.from(encrypted.cipherBase64, "base64");
    const tagBuffer = Buffer.from(encrypted.tagBase64, "base64");
    const decipher = crypto.createDecipheriv("aes-256-gcm", keyDeviceBuffer, ivBuffer);
    decipher.setAuthTag(tagBuffer);
    const plainBuffer = Buffer.concat([decipher.update(cipherBuffer), decipher.final()]);
    return plainBuffer.toString("utf8");
  }

  private createFingerprintText(keyBuffer: Buffer): string {
    return crypto.createHash("sha256").update(keyBuffer).digest("hex").slice(0, 16);
  }

  private getMasterStorageKey(): Buffer {
    const saltText = `localserver-key-store:${this.appConfig.sourceId}`;
    return crypto.scryptSync(this.appConfig.kUserAppIdentifier, saltText, 32);
  }

  private encryptTextForStore(plainText: string): encryptedBlob {
    const ivBuffer = crypto.randomBytes(12);
    const cipher = crypto.createCipheriv("aes-256-gcm", this.getMasterStorageKey(), ivBuffer);
    const cipherBuffer = Buffer.concat([cipher.update(plainText, "utf8"), cipher.final()]);
    const tagBuffer = cipher.getAuthTag();
    return {
      ivBase64: ivBuffer.toString("base64"),
      tagBase64: tagBuffer.toString("base64"),
      cipherBase64: cipherBuffer.toString("base64")
    };
  }

  private decryptTextFromStore(blob: encryptedBlob): string {
    const ivBuffer = Buffer.from(blob.ivBase64, "base64");
    const tagBuffer = Buffer.from(blob.tagBase64, "base64");
    const cipherBuffer = Buffer.from(blob.cipherBase64, "base64");
    const decipher = crypto.createDecipheriv("aes-256-gcm", this.getMasterStorageKey(), ivBuffer);
    decipher.setAuthTag(tagBuffer);
    const plainBuffer = Buffer.concat([decipher.update(cipherBuffer), decipher.final()]);
    return plainBuffer.toString("utf8");
  }

  private readPayload(): keyStorePayload {
    if (!fs.existsSync(this.storeFilePath)) {
      return {
        kUserBase64: "",
        kUserIssuedAt: "",
        deviceKeys: {}
      };
    }
    const fileText = fs.readFileSync(this.storeFilePath, "utf8");
    const parsedEnvelope = JSON.parse(fileText) as keyStoreEnvelope;
    const decryptedText = this.decryptTextFromStore(parsedEnvelope.encryptedPayload);
    return JSON.parse(decryptedText) as keyStorePayload;
  }

  private writePayload(payload: keyStorePayload): void {
    const encryptedPayload = this.encryptTextForStore(JSON.stringify(payload));
    const envelope: keyStoreEnvelope = {
      version: 1,
      encryptedPayload
    };
    fs.writeFileSync(this.storeFilePath, `${JSON.stringify(envelope, null, 2)}\n`, "utf8");
  }

  private getKUserBufferFromPayload(payload: keyStorePayload): Buffer {
    if (payload.kUserBase64.length === 0) {
      throw new Error("getKUserBufferFromPayload failed. k-user is not issued.");
    }
    const kUserBuffer = Buffer.from(payload.kUserBase64, "base64");
    if (kUserBuffer.length !== 32) {
      throw new Error(`getKUserBufferFromPayload failed. k-user length must be 32 bytes. actual=${kUserBuffer.length}`);
    }
    return kUserBuffer;
  }
}
