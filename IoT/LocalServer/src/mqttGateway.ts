/**
 * @file mqttGateway.ts
 * @description MQTT接続・受信解析・コマンド送信を担当する。
 * @remarks
 * - [重要] 起動時にstatus要求を送信し、初期オンライン判定を短時間で確立する。
 * - [厳守] MQTT TLS利用時はCA証明書を読み込み、検証スキップを行わない。
 * - [禁止] publish失敗を黙殺しない。必ずエラーを返す。
 */

import fs from "fs";
import { EventEmitter } from "events";
import mqtt, { MqttClient, IClientOptions } from "mqtt";
import { appConfig } from "./config";
import { DeviceRegistry } from "./deviceRegistry";
import { mqttCommandPayload, otaProgressMessage, statusMessage } from "./types";

export interface secureEchoMessage {
  topic: string;
  srcId: string;
  requestId: string;
  ivBase64: string;
  cipherBase64: string;
  tagBase64: string;
  receivedAt: string;
}

/**
 * @description MQTTイベントの型。
 */
interface mqttGatewayEvents {
  statusUpdated: [status: statusMessage];
  otaProgressUpdated: [otaProgress: otaProgressMessage];
  secureEchoUpdated: [secureEcho: secureEchoMessage];
  connected: [];
  disconnected: [];
}

type mqttCommandKind = "call" | "set" | "get" | "network";
const commandPublishQos: 1 = 1;

/**
 * @description 型付きEventEmitterヘルパー。
 */
class typedEmitter extends EventEmitter {
  public emit<EventKey extends keyof mqttGatewayEvents>(eventName: EventKey, ...args: mqttGatewayEvents[EventKey]): boolean {
    return super.emit(eventName, ...args);
  }

  public on<EventKey extends keyof mqttGatewayEvents>(eventName: EventKey, listener: (...args: mqttGatewayEvents[EventKey]) => void): this {
    return super.on(eventName, listener);
  }
}

/**
 * @description MQTT入出力を扱うゲートウェイクラス。
 */
export class mqttGateway {
  private readonly config: appConfig;
  private readonly registry: DeviceRegistry;
  private readonly emitter = new typedEmitter();
  private readonly client: MqttClient;
  private readonly sourceId: string;
  private readonly pendingSecureEchoMap = new Map<
    string,
    {
      resolve: (secureEcho: secureEchoMessage) => void;
      reject: (error: Error) => void;
      timeoutHandle: NodeJS.Timeout;
    }
  >();

  /**
   * @description コンストラクタ。
   * @param config アプリ設定。
   * @param registry 状態レジストリ。
   */
  public constructor(config: appConfig, registry: DeviceRegistry) {
    this.config = config;
    this.registry = registry;
    this.sourceId = config.sourceId;
    this.client = this.createClient();
    this.registerClientHandlers();
  }

  /**
   * @description MQTT接続を開始する。
   */
  public connect(): void {
    if (this.client.connected) {
      return;
    }
    this.client.reconnect();
  }

  /**
   * @description MQTTイベント購読を登録する。
   * @param eventName イベント名。
   * @param listener 受信ハンドラ。
   */
  public on<EventKey extends keyof mqttGatewayEvents>(eventName: EventKey, listener: (...args: mqttGatewayEvents[EventKey]) => void): void {
    this.emitter.on(eventName, listener);
  }

  /**
   * @description status要求コマンドを送信する。
   * @param targetNames 送信先デバイス名。`all`で全体要求。
   */
  public async requestStatus(targetNames: string[] | "all"): Promise<void> {
    await this.publishCommand("call", "status", targetNames, {
      requestType: "statusRequest",
      reason: "manualOrStartup"
    });
  }

