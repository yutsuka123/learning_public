/**
 * @file deviceRegistry.ts
 * @description ESP32の状態一覧を保持し、offline判定を提供する。
 * @remarks
 * - [重要] 受信が途絶えた端末を自動でofflineへ遷移させる。
 * - [厳守] Offline判定閾値は設定値で変更可能にする。
 * - [禁止] 不正JSON受信でプロセス全体を停止させない。
 */

import { deviceState, otaProgressMessage, statusMessage, trhMessage } from "./types";

/**
 * @description デバイス状態のインメモリ管理クラス。
 */
export class DeviceRegistry {
  private readonly deviceMap = new Map<string, deviceState>();
  private readonly offlineTimeoutMs: number;
  private readonly enableLocalOfflineTimeout: boolean;

  /**
   * @description コンストラクタ。
   * @param statusOfflineTimeoutSeconds 受信途絶時にofflineへ遷移する秒数。
   */
  public constructor(statusOfflineTimeoutSeconds: number, enableLocalOfflineTimeout: boolean = true) {
    this.offlineTimeoutMs = Math.max(1, statusOfflineTimeoutSeconds) * 1000;
    this.enableLocalOfflineTimeout = enableLocalOfflineTimeout;
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
      publicId: this.resolveInitialPublicId(status.publicId, status.macAddr, normalizedDeviceName),
      configVersion: status.configVersion,
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
      statusTopic: status.topic,
      temperatureC: previousState?.temperatureC ?? null,
      humidityRh: previousState?.humidityRh ?? null,
      pressureHpa: previousState?.pressureHpa ?? null,
      environmentSensorId: previousState?.environmentSensorId ?? "",
      environmentSensorAddress: previousState?.environmentSensorAddress ?? "",
      environmentUpdatedAt: previousState?.environmentUpdatedAt ?? ""
    };

