/**
 * @file types.ts
 * @description LocalServerで利用する型定義を集約する。
 * @remarks
 * - [重要] MQTT受信JSONが不足項目を含んでも運用継続できるよう、optional項目を許容する。
 * - [厳守] プロパティ名はローワーキャメルケースで統一する。
 * - [禁止] 機密情報（パスワード、鍵素材）を状態型へ保存しない。
 */

/**
 * @description ESP32のオンライン状態を表す列挙型。
 */
export type deviceOnlineState = "online" | "offline" | "unknown";

/**
 * @description ESP32一覧表示用の状態。
 */
export interface deviceState {
  deviceName: string;
  srcId: string;
  dstId: string;
  macAddr: string;
  ipAddress: string;
  wifiSsid: string;
  firmwareVersion: string;
  onlineState: deviceOnlineState;
  detail: string;
  lastMessageId: string;
  lastStatusSub: string;
  lastSeenAt: string;
  statusTopic: string;
}

/**
 * @description MQTT受信メッセージの正規化結果。
 */
export interface statusMessage {
  topic: string;
  srcId: string;
  dstId: string;
  messageId: string;
  macAddr: string;
  ipAddress: string;
  wifiSsid: string;
  firmwareVersion: string;
  onlineState: string;
  statusSub: string;
  detail: string;
  receivedAt: string;
}

/**
 * @description コマンド発行APIの要求パラメータ。
 */
export interface commandRequestBody {
  targetNames: string[] | "all";
  timeoutSeconds?: number;
}

/**
 * @description OTAコマンドAPIの要求パラメータ。
 */
export interface otaCommandRequestBody extends commandRequestBody {
  manifestUrl?: string;
  firmwareUrl?: string;
  firmwareVersion?: string;
}

/**
 * @description MQTT publish payloadの共通基本型。
 */
export interface mqttCommandPayload {
  v: number;
  DstID: string;
  SrcID: string;
  id: string;
  ts: string;
  op: string;
  sub: string;
  args: Record<string, unknown>;
}
