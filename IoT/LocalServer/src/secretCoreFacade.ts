/**
 * @file secretCoreFacade.ts
 * @description LocalServer から SecretCore を呼び出すための境界IFを定義する。
 * @remarks
 * - [重要] TS 側から SecretCore の low-level IPC コマンドを直接呼ばず、本Facade経由に統一する。
 * - [厳守] UI 層へ返す値は fingerprint / status / result などの最小情報に限定する。
 * - [禁止] raw `k-user` を本Facadeの戻り値に含めない。
 * - 変更日: 2026-03-15 新規作成。理由: UI 部とセキュア部の境界IFを固定するため。
 */

import { SecretCoreIpcClient } from "./secretCoreIpcClient";
import { deviceState, otaProgressMessage, statusMessage, trhMessage } from "./types";
import { secureEchoMessage } from "./deviceTransport";

/**
 * @description k-user 発行結果の最小DTO。
 */
export interface secretCoreKUserIssueResult {
  isIssued: boolean;
  source: string;
  keyFingerprint: string;
}

/**
 * @description k-user 状態DTO。
 */
export interface secretCoreKUserStatusResult {
  isIssued: boolean;
  issuedAt: string;
  keyFingerprint: string;
  deviceKeyCount: number;
  source: string;
}

/**
 * @description k-device 導出結果DTO。
 */
export interface secretCoreKDeviceResult {
  targetDeviceName: string;
  keyDeviceBase64: string;
}

/**
 * @description 状態復帰待機結果DTO。
 */
export interface secretCoreStatusRecoveryResult {
  deviceName: string;
  publicId: string;
  firmwareVersion: string;
  configVersion: string;
}

/**
 * @description OTA workflow 状態DTO。
 */
export interface secretCoreWorkflowStatusResult {
  workflowId: string;
  workflowType: string;
  targetDeviceName: string;
  state: "queued" | "running" | "waiting_device" | "verifying" | "completed" | "failed";
  result?: string;
  errorSummary?: string;
  detail?: string;
  startedAt: string;
  updatedAt: string;
  firmwareVersion: string;
}

/**
 * @description AES-256-GCM 暗号化DTO。
 */
export interface secretCoreEncryptedPayload {
  alg: "A256GCM";
  ivBase64: string;
  cipherBase64: string;
  tagBase64: string;
}

/**
 * @description SecretCore 受信MQTTイベントDTO。
 */
export interface secretCoreMqttInboundEvent {
  kind: "connected" | "disconnected" | "error" | "statusUpdated" | "trhUpdated" | "otaProgressUpdated" | "secureEchoUpdated" | "deviceStateUpdated";
  detail?: string;
  receivedAt: string;
  status?: statusMessage;
  trh?: trhMessage;
  otaProgress?: otaProgressMessage;
  secureEcho?: secureEchoMessage;
  deviceState?: deviceState;
}

/**
 * @description SecretCore に対する型付き境界IF。
 */
export class SecretCoreFacade {
  private readonly secretCoreIpcClient: SecretCoreIpcClient;

  /**
   * @description Facade を初期化する。
   * @param secretCoreIpcClient low-level IPC クライアント。
   */
  public constructor(secretCoreIpcClient: SecretCoreIpcClient) {
    this.secretCoreIpcClient = secretCoreIpcClient;
  }

  /**
   * @description SecretCore のヘルス状態を返す。
   * @param logError 接続失敗ログを出力するか。
   * @returns 起動可否。
   */
  public async checkHealth(logError: boolean = true): Promise<boolean> {
    return await this.secretCoreIpcClient.checkHealth(logError);
  }

  /**
   * @description k-user 発行状態を保証し、fingerprint 付きで返す。
   * @returns 発行状態。
   */
  public async issueKUser(): Promise<secretCoreKUserIssueResult> {
    return await this.secretCoreIpcClient.sendRequest<secretCoreKUserIssueResult>("issue_k_user");
  }

  /**
   * @description k-user 状態を取得する。
   * @returns 最小状態DTO。
   */
  public async getKUserStatus(): Promise<secretCoreKUserStatusResult> {
    return await this.secretCoreIpcClient.sendRequest<secretCoreKUserStatusResult>("get_k_user_status");
  }

  /**
   * @description 対象デバイス向けの k-device を取得する。
   * @param targetDeviceName 対象デバイス名。
   * @returns k-device 結果。
   */
  public async getKDevice(targetDeviceName: string): Promise<secretCoreKDeviceResult> {
    return await this.secretCoreIpcClient.sendRequest<secretCoreKDeviceResult>("get_k_device", { targetDeviceName });
  }

