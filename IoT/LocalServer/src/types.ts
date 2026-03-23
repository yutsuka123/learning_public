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
  publicId: string;
  configVersion: string;
  srcId: string;
  dstId: string;
  macAddr: string;
  ipAddress: string;
  wifiSsid: string;
  firmwareVersion: string;
  firmwareWrittenAt: string;
  runningPartition: string;
  bootPartition: string;
  nextUpdatePartition: string;
  otaProgressPercent: number | null;
  otaPhase: string;
  otaDetail: string;
  otaUpdatedAt: string;
  onlineState: deviceOnlineState;
  detail: string;
  lastMessageId: string;
  lastStatusSub: string;
  lastSeenAt: string;
  statusTopic: string;
  /** BME280温度(℃)。未取得時null。 */
  temperatureC: number | null;
  /** BME280相対湿度(%)。未取得時null。 */
  humidityRh: number | null;
  /** BME280気圧(hPa)。未取得時null。 */
  pressureHpa: number | null;
  /** 環境センサーID。 */
  environmentSensorId: string;
  /** 環境センサーI2Cアドレス。 */
  environmentSensorAddress: string;
  /** 環境データ最終取得日時(ISO8601)。 */
  environmentUpdatedAt: string;
}

/**
 * @description MQTT受信メッセージの正規化結果。
 */
export interface statusMessage {
  topic: string;
  srcId: string;
  dstId: string;
  messageId: string;
  publicId: string;
  configVersion: string;
  macAddr: string;
  ipAddress: string;
  wifiSsid: string;
  firmwareVersion: string;
  firmwareWrittenAt: string;
  runningPartition: string;
  bootPartition: string;
  nextUpdatePartition: string;
  onlineState: string;
  statusSub: string;
  detail: string;
  receivedAt: string;
}

/**
 * @description 環境センサー(BME280) notice/trh 受信の正規化結果。
 */
export interface trhMessage {
  topic: string;
  srcId: string;
  dstId: string;
  messageId: string;
  result: string;
  detail: string;
  sensorId: string;
  sensorAddress: string;
  temperatureC: number | null;
  humidityRh: number | null;
  pressureHpa: number | null;
  receivedAt: string;
}

/**
 * @description OTA進捗通知の正規化結果。
 */
