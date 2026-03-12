/**
 * @file deviceRegistry.ts
 * @description ESP32の状態一覧を保持し、offline判定を提供する。
 * @remarks
 * - [重要] 受信が途絶えた端末を自動でofflineへ遷移させる。
 * - [厳守] Offline判定閾値は設定値で変更可能にする。
 * - [禁止] 不正JSON受信でプロセス全体を停止させない。
 */

import { deviceState, otaProgressMessage, statusMessage } from "./types";

/**
 * @description デバイス状態のインメモリ管理クラス。
 */
export class DeviceRegistry {
  private readonly deviceMap = new Map<string, deviceState>();
  private readonly offlineTimeoutMs: number;

  /**
   * @description コンストラクタ。
   * @param statusOfflineTimeoutSeconds 受信途絶時にofflineへ遷移する秒数。
   */
  public constructor(statusOfflineTimeoutSeconds: number) {
    this.offlineTimeoutMs = Math.max(1, statusOfflineTimeoutSeconds) * 1000;
  }

  /**
   * @description status受信内容でレジストリを更新する。
   * @param status 受信した正規化status。
   * @returns 更新後のデバイス状態。
   */
  public updateByStatus(status: statusMessage): deviceState {
    const normalizedDeviceName = status.topic.split("/").at(-1) ?? status.srcId;
    const normalizedOnlineState = this.normalizeOnlineState(status.onlineState);
    const previousState = this.deviceMap.get(normalizedDeviceName);
    const normalizedStatusSub = status.statusSub.trim().toLowerCase();
    const shouldFinalizeOtaByStartup =
      normalizedOnlineState === "online" &&
      normalizedStatusSub === "start-up" &&
      this.isOtaSuccessStartup(previousState);

    const nextState: deviceState = {
      deviceName: normalizedDeviceName,
      srcId: status.srcId,
      dstId: status.dstId,
      macAddr: status.macAddr,
      ipAddress: status.ipAddress,
      wifiSsid: status.wifiSsid,
      firmwareVersion: status.firmwareVersion,
      firmwareWrittenAt: status.firmwareWrittenAt,
      runningPartition: status.runningPartition,
      bootPartition: status.bootPartition,
      nextUpdatePartition: status.nextUpdatePartition,
      otaProgressPercent: shouldFinalizeOtaByStartup ? 100 : previousState?.otaProgressPercent ?? null,
      otaPhase: shouldFinalizeOtaByStartup ? "done" : previousState?.otaPhase ?? "",
      otaDetail: shouldFinalizeOtaByStartup ? "rebooted after ota" : previousState?.otaDetail ?? "",
      // [重要] done通知を受信済みなら、その受信時刻(otaUpdatedAt)を保持して画面表示に使う。
      otaUpdatedAt: shouldFinalizeOtaByStartup ? previousState?.otaUpdatedAt ?? status.receivedAt : previousState?.otaUpdatedAt ?? "",
      onlineState: normalizedOnlineState,
      detail: status.detail,
      lastMessageId: status.messageId,
      lastStatusSub: status.statusSub,
      lastSeenAt: status.receivedAt,
      statusTopic: status.topic
    };

    if (previousState !== undefined && nextState.firmwareVersion.length === 0) {
      nextState.firmwareVersion = previousState.firmwareVersion;
    }
    if (previousState !== undefined && nextState.firmwareWrittenAt.length === 0) {
      nextState.firmwareWrittenAt = previousState.firmwareWrittenAt;
    }

    this.deviceMap.set(normalizedDeviceName, nextState);
    return nextState;
  }