  /**
   * @description OTA開始コマンドを送信する。
   * @param targetNames 送信先デバイス名。
   * @param args OTA引数。
   */
  public async requestOta(
    targetNames: string[] | "all",
    args: {
      manifestUrl: string;
      firmwareUrl: string;
      firmwareVersion: string;
      sha256: string;
      timeoutSeconds: number;
    }
  ): Promise<void> {
    await this.publishCommand("call", "otaStart", targetNames, {
      requestType: "otaStart",
      manifestUrl: args.manifestUrl,
      firmwareUrl: args.firmwareUrl,
      firmwareVersion: args.firmwareVersion,
      sha256: args.sha256,
      timeoutSeconds: args.timeoutSeconds
    });
  }

  /**
   * @description 任意callコマンドを送信する。
   * @param targetNames 送信先デバイス名。
   * @param subCommand callサブコマンド。
   * @param args 追加引数。
   */
  public async requestCall(targetNames: string[] | "all", subCommand: string, args: Record<string, unknown>): Promise<void> {
    await this.publishCommand("call", subCommand, targetNames, args);
  }

  /**
   * @description 任意setコマンドを送信する。
   * @param targetNames 送信先デバイス名。
   * @param subCommand setサブコマンド。
   * @param args 追加引数。
   */
  public async requestSet(targetNames: string[] | "all", subCommand: string, args: Record<string, unknown>): Promise<void> {
    await this.publishCommand("set", subCommand, targetNames, args);
  }

  /**
   * @description 任意getコマンドを送信する。
   * @param targetNames 送信先デバイス名。
   * @param subCommand getサブコマンド。
   * @param args 追加引数。
   */
  public async requestGet(targetNames: string[] | "all", subCommand: string, args: Record<string, unknown>): Promise<void> {
    await this.publishCommand("get", subCommand, targetNames, args);
  }

  /**
   * @description 任意networkコマンドを送信する。
   * @param targetNames 送信先デバイス名。
   * @param subCommand networkサブコマンド。
   * @param args 追加引数。
   */
  public async requestNetwork(targetNames: string[] | "all", subCommand: string, args: Record<string, unknown>): Promise<void> {
    await this.publishCommand("network", subCommand, targetNames, args);
  }

  /**
   * @description 暗号化 securePing を送信し、対応する secureEcho を待機する。
   * @param targetName 宛先デバイス名。
   * @param requestId 要求ID。
   * @param encryptedArgs 暗号化引数。
   * @param timeoutMs 応答待機タイムアウト。
   * @returns 受信したsecureEcho。
   */
  public async requestSecurePing(
    targetName: string,
    requestId: string,
    encryptedArgs: { ivBase64: string; cipherBase64: string; tagBase64: string },
    timeoutMs: number
  ): Promise<secureEchoMessage> {
    if (requestId.trim().length === 0) {
      throw new Error("requestSecurePing failed. requestId is empty.");
    }
    if (timeoutMs <= 0) {
      throw new Error(`requestSecurePing failed. timeoutMs must be greater than 0. timeoutMs=${timeoutMs}`);
    }
    const waitPromise = this.waitForSecureEcho(requestId, timeoutMs);
    await this.publishCommand("call", "securePing", [targetName], {
      requestType: "securePing",
      requestId,
      enc: {
        alg: "A256GCM",
        iv: encryptedArgs.ivBase64,
        ct: encryptedArgs.cipherBase64,
        tag: encryptedArgs.tagBase64
      }
    });
    return await waitPromise;
  }

  /**
   * @description MQTT publish共通処理。
   * @param commandKind コマンド種別。
   * @param subCommand サブコマンド名。
   * @param targetNames 送信先。
   * @param args 追加引数。
   */
  private async publishCommand(
    commandKind: mqttCommandKind,
    subCommand: string,
    targetNames: string[] | "all",
    args: Record<string, unknown>
  ): Promise<void> {
    const destinationList = this.resolveDestinationList(targetNames);
    if (destinationList.length === 0) {
      throw new Error(`publishCommand failed. no destination found. commandKind=${commandKind} subCommand=${subCommand}`);
    }

    const publishTasks = destinationList.map(async (destinationName) => {
      const nextPayload: mqttCommandPayload = {
        v: 1,
        DstID: destinationName,
        SrcID: this.sourceId,
        id: this.createMessageId(),
        ts: new Date().toISOString(),
        op: commandKind,
        sub: subCommand,
        args
      };
      const nextTopic = `esp32lab/${commandKind}/${subCommand}/${destinationName}`;
      await this.publish(nextTopic, JSON.stringify(nextPayload));
    });

    await Promise.all(publishTasks);
  }

