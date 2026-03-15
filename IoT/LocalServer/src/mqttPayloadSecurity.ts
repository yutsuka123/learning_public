/**
 * @file mqttPayloadSecurity.ts
 * @description MQTT payload全文暗号化/復号化の共通モジュール。
 * @remarks
 * - [重要] トピックは平文のまま、payload本文のみを k-device(AES-256-GCM) で暗号化する。
 * - [厳守] 復号時は `security.mode` を検査し、想定外形式を誤って復号しない。
 * - [推奨] 運用切替は `MQTT_PAYLOAD_ENCRYPTION_MODE`（plain/compat/strict）で実施する。
 */

import { keyService } from "./keyService";

export type mqttPayloadEncryptionMode = "plain" | "compat" | "strict";

interface payloadEncryptionEnvelope {
  v: 1;
  security: {
    mode: "k-device-a256gcm-v1";
  };
  enc: {
    alg: "A256GCM";
    iv: string;
    ct: string;
    tag: string;
  };
}

export interface decodedPayloadResult {
  plainPayloadText: string;
  encrypted: boolean;
}

const envelopeModeText = "k-device-a256gcm-v1" as const;

/**
 * @description 環境変数から暗号化運用モードを解決する。
 * @returns 運用モード。
 */
export function resolveMqttPayloadEncryptionMode(): mqttPayloadEncryptionMode {
  // [厳守] 本番既定は strict。環境変数未設定時に平文許容へ戻らないよう固定する。
  const rawModeText = (process.env.MQTT_PAYLOAD_ENCRYPTION_MODE ?? "strict").trim().toLowerCase();
  if (rawModeText === "plain" || rawModeText === "compat" || rawModeText === "strict") {
    return rawModeText;
  }
  // [禁止] 不正値時に互換モードへ自動フォールバックしない（平文混入リスク回避）。
  return "strict";
}

/**
 * @description MQTT payload暗号化/復号化を提供するサービス。
 */
export class mqttPayloadSecurityService {
  private readonly localKeyService: keyService;
  private readonly encryptionMode: mqttPayloadEncryptionMode;

  public constructor(localKeyService: keyService, encryptionMode: mqttPayloadEncryptionMode) {
    this.localKeyService = localKeyService;
    this.encryptionMode = encryptionMode;
  }

  /**
   * @description 現在の暗号化運用モードを返す。
   * @returns 運用モード。
   */
  public getMode(): mqttPayloadEncryptionMode {
    return this.encryptionMode;
  }

  /**
   * @description 送信payloadをモードに応じて暗号化する。
   * @param targetDeviceName 宛先デバイス名。
   * @param plainPayloadText 平文payload。
   * @returns 送信用payload文字列。
   */
  public async encodeOutgoingPayload(targetDeviceName: string, plainPayloadText: string): Promise<string> {
    if (this.encryptionMode === "plain") {
      return plainPayloadText;
    }
    try {
      const encrypted = await this.localKeyService.encryptByKDevice(targetDeviceName, plainPayloadText);
      const envelope: payloadEncryptionEnvelope = {
        v: 1,
        security: {
          mode: envelopeModeText
        },
        enc: {
          alg: "A256GCM",
          iv: encrypted.ivBase64,
          ct: encrypted.cipherBase64,
          tag: encrypted.tagBase64
        }
      };
      return JSON.stringify(envelope);
    } catch (encryptionError) {
      const detailText = encryptionError instanceof Error ? encryptionError.message : String(encryptionError);
      if (this.encryptionMode === "compat") {
        console.warn(
          `mqttPayloadSecurityService.encodeOutgoingPayload fallback to plain. targetDeviceName=${targetDeviceName} mode=${this.encryptionMode} detail=${detailText}`
        );
        return plainPayloadText;
      }
      throw new Error(
        `mqttPayloadSecurityService.encodeOutgoingPayload failed. targetDeviceName=${targetDeviceName} mode=${this.encryptionMode} detail=${detailText}`
      );
    }
  }

  /**
   * @description 受信payloadをモードに応じて復号する。
   * @param sourceDeviceName 送信元デバイス名（k-device解決に使用）。
   * @param incomingPayloadText 受信payload。
   * @returns 復号結果。
   */
  public async decodeIncomingPayload(sourceDeviceName: string, incomingPayloadText: string): Promise<decodedPayloadResult> {
    if (this.encryptionMode === "plain") {
      return {
        plainPayloadText: incomingPayloadText,
        encrypted: false
      };
    }
    const envelope = this.tryParseEnvelope(incomingPayloadText);
    if (envelope === null) {
      if (this.encryptionMode === "strict") {
        throw new Error(
          `mqttPayloadSecurityService.decodeIncomingPayload failed. encryption is required but envelope is missing. sourceDeviceName=${sourceDeviceName} mode=${this.encryptionMode}`
        );
      }
      return {
        plainPayloadText: incomingPayloadText,
        encrypted: false
      };
    }
    try {
      const decryptedText = await this.localKeyService.decryptByKDevice(sourceDeviceName, {
        ivBase64: envelope.enc.iv,
        cipherBase64: envelope.enc.ct,
        tagBase64: envelope.enc.tag
      });
      return {
        plainPayloadText: decryptedText,
        encrypted: true
      };
    } catch (decryptionError) {
      const detailText = decryptionError instanceof Error ? decryptionError.message : String(decryptionError);
      throw new Error(
        `mqttPayloadSecurityService.decodeIncomingPayload failed. sourceDeviceName=${sourceDeviceName} mode=${this.encryptionMode} detail=${detailText}`
      );
    }
  }

  private tryParseEnvelope(payloadText: string): payloadEncryptionEnvelope | null {
    let parsedObject: unknown = null;
    try {
      parsedObject = JSON.parse(payloadText);
    } catch {
      return null;
    }
    if (typeof parsedObject !== "object" || parsedObject === null) {
      return null;
    }
    const envelope = parsedObject as Partial<payloadEncryptionEnvelope>;
    const securityMode = envelope.security?.mode;
    const algorithm = envelope.enc?.alg;
    if (securityMode !== envelopeModeText || algorithm !== "A256GCM") {
      return null;
    }
    if (
      typeof envelope.enc?.iv !== "string" ||
      typeof envelope.enc?.ct !== "string" ||
      typeof envelope.enc?.tag !== "string" ||
      envelope.enc.iv.length === 0 ||
      envelope.enc.ct.length === 0 ||
      envelope.enc.tag.length === 0
    ) {
      throw new Error("tryParseEnvelope failed. encrypted fields are invalid.");
    }
    return envelope as payloadEncryptionEnvelope;
  }
}