  /**
   * @description OTA進捗通知でレジストリを部分更新する。
   * @param otaProgress 受信した正規化OTA進捗。
   * @returns 更新後のデバイス状態。
   */
  public updateByOtaProgress(otaProgress: otaProgressMessage): deviceState {
    const normalizedDeviceName = otaProgress.topic.split("/").at(-1) ?? otaProgress.srcId;
    const previousState = this.deviceMap.get(normalizedDeviceName);
    const nextState: deviceState = {
      deviceName: normalizedDeviceName,
      srcId: otaProgress.srcId,
      dstId: otaProgress.dstId,
      macAddr: previousState?.macAddr ?? "",
      ipAddress: previousState?.ipAddress ?? "",
      wifiSsid: previousState?.wifiSsid ?? "",
      firmwareVersion: previousState?.firmwareVersion ?? "",
      firmwareWrittenAt: previousState?.firmwareWrittenAt ?? "",
      runningPartition: previousState?.runningPartition ?? "",
      bootPartition: previousState?.bootPartition ?? "",
      nextUpdatePartition: previousState?.nextUpdatePartition ?? "",
      otaProgressPercent: otaProgress.progressPercent,
      otaPhase: otaProgress.phase,
      otaDetail: otaProgress.detail,
      otaUpdatedAt: otaProgress.receivedAt,
      onlineState: previousState?.onlineState ?? "unknown",
      detail: previousState?.detail ?? "",
      lastMessageId: otaProgress.messageId,
      lastStatusSub: previousState?.lastStatusSub ?? "",
      lastSeenAt: previousState?.lastSeenAt ?? "",
      statusTopic: previousState?.statusTopic ?? ""
    };
    this.deviceMap.set(normalizedDeviceName, nextState);
    return nextState;
  }

  /**
   * @description 現在保持している全デバイス状態を返す。
   * @returns デバイス状態配列。
   */
  public listDevices(): deviceState[] {
    this.updateOfflineByTimeout();
    return Array.from(this.deviceMap.values()).sort((leftDevice, rightDevice) => {
      return leftDevice.deviceName.localeCompare(rightDevice.deviceName);
    });
  }

  /**
   * @description 全デバイス名を返す。
   * @returns デバイス名配列。
   */
  public listDeviceNames(): string[] {
    this.updateOfflineByTimeout();
    return this.listDevices().map((deviceStateItem) => deviceStateItem.deviceName);
  }

  /**
   * @description status受信途絶端末をofflineへ更新する。
   */
  public updateOfflineByTimeout(): void {
    const currentTimeMs = Date.now();
    for (const [deviceName, deviceStatus] of this.deviceMap.entries()) {
      if (deviceStatus.lastSeenAt.length === 0) {
        continue;
      }
      const elapsedMs = currentTimeMs - Date.parse(deviceStatus.lastSeenAt);
      if (Number.isFinite(elapsedMs) && elapsedMs > this.offlineTimeoutMs && deviceStatus.onlineState !== "offline") {
        this.deviceMap.set(deviceName, {
          ...deviceStatus,
          onlineState: "offline",
          detail: "Offline timeout",
          lastStatusSub: "timeout"
        });
      }
    }
  }

  /**
   * @description MQTT status値を管理用onlineStateへ変換する。
   * @param onlineStateText MQTT受信の状態文字列。
   * @returns 正規化結果。
   */
  private normalizeOnlineState(onlineStateText: string): "online" | "offline" | "unknown" {
    const normalizedText = onlineStateText.trim().toLowerCase();
    if (normalizedText.includes("online")) {
      return "online";
    }
    if (normalizedText.includes("offline") || normalizedText.includes("disconnect")) {
      return "offline";
    }
    return "unknown";
  }

  /**
   * @description OTA成功後の再起動statusかどうかを判定する。
   * @param previousState 直前の保持状態。
   * @returns OTA完了へ収束させるべき場合true。
   */
  private isOtaSuccessStartup(previousState: deviceState | undefined): boolean {
    if (previousState === undefined) {
      return false;
    }
    const normalizedPhase = previousState.otaPhase.trim().toLowerCase();
    if (normalizedPhase === "write" || normalizedPhase === "verify" || normalizedPhase === "done") {
      return true;
    }
    return false;
  }
}