  /**
   * @description k-device を用いて暗号化する。
   * @param targetDeviceName 対象デバイス名。
   * @param plainText 平文。
   * @returns 暗号化結果。
   */
  public async encryptByKDevice(targetDeviceName: string, plainText: string): Promise<secretCoreEncryptedPayload> {
    return await this.secretCoreIpcClient.sendRequest<secretCoreEncryptedPayload>("encrypt", { targetDeviceName, plainText });
  }

  /**
   * @description k-device を用いて復号する。
   * @param targetDeviceName 対象デバイス名。
   * @param encrypted 暗号化オブジェクト。
   * @returns 復号文字列。
   */
  public async decryptByKDevice(targetDeviceName: string, encrypted: secretCoreEncryptedPayload): Promise<string> {
    const result = await this.secretCoreIpcClient.sendRequest<{ plainText: string }>("decrypt", { targetDeviceName, encrypted });
    return result.plainText;
  }

  /**
   * @description k-user の暗号化バックアップを出力する。
   * @param backupPassword バックアップパスワード。
   * @param backupFilePath 出力先ファイルパス。
   * @returns 出力結果。
   */
  public async exportKUserBackup(
    backupPassword: string,
    backupFilePath?: string
  ): Promise<{ exported: true; backupFilePath: string; keyFingerprint: string; source: string; format: string }> {
    return await this.secretCoreIpcClient.sendRequest("export_k_user", { backupPassword, backupFilePath });
  }

  /**
   * @description k-user 暗号化バックアップを復元する。
   * @param backupPassword バックアップパスワード。
   * @param backupFilePath バックアップファイルパス。
   * @returns 復元結果。
   */
  public async importKUserBackup(
    backupPassword: string,
    backupFilePath?: string
  ): Promise<{ imported: true; keyFingerprint: string; source: string }> {
    return await this.secretCoreIpcClient.sendRequest("import_k_user_backup", { backupPassword, backupFilePath });
  }

  /**
   * @description SecretCore 経由で MQTT publish を実行する。
   * @param topic 送信先トピック。
   * @param payload 送信payload。
   * @param qos QoS。
   * @returns publish結果。
   */
  public async publishMqttMessage(
    topic: string,
    payload: string,
    qos: 0 | 1 | 2
  ): Promise<{ published: true; topic: string; qos: number; payloadLength: number }> {
    return await this.secretCoreIpcClient.sendRequest("mqtt_publish", { topic, payload, qos });
  }

  /**
   * @description SecretCore 内の MQTT subscribe 受信ループを開始する。
   * @returns 起動結果。
   */
  public async startMqttReceiver(): Promise<{ started: boolean; alreadyRunning: boolean }> {
    return await this.secretCoreIpcClient.sendRequest("mqtt_start_receiver");
  }

  /**
   * @description SecretCore に蓄積された MQTT 受信イベントを取り出す。
   * @returns 受信イベント配列。
   */
  public async drainMqttEvents(): Promise<{ events: secretCoreMqttInboundEvent[] }> {
    return await this.secretCoreIpcClient.sendRequest("mqtt_drain_events");
  }

  /**
   * @description SecretCore に保持された deviceState を用いて online 復帰を待機する。
   * @param targetDeviceName 対象デバイス名。空文字で任意の online 機体。
   * @param timeoutSeconds 待機秒数。
   * @returns 復帰結果。
   */
  public async waitForStatusRecovery(
    targetDeviceName: string,
    timeoutSeconds: number
  ): Promise<secretCoreStatusRecoveryResult> {
    return await this.secretCoreIpcClient.sendRequest<secretCoreStatusRecoveryResult>("wait_for_status_recovery", {
      targetDeviceName,
      timeoutSeconds
    });
  }

  /**
   * @description Rust 側 OTA workflow を開始する。
   * @param targetDeviceName 対象デバイス名。
   * @param args OTA 実行引数。
   * @returns workflow 初期状態。
   */
  public async runSignedOtaCommand(
    targetDeviceName: string,
    args: {
      manifestUrl: string;
      firmwareUrl: string;
      firmwareVersion: string;
      sha256: string;
      timeoutSeconds: number;
    }
  ): Promise<secretCoreWorkflowStatusResult> {
    return await this.secretCoreIpcClient.sendRequest<secretCoreWorkflowStatusResult>("run_signed_ota_command", {
      targetDeviceName,
      manifestUrl: args.manifestUrl,
      firmwareUrl: args.firmwareUrl,
      firmwareVersion: args.firmwareVersion,
      sha256: args.sha256,
      timeoutSeconds: args.timeoutSeconds
    });
  }

  /**
   * @description Rust 側 workflow 状態を取得する。
   * @param workflowId workflow ID。
   * @returns workflow 状態。
   */
  public async getWorkflowStatus(workflowId: string): Promise<secretCoreWorkflowStatusResult> {
    return await this.secretCoreIpcClient.sendRequest<secretCoreWorkflowStatusResult>("get_workflow_status", { workflowId });
  }
}
