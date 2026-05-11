/**
 * @file secretCoreIpcClient.ts
 * @description SecretCore Named Pipe 通信用の low-level IPC クライアント。
 * @remarks
 * - [厳守] 業務コードは本クラスを直接利用せず、`SecretCoreFacade` を経由する。
 * - [禁止] UI / REST handler / service 層が `sendRequest()` を直接呼ぶ構成。
 * - [厳守] IPC メッセージは 16 byte の IPC nonce と requestId + timestamp を持ち、AES-256-GCM の実IV はそこから導出する。
 * - 変更日: 2026-05-11 IPC nonce の 16 byte 化。理由: 008-0011 対応のため。
 */
import * as crypto from "crypto";
import * as net from "net";

interface secretCoreProtectedRequestEnvelope {
  version: 1;
  requestId: string;
  timestamp: number;
  nonceBase64: string;
  cipherBase64: string;
  tagBase64: string;
}

interface secretCoreProtectedResponseEnvelope {
  version: 1;
  responseToRequestId: string;
  timestamp: number;
  nonceBase64: string;
  cipherBase64: string;
  tagBase64: string;
}

export class SecretCoreIpcClient {
  private readonly getPipeName: () => string;
  private readonly getIpcSessionKeyBase64: () => string;

  constructor(getPipeName: () => string, getIpcSessionKeyBase64: () => string) {
    this.getPipeName = getPipeName;
    this.getIpcSessionKeyBase64 = getIpcSessionKeyBase64;
  }

  public async sendRequest<T = any>(command: string, payload?: any): Promise<T> {
    return new Promise((resolve, reject) => {
      const requestId = crypto.randomUUID();
      const requestEnvelope = this.createProtectedRequestEnvelope(requestId, {
        command,
        payload: payload || null
      });
      const client = net.createConnection(this.getPipeName(), () => {
        client.write(JSON.stringify(requestEnvelope));
      });

      let responseData = "";

      client.on("data", (chunk) => {
        responseData += chunk.toString();
        client.end();
      });

      client.on("end", () => {
        try {
          const parsedEnvelope = JSON.parse(responseData) as secretCoreProtectedResponseEnvelope;
          const parsed = this.parseProtectedResponseEnvelope(requestId, parsedEnvelope) as {
            status: string;
            data: T;
            error?: string;
          };
          if (parsed.status === "ok") {
            resolve(parsed.data);
          } else {
            reject(new Error(`IPC Error: ${parsed.error}`));
          }
        } catch (e) {
          reject(new Error(`Failed to parse IPC response: ${e}`));
        }
      });

      client.on("error", (err) => {
        reject(err);
      });
    });
  }

  public async checkHealth(logError: boolean = true): Promise<boolean> {
    try {
      const res = await this.sendRequest("health");
      return res && (res as any).version !== undefined;
    } catch (e) {
      if (logError) {
        console.error("SecretCore health check failed:", e);
      }
      return false;
    }
  }

  private createProtectedRequestEnvelope(
    requestId: string,
    requestObject: { command: string; payload: any }
  ): secretCoreProtectedRequestEnvelope {
    const sessionKeyBuffer = this.getSessionKeyBuffer();
    const nonceBuffer = crypto.randomBytes(16);
    const timestamp = Date.now();
    const cipherNonceBuffer = this.deriveCipherNonceBuffer(nonceBuffer, requestId, timestamp);
    const cipher = crypto.createCipheriv("aes-256-gcm", sessionKeyBuffer, cipherNonceBuffer);
    cipher.setAAD(Buffer.from(this.createRequestAadText(requestId, timestamp, nonceBuffer), "utf8"));
    const cipherBuffer = Buffer.concat([cipher.update(JSON.stringify(requestObject), "utf8"), cipher.final()]);
    const tagBuffer = cipher.getAuthTag();
    return {
      version: 1,
      requestId,
      timestamp,
      nonceBase64: nonceBuffer.toString("base64"),
      cipherBase64: cipherBuffer.toString("base64"),
      tagBase64: tagBuffer.toString("base64")
    };
  }

  private parseProtectedResponseEnvelope(requestId: string, responseEnvelope: secretCoreProtectedResponseEnvelope): unknown {
    if (responseEnvelope.version !== 1) {
      throw new Error(`parseProtectedResponseEnvelope failed. unsupported version=${responseEnvelope.version}`);
    }
    if (responseEnvelope.responseToRequestId !== requestId) {
      throw new Error(
        `parseProtectedResponseEnvelope failed. requestId mismatch. expected=${requestId} actual=${responseEnvelope.responseToRequestId}`
      );
    }
    const sessionKeyBuffer = this.getSessionKeyBuffer();
    const nonceBuffer = Buffer.from(responseEnvelope.nonceBase64, "base64");
    const cipherBuffer = Buffer.from(responseEnvelope.cipherBase64, "base64");
    const tagBuffer = Buffer.from(responseEnvelope.tagBase64, "base64");
    if (nonceBuffer.length !== 16) {
      throw new Error(`parseProtectedResponseEnvelope failed. nonce length must be 16 bytes. actual=${nonceBuffer.length}`);
    }
    const cipherNonceBuffer = this.deriveCipherNonceBuffer(
      nonceBuffer,
      responseEnvelope.responseToRequestId,
      responseEnvelope.timestamp
    );
    const decipher = crypto.createDecipheriv("aes-256-gcm", sessionKeyBuffer, cipherNonceBuffer);
    decipher.setAAD(
      Buffer.from(
        this.createResponseAadText(responseEnvelope.responseToRequestId, responseEnvelope.timestamp, nonceBuffer),
        "utf8"
      )
    );
    decipher.setAuthTag(tagBuffer);
    const plainBuffer = Buffer.concat([decipher.update(cipherBuffer), decipher.final()]);
    return JSON.parse(plainBuffer.toString("utf8"));
  }

  private getSessionKeyBuffer(): Buffer {
    const sessionKeyBase64 = this.getIpcSessionKeyBase64();
    if (sessionKeyBase64.trim().length === 0) {
      throw new Error("getSessionKeyBuffer failed. session key is empty.");
    }
    const sessionKeyBuffer = Buffer.from(sessionKeyBase64, "base64");
    if (sessionKeyBuffer.length !== 32) {
      throw new Error(`getSessionKeyBuffer failed. key length must be 32 bytes. actual=${sessionKeyBuffer.length}`);
    }
    return sessionKeyBuffer;
  }

  private createResponseAadText(responseToRequestId: string, timestamp: number, nonceBuffer: Buffer): string {
    return `v=1|rrid=${responseToRequestId}|ts=${timestamp}|nonce=${nonceBuffer.toString("base64")}`;
  }

  private createRequestAadText(requestId: string, timestamp: number, nonceBuffer: Buffer): string {
    return `v=1|rid=${requestId}|ts=${timestamp}|nonce=${nonceBuffer.toString("base64")}`;
  }

  private deriveCipherNonceBuffer(nonceBuffer: Buffer, requestId: string, timestamp: number): Buffer {
    const hashBuffer = crypto
      .createHash("sha256")
      .update(nonceBuffer)
      .update(requestId, "utf8")
      .update(String(timestamp), "utf8")
      .digest();
    return hashBuffer.subarray(0, 12);
  }
}
