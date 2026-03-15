/**
 * @file deviceTransport.ts
 * @description LocalServer から見たデバイス通信層の境界IFを定義する。
 * @remarks
 * - [重要] `server.ts` は具体実装（MQTT/将来のRust通信実装）へ直接依存せず、本IFへ依存する。
 * - [厳守] 通信方式の違いは本IFの実装差し替えで吸収する。
 * - [将来対応] Rust 側が MQTT/OTA 通信を主担当した際は、本IFの Rust 実装へ置き換える。
 * - 変更日: 2026-03-15 新規作成。理由: 003-0012 の残課題「通信まで Rust 主導」へ段階移行するため。
 */

import { deviceState, otaProgressMessage, statusMessage, trhMessage } from "./types";

export interface secureEchoMessage {
  topic: string;
  srcId: string;
  requestId: string;
  ivBase64: string;
  cipherBase64: string;
  tagBase64: string;
  receivedAt: string;
}

export interface deviceTransportEventMap {
  statusUpdated: [status: statusMessage];
  otaProgressUpdated: [otaProgress: otaProgressMessage];
  secureEchoUpdated: [secureEcho: secureEchoMessage];
  trhUpdated: [trh: trhMessage];
  deviceStateUpdated: [deviceState: deviceState];
  connected: [];
  disconnected: [];
}

/**
 * @description デバイス通信層の共通IF。
 */
export interface deviceTransport {
  connect(): void;
  on<EventKey extends keyof deviceTransportEventMap>(
    eventName: EventKey,
    listener: (...args: deviceTransportEventMap[EventKey]) => void
  ): void;
  requestStatus(targetNames: string[] | "all"): Promise<void>;
  requestOta(
    targetNames: string[] | "all",
    args: {
      manifestUrl: string;
      firmwareUrl: string;
      firmwareVersion: string;
      sha256: string;
      timeoutSeconds: number;
    }
  ): Promise<void>;
  requestCall(targetNames: string[] | "all", subCommand: string, args: Record<string, unknown>): Promise<void>;
  requestSet(targetNames: string[] | "all", subCommand: string, args: Record<string, unknown>): Promise<void>;
  requestGet(targetNames: string[] | "all", subCommand: string, args: Record<string, unknown>): Promise<void>;
  requestNetwork(targetNames: string[] | "all", subCommand: string, args: Record<string, unknown>): Promise<void>;
  requestSecurePing(
    targetName: string,
    requestId: string,
    encryptedArgs: { ivBase64: string; cipherBase64: string; tagBase64: string },
    timeoutMs: number
  ): Promise<secureEchoMessage>;
}