  /**
   * @description 送信対象のデバイス名配列を返す。
   * @param targetNames API入力。
   * @returns 宛先配列。
   */
  private resolveDestinationList(targetNames: string[] | "all"): string[] {
    if (targetNames === "all") {
      const knownDeviceNames = this.registry.listDeviceNames();
      return knownDeviceNames.length === 0 ? ["all"] : knownDeviceNames;
    }
    return targetNames.filter((targetName) => targetName.trim().length > 0);
  }

  /**
   * @description MQTT publishをPromise化する。
   * @param topic 送信トピック。
   * @param payload JSON文字列。
   */
  private async publish(topic: string, payload: string): Promise<void> {
    await new Promise<void>((resolve, reject) => {
      // [厳守] コマンド publish は QoS1 固定で送信する。
      this.client.publish(topic, payload, { qos: commandPublishQos }, (publishError) => {
        if (publishError !== undefined && publishError !== null) {
          reject(
            new Error(
              `publish failed. topic=${topic} qos=${commandPublishQos} payloadLength=${payload.length} reason=${publishError.message}`
            )
          );
          return;
        }
        resolve();
      });
    });
  }

  /**
   * @description MQTTクライアントを生成する。
   * @returns MQTTクライアント。
   */
  private createClient(): MqttClient {
    const connectHost = this.config.mqttFallbackIp.length > 0 ? this.config.mqttFallbackIp : this.config.mqttHost;
    const brokerUrl = `${this.config.mqttProtocol}://${connectHost}:${this.config.mqttPort}`;
    const options: IClientOptions = {
      username: this.config.mqttUsername,
      password: this.config.mqttPassword,
      reconnectPeriod: 3000,
      connectTimeout: this.config.mqttConnectTimeoutMs,
      clientId: `${this.config.sourceId}-${Math.floor(Date.now() / 1000)}`,
      manualConnect: true
    };

    if (this.config.mqttProtocol === "mqtts") {
      const caBuffer = fs.readFileSync(this.config.mqttCaPath);
      options.ca = caBuffer;
      options.rejectUnauthorized = true;
      if (this.config.mqttFallbackIp.length > 0) {
        options.servername = this.config.mqttHost;
      }
    }

    return mqtt.connect(brokerUrl, options);
  }

  /**
   * @description MQTTイベントを登録する。
   */
  private registerClientHandlers(): void {
    this.client.on("connect", () => {
      this.subscribeStatusTopics();
      this.emitter.emit("connected");
    });

    this.client.on("close", () => {
      this.emitter.emit("disconnected");
    });

    this.client.on("error", (clientError) => {
      console.error(`mqttGateway client error. message=${clientError.message}`);
    });

    this.client.on("message", (topic, payloadBuffer) => {
      try {
        const payloadText = payloadBuffer.toString("utf8");
        if (topic.includes("/notice/otaProgress/")) {
          const parsedOtaProgress = this.parseOtaProgressMessage(topic, payloadText);
          this.registry.updateByOtaProgress(parsedOtaProgress);
          this.emitter.emit("otaProgressUpdated", parsedOtaProgress);
          return;
        }
        if (topic.includes("/notice/secureEcho/")) {
          const parsedSecureEcho = this.parseSecureEchoMessage(topic, payloadText);
          this.resolvePendingSecureEcho(parsedSecureEcho);
          this.emitter.emit("secureEchoUpdated", parsedSecureEcho);
          return;
        }
        const parsedStatus = this.parseStatusMessage(topic, payloadText);
        this.registry.updateByStatus(parsedStatus);
        this.emitter.emit("statusUpdated", parsedStatus);
      } catch (messageError) {
        const errorMessage = messageError instanceof Error ? messageError.message : String(messageError);
        console.error(`mqttGateway onMessage parse failed. topic=${topic} error=${errorMessage}`);
      }
    });
  }