    if (previousState !== undefined && nextState.firmwareVersion.length === 0) {
      nextState.firmwareVersion = previousState.firmwareVersion;
    }
    if (previousState !== undefined && nextState.publicId.length === 0) {
      nextState.publicId = previousState.publicId;
    }
    if (previousState !== undefined && nextState.configVersion.length === 0) {
      nextState.configVersion = previousState.configVersion;
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
      publicId: previousState?.publicId ?? this.resolveInitialPublicId("", previousState?.macAddr ?? "", normalizedDeviceName),
      configVersion: previousState?.configVersion ?? "",
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
      statusTopic: previousState?.statusTopic ?? "",
      temperatureC: previousState?.temperatureC ?? null,
      humidityRh: previousState?.humidityRh ?? null,
      pressureHpa: previousState?.pressureHpa ?? null,
      environmentSensorId: previousState?.environmentSensorId ?? "",
      environmentSensorAddress: previousState?.environmentSensorAddress ?? "",
      environmentUpdatedAt: previousState?.environmentUpdatedAt ?? ""
    };
    this.deviceMap.set(normalizedDeviceName, nextState);
    return nextState;
  }

  /**
   * @description 環境センサー(notice/trh)受信でレジストリを部分更新する。
   * @param trh 受信した正規化trh。
   * @returns 更新後のデバイス状態。
   */
  public updateByTrh(trh: trhMessage): deviceState {
    const normalizedDeviceName = trh.topic.split("/").at(-1) ?? trh.srcId;
    const previousState = this.deviceMap.get(normalizedDeviceName);
    const isSuccess = trh.result.toUpperCase() === "OK";
    const nextState: deviceState = {
      deviceName: normalizedDeviceName,
      publicId: previousState?.publicId ?? this.resolveInitialPublicId("", previousState?.macAddr ?? "", normalizedDeviceName),
      configVersion: previousState?.configVersion ?? "",
      srcId: trh.srcId,
      dstId: trh.dstId,
      macAddr: previousState?.macAddr ?? "",
      ipAddress: previousState?.ipAddress ?? "",
      wifiSsid: previousState?.wifiSsid ?? "",
      firmwareVersion: previousState?.firmwareVersion ?? "",
      firmwareWrittenAt: previousState?.firmwareWrittenAt ?? "",
      runningPartition: previousState?.runningPartition ?? "",
      bootPartition: previousState?.bootPartition ?? "",
      nextUpdatePartition: previousState?.nextUpdatePartition ?? "",
      otaProgressPercent: previousState?.otaProgressPercent ?? null,
      otaPhase: previousState?.otaPhase ?? "",
      otaDetail: previousState?.otaDetail ?? "",
      otaUpdatedAt: previousState?.otaUpdatedAt ?? "",
      onlineState: previousState?.onlineState ?? "online",
      detail: previousState?.detail ?? "",
      lastMessageId: trh.messageId,
      lastStatusSub: previousState?.lastStatusSub ?? "",
      lastSeenAt: trh.receivedAt,
      statusTopic: previousState?.statusTopic ?? "",
      temperatureC: isSuccess ? trh.temperatureC : previousState?.temperatureC ?? null,
      humidityRh: isSuccess ? trh.humidityRh : previousState?.humidityRh ?? null,
      pressureHpa: isSuccess ? trh.pressureHpa : previousState?.pressureHpa ?? null,
      environmentSensorId: trh.sensorId.length > 0 ? trh.sensorId : previousState?.environmentSensorId ?? "",
      environmentSensorAddress: trh.sensorAddress.length > 0 ? trh.sensorAddress : previousState?.environmentSensorAddress ?? "",
      environmentUpdatedAt: trh.receivedAt
    };
    this.deviceMap.set(normalizedDeviceName, nextState);
    return nextState;
  }

  /**
   * @description 完成済み deviceState スナップショットで上書き更新する。
   * @param nextState Rust 等で統合済みのデバイス状態。
   * @returns 保持後の状態。
   */
  public upsertDeviceState(nextState: deviceState): deviceState {
    this.deviceMap.set(nextState.deviceName, nextState);
    return nextState;
  }

  /**
   * @description 現在保持している全デバイス状態を返す。
   * @returns デバイス状態配列。
   */
  public listDevices(): deviceState[] {
    if (this.enableLocalOfflineTimeout) {
      this.updateOfflineByTimeout();
    }
    return Array.from(this.deviceMap.values()).sort((leftDevice, rightDevice) => {
      return leftDevice.deviceName.localeCompare(rightDevice.deviceName);
    });
  }

  /**
   * @description 全デバイス名を返す。
   * @returns デバイス名配列。
   */
  public listDeviceNames(): string[] {
    if (this.enableLocalOfflineTimeout) {
      this.updateOfflineByTimeout();
    }
    return this.listDevices().map((deviceStateItem) => deviceStateItem.deviceName);
  }

  /**
   * @description status受信途絶端末をofflineへ更新する。
   */
  public updateOfflineByTimeout(): void {
    if (!this.enableLocalOfflineTimeout) {
      return;
    }
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

  /**
   * @description 受信済みpublicIdが空欄なら、MAC由来の初期名へ補完する。
   * @param receivedPublicId 受信したpublicId。
   * @param macAddr 受信したMACアドレス。
   * @param fallbackDeviceName MACが欠損した場合の最終フォールバック。
   * @returns 保存すべきpublicId。
   */
  private resolveInitialPublicId(receivedPublicId: string, macAddr: string, fallbackDeviceName: string): string {
    const trimmedPublicId = receivedPublicId.trim();
    if (trimmedPublicId.length > 0) {
      return trimmedPublicId;
    }
    const initialPublicId = this.createInitialPublicIdFromMacAddress(macAddr);
    if (initialPublicId.length > 0) {
      return initialPublicId;
    }
    return fallbackDeviceName;
  }

  /**
   * @description MACアドレス文字列から `IoT_<MACコロン除去>` を生成する。
   * @param macAddr MACアドレス文字列。
   * @returns 初期publicId。MAC不正時は空文字。
   */
  private createInitialPublicIdFromMacAddress(macAddr: string): string {
    const normalizedMac = macAddr.replace(/[^0-9a-fA-F]/g, "").trim();
    if (normalizedMac.length === 0) {
      return "";
    }
    return `IoT_${normalizedMac.toUpperCase()}`;
  }

}