export interface otaProgressMessage {
  topic: string;
  srcId: string;
  dstId: string;
  messageId: string;
  firmwareVersion: string;
  progressPercent: number | null;
  phase: string;
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
 * @description 汎用set/get/networkコマンドAPIの要求パラメータ。
 */
export interface genericCommandRequestBody extends commandRequestBody {
  subCommand: string;
  args?: Record<string, unknown>;
}

/**
 * @description OTAコマンドAPIの要求パラメータ。
 */
export interface otaCommandRequestBody extends commandRequestBody {
  manifestUrl?: string;
  firmwareUrl?: string;
  firmwareVersion?: string;
  sha256?: string;
}

/**
 * @description AP単体設定投入APIの要求パラメータ。
 */
export interface apConfigureRequestBody {
  ssid: string;
  wifiSsid: string;
  wifiPass: string;
  mqttUrl: string;
  mqttUrlName?: string;
  mqttUser: string;
  mqttPass: string;
  mqttPort: number;
  mqttTls: boolean;
  mqttTlsCaCertPem?: string;
  mqttTlsCertIssueNo?: string;
  mqttTlsCertSetAt?: string;
  serverUrl?: string;
  serverUrlName?: string;
  serverUser?: string;
  serverPass?: string;
  serverPort?: number;
  serverTls?: boolean;
  otaUrl?: string;
  otaUrlName?: string;
  otaUser?: string;
  otaPass?: string;
  otaPort?: number;
  otaTls?: boolean;
  timeServerUrl?: string;
  timeServerUrlName?: string;
  timeServerPort?: number;
  timeServerTls?: boolean;
  targetDeviceName?: string;
  keyDeviceBase64?: string;
  requestReboot?: boolean;
}

/**
 * @description Pairing workflow の Wi-Fi 設定入力。
 */
export interface pairingRequestedWifiSettings {
  ssid: string;
  username?: string;
  password: string;
}

/**
 * @description Pairing workflow の接続先設定入力。
 */
export interface pairingRequestedEndpointSettings {
  host: string;
  hostName?: string;
  port: number;
  tls: boolean;
  username: string;
  password: string;
  caCertRef?: string;
}

/**
 * @description Pairing workflow のサーバー接続設定入力。
 */
export interface pairingRequestedServerSettings {
  host: string;
  hostName?: string;
  port: number;
  tls: boolean;
}

/**
 * @description Pairing workflow の認証情報入力。
 */
export interface pairingRequestedCredentials {
  wifiUsername?: string;
  wifiPassword: string;
  mqttUsername: string;
  mqttPassword: string;
  otaUsername: string;
  otaPassword: string;
  keyDevice: string;
}

/**
 * @description Pairing workflow の要求設定全体。
 */
export interface pairingRequestedSettings {
  wifi: pairingRequestedWifiSettings;
  mqtt: pairingRequestedEndpointSettings;
  ota: pairingRequestedEndpointSettings;
  credentials: pairingRequestedCredentials;
  server?: pairingRequestedServerSettings;
  ntp?: pairingRequestedServerSettings;
}

/**
 * @description Pairing workflow 開始APIの要求パラメータ。
 */
export interface pairingWorkflowStartRequestBody {
  targetDeviceId: string;
  sessionId: string;
  keyVersion: string;
  requestedSettings: pairingRequestedSettings;
}

/**
 * @description KeyRotation workflow 開始APIの要求パラメータ。
 * @remarks
 * - [重要] 現段階では Pairing と同じ設定入力を再利用し、新 `keyVersion` への切替対象を表す。
 * - [厳守] raw key を含めず、必要設定と識別子のみを渡す。
 */
export interface keyRotationWorkflowStartRequestBody {
  targetDeviceId: string;
  sessionId: string;
  keyVersion: string;
  requestedSettings: pairingRequestedSettings;
}

/**
 * @description Production workflow の事前チェック観測値。
 * @remarks
 * - [重要] `ProductionTool` の 8 項目事前チェックを TS から Rust へ最小DTOで渡すための型。
 * - [厳守] 秘密値や eFuse 実値は含めず、可否判定と補助測定値のみを扱う。
 */
export interface productionWorkflowPrecheckSnapshot {
  targetDeviceMatched?: boolean;
  powerStable?: boolean;
  firmwareVersionApproved?: boolean;
  keyIdVerified?: boolean;
  unsecuredStateConfirmed?: boolean;
  operatorAuthenticated?: boolean;
  stackMarginOk?: boolean;
  heapMarginOk?: boolean;
  measuredFreeHeapBytes?: number;
  measuredMinStackMarginBytes?: number;
}

/**
 * @description Production workflow の実行設定。
 * @remarks
 * - [重要] 高リスク本体は Rust 側で段階実行するため、TS 側は設定値と dry-run 意図だけを渡す。
 * - [禁止] eFuse 実値や中間秘密をここへ含めない。
 */
export interface productionWorkflowSettings {
  dryRun: boolean;
  stepPlan?: string[];
  operatorComment?: string;
  expectedSerial?: string;
  expectedMac?: string;
  expectedFirmwareVersion?: string;
  minimumFreeHeapBytes?: number;
  minimumStackMarginBytes?: number;
  apBaseUrl?: string;
  apUsername?: string;
  apPassword?: string;
  precheckSnapshot?: productionWorkflowPrecheckSnapshot;
}

/**
 * @description Production workflow 開始APIの要求パラメータ。
 */
export interface productionWorkflowStartRequestBody {
  targetDeviceId: string;
  runId: string;
  productionSettings: productionWorkflowSettings;
}

/**
 * @description rollback試験モード切替APIの要求パラメータ。
 */
export interface rollbackTestCommandRequestBody extends commandRequestBody {
  mode: "enable" | "disable";
}

/**
 * @description OTA配布ファームウェアの取得方式。
 */
export type firmwareSourceType = "localPath" | "uploadedFile";

/**
 * @description LocalServerの永続設定モデル。
 */
export interface localServerSettings {
  timeZone: string;
  firmwareSource: firmwareSourceType;
  firmwareLocalPath: string;
  firmwareUploadedFileName: string;
  otaFirmwareVersion: string;
  wifiUsbInterfaceName: string;
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