  /**
   * @description status通知トピック購読を設定する。
   */
  private subscribeStatusTopics(): void {
    const topicList = ["esp32lab/notice/status/+", "esp32lab/notice/+/+"];
    for (const topicName of topicList) {
      this.client.subscribe(topicName, { qos: 1 }, (subscribeError) => {
        if (subscribeError !== undefined && subscribeError !== null) {
          console.error(`subscribeStatusTopics failed. topic=${topicName} reason=${subscribeError.message}`);
        }
      });
    }
  }

  /**
   * @description status通知JSONを正規化する。
   * @param topic MQTTトピック。
   * @param payloadText JSON文字列。
   * @returns 正規化status。
   */
  private parseStatusMessage(topic: string, payloadText: string): statusMessage {
    const parsedObject = JSON.parse(payloadText) as Record<string, unknown>;
    const nowText = new Date().toISOString();
    const srcId = this.getStringValue(parsedObject, "SrcID", "unknown-source");
    const dstId = this.getStringValue(parsedObject, "DstID", "");
    const messageId = this.getStringValue(parsedObject, "id", `${srcId}-${Date.now()}`);
    const onlineState = this.getStringValue(parsedObject, "onlineState", "unknown");
    const statusSub = this.getStringValue(parsedObject, "sub", "status");
    const firmwareVersion =
      this.getStringValue(parsedObject, "fwVersion", "") ||
      this.getStringValue(parsedObject, "firmwareVersion", "");
    const firmwareWrittenAt =
      this.getStringValue(parsedObject, "fwWrittenAt", "") ||
      this.getStringValue(parsedObject, "firmwareWrittenAt", "");

    return {
      topic,
      srcId,
      dstId,
      messageId,
      macAddr: this.getStringValue(parsedObject, "macAddr", ""),
      ipAddress: this.getStringValue(parsedObject, "ipAddress", ""),
      wifiSsid: this.getStringValue(parsedObject, "wifiSsid", ""),
      firmwareVersion,
      firmwareWrittenAt,
      runningPartition: this.getStringValue(parsedObject, "runningPartition", ""),
      bootPartition: this.getStringValue(parsedObject, "bootPartition", ""),
      nextUpdatePartition: this.getStringValue(parsedObject, "nextUpdatePartition", ""),
      onlineState,
      statusSub,
      detail: this.getStringValue(parsedObject, "detail", ""),
      receivedAt: nowText
    };
  }

  /**
   * @description OTA進捗通知JSONを正規化する。
   * @param topic MQTTトピック。
   * @param payloadText JSON文字列。
   * @returns 正規化OTA進捗。
   */
  private parseOtaProgressMessage(topic: string, payloadText: string): otaProgressMessage {
    const parsedObject = JSON.parse(payloadText) as Record<string, unknown>;
    const srcId = this.getStringValue(parsedObject, "SrcID", "unknown-source");
    const dstId = this.getStringValue(parsedObject, "DstID", "");
    const receivedAt = new Date().toISOString();
    return {
      topic,
      srcId,
      dstId,
      messageId: this.getStringValue(parsedObject, "id", `${srcId}-${Date.now()}`),
      firmwareVersion:
        this.getStringValue(parsedObject, "fwVersion", "") ||
        this.getStringValue(parsedObject, "firmwareVersion", ""),
      progressPercent: this.getNumberValue(parsedObject, "progressPercent"),
      phase: this.getStringValue(parsedObject, "phase", ""),
      detail: this.getStringValue(parsedObject, "detail", ""),
      receivedAt
    };
  }

  /**
   * @description secureEcho通知JSONを正規化する。
   * @param topic MQTTトピック。
   * @param payloadText JSON文字列。
   * @returns 正規化secureEcho。
   */
  private parseSecureEchoMessage(topic: string, payloadText: string): secureEchoMessage {
    const parsedObject = JSON.parse(payloadText) as Record<string, unknown>;
    const srcId = this.getStringValue(parsedObject, "SrcID", "unknown-source");
    const requestId = this.getStringValue(parsedObject, "requestId", "");
    const encObject = (parsedObject.enc as Record<string, unknown> | undefined) ?? {};
    const ivBase64 = this.getStringValue(encObject, "iv", "");
    const cipherBase64 = this.getStringValue(encObject, "ct", "");
    const tagBase64 = this.getStringValue(encObject, "tag", "");
    if (requestId.length === 0) {
      throw new Error("parseSecureEchoMessage failed. requestId is empty.");
    }
    if (ivBase64.length === 0 || cipherBase64.length === 0 || tagBase64.length === 0) {
      throw new Error("parseSecureEchoMessage failed. encrypted field is missing.");
    }
    return {
      topic,
      srcId,
      requestId,
      ivBase64,
      cipherBase64,
      tagBase64,
      receivedAt: new Date().toISOString()
    };
  }

  /**
   * @description requestId待ちのsecureEcho Promiseを解決する。
   * @param secureEcho 受信メッセージ。
   */
  private resolvePendingSecureEcho(secureEcho: secureEchoMessage): void {
    const pending = this.pendingSecureEchoMap.get(secureEcho.requestId);
    if (pending === undefined) {
      return;
    }
    clearTimeout(pending.timeoutHandle);
    this.pendingSecureEchoMap.delete(secureEcho.requestId);
    pending.resolve(secureEcho);
  }

  /**
   * @description requestId指定で secureEcho を待機する。
   * @param requestId 要求ID。
   * @param timeoutMs タイムアウト。
   * @returns Promise。
   */
  private waitForSecureEcho(requestId: string, timeoutMs: number): Promise<secureEchoMessage> {
    return new Promise<secureEchoMessage>((resolve, reject) => {
      if (this.pendingSecureEchoMap.has(requestId)) {
        reject(new Error(`waitForSecureEcho failed. duplicate requestId=${requestId}`));
        return;
      }
      const timeoutHandle = setTimeout(() => {
        this.pendingSecureEchoMap.delete(requestId);
        reject(new Error(`waitForSecureEcho timeout. requestId=${requestId} timeoutMs=${timeoutMs}`));
      }, timeoutMs);
      this.pendingSecureEchoMap.set(requestId, {
        resolve,
        reject,
        timeoutHandle
      });
    });
  }

  /**
   * @description 任意オブジェクトから文字列キーを安全に取り出す。
   * @param sourceObject 取得対象。
   * @param keyName キー名。
   * @param fallbackValue 未定義時の既定値。
   * @returns 文字列値。
   */
  private getStringValue(sourceObject: Record<string, unknown>, keyName: string, fallbackValue: string): string {
    const rawValue = sourceObject[keyName];
    return typeof rawValue === "string" ? rawValue : fallbackValue;
  }

  /**
   * @description 任意オブジェクトから数値キーを安全に取り出す。
   * @param sourceObject 取得対象。
   * @param keyName キー名。
   * @returns 数値値。未定義時はnull。
   */
  private getNumberValue(sourceObject: Record<string, unknown>, keyName: string): number | null {
    const rawValue = sourceObject[keyName];
    return typeof rawValue === "number" && Number.isFinite(rawValue) ? rawValue : null;
  }

  /**
   * @description メッセージIDを生成する。
   * @returns 生成済みID。
   */
  private createMessageId(): string {
    return `${this.sourceId}-${Date.now()}-${Math.floor(Math.random() * 100000)}`;
  }
}
