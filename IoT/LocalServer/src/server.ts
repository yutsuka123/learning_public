/**
 * @file server.ts
 * @description LocalServer本体。Web UI・REST API・MQTT連携・OTA配布エンドポイント・設定復旧導線を提供する。
 * @remarks
 * - [重要] 起動時にstatus要求を送信し、ESP32オンライン状態を初期同期する。
 * - [厳守] OTA配布はHTTPSを優先し、証明書未配置時は警告を出して無効化する。
 * - [禁止] OTA配布URLへ機密情報（認証値）を埋め込まない。
 * - [重要][2026-05-17] 管理者向け `POST /api/admin/local-history/export` で SQLite 履歴をフィルタ付き JSON Lines へ出力する（DB仕様書 3章）。環境変数 `LOCAL_HISTORY_SCHEDULED_EXPORT_ENABLED` 等で同一処理を定期実行する。
 * - [重要][2026-05-17] SecretCore 子プロセスは **stdin ブートストラップの stdout ack** を待ってから `checkHealth` を行い、未更新の `secret_core.exe` による Named Pipe 名不一致を早期終了する。
 */

import fs from "fs";
import path from "path";
import https from "https";
import crypto from "crypto";
import os from "os";
import net from "net";
import dgram from "dgram";
import { exec, spawnSync } from "child_process";
import { promisify } from "util";
import express, { Request, Response } from "express";
import multer from "multer";
import { WebSocketServer } from "ws";
import { loadConfig, appConfig } from "./config";
import { S3Client, PutObjectCommand, GetObjectCommand } from "@aws-sdk/client-s3";
import { getSignedUrl } from "@aws-sdk/s3-request-presigner";
import { DeviceRegistry } from "./deviceRegistry";
import { deviceTransport } from "./deviceTransport";
import { mqttGateway } from "./mqttGateway";
import { createCloudMqttGateway } from "./cloudMqttSubscriber";
import { SettingsStore } from "./settingsStore";
import { keyService } from "./keyService";
import {
  apConfigureRequestBody,
  commandRequestBody,
  keyRotationWorkflowStartRequestBody,
  recoveryReRegistrationPlanRequestBody,
  recoveryReRegistrationExecuteRequestBody,
  recoveryReRegistrationRestoreRequestBody,
  otaCommandRequestBody,
  otaTamperedSignatureTestRequestBody,
  settingTamperedSignatureTestRequestBody,
  pairingRequestedSettings,
  pairingWorkflowStartRequestBody,
  productionWorkflowPrecheckSnapshot,
  productionWorkflowStartRequestBody,
  rollbackTestCommandRequestBody,
  localServerSettings,
  genericCommandRequestBody,
  recoveryReRegistrationExecuteResponse,
  localHistoryExportRequestBody,
  localHistoryDeleteDatabaseRequestBody
} from "./types";
import { SecretCoreManager } from "./secretCoreManager";
import { SecretCoreIpcClient } from "./secretCoreIpcClient";
import { SecretCoreFacade, secretCoreWorkflowStatusResult } from "./secretCoreFacade";
import { buildPairingWorkflowStartRequestBodyFromApConfigure, validatePairingWorkflowStartRequestBody } from "./pairingWorkflowInput";
import { mqttPayloadSecurityService, resolveMqttPayloadEncryptionMode } from "./mqttPayloadSecurity";
import { LocalHistoryStore, type historyExportQuery } from "./localHistoryStore";
import { formatRecordedAtJstFromIsoUtc } from "./historyTimestamps";

const config = loadConfig();
/** @description 定期履歴エクスポートの `exportHistory.executedBy` および監査ログ用ラベル。 */
const LOCAL_HISTORY_SCHEDULED_EXECUTOR = "LocalServer/scheduledExport";
const app = express();
// [Phase2完了] SecretCoreにTS版k-userをインポート済み。wrapped_k_user.binで一致確認済み。
// Phase1で一時的にfalseにしていたものをtrueに復元。変更日: 2026-03-15
const USE_SECRET_CORE = true;
// [PhaseC進行中] MQTT 通信を SecretCore(Rust) へ段階移行する。
// Stage1 は command publish、Stage2 は subscribe/受信イベント取り込みまで Rust 側へ移す。変更日: 2026-03-15
const MQTT_TRANSPORT_MODE: "ts" | "rust" = USE_SECRET_CORE ? "rust" : "ts";
const registry = new DeviceRegistry(config.statusOfflineTimeoutSeconds, MQTT_TRANSPORT_MODE !== "rust");
const settingsStore = new SettingsStore(config);
const secretCoreManager = new SecretCoreManager();
const secretCoreClient = new SecretCoreIpcClient(
  () => secretCoreManager.getPipeName(),
  () => secretCoreManager.getIpcSessionKeyBase64()
);
const secretCoreFacade = new SecretCoreFacade(secretCoreClient);
const localKeyService = new keyService(config, secretCoreFacade, USE_SECRET_CORE);
const serverPayloadSecurityService = new mqttPayloadSecurityService(localKeyService, resolveMqttPayloadEncryptionMode());
const LOCAL_HISTORY_DELETE_DB_CONFIRM = "DELETE_LOCAL_HISTORY_DB";
const localHistoryStore = new LocalHistoryStore(config, settingsStore.getSettings().localHistoryRetentionDays);

// [重要] ゲートウェイは動的モード切替のため let で保持する。
// 初期モードは settings.json の brokerMode を優先し、未保存時は CLOUD_MQTT_ENABLED から決定する。
let gateway: deviceTransport = buildGateway(settingsStore.getSettings().brokerMode);
const adminSessionMap = new Map<string, number>();
const adminSessionTtlMs = 3 * 60 * 60 * 1000;
const adminLoginLockoutThreshold = 3;
const adminLoginLockoutDurationMs = 5 * 60 * 1000;
const securityStateFilePath = path.resolve(process.cwd(), "data", "securityState.json");
const securityAuditDirectoryPath = path.resolve(process.cwd(), "logs");
const securityAuditFilePath = path.join(securityAuditDirectoryPath, "security-audit.log");
const execAsync = promisify(exec);
const apHttpRequestTimeoutMs = 8000;
const uploadDirectoryPath = path.resolve(process.cwd(), "uploads");
if (!fs.existsSync(uploadDirectoryPath)) {
  fs.mkdirSync(uploadDirectoryPath, { recursive: true });
}
const firmwareUploadMiddleware = multer({
  storage: multer.diskStorage({
    destination: (_request, _file, callback) => {
      callback(null, uploadDirectoryPath);
    },
    filename: (_request, file, callback) => {
      const timestampText = new Date().toISOString().replace(/[:.]/g, "-");
      const normalizedName = file.originalname.replace(/[^a-zA-Z0-9._-]/g, "_");
      callback(null, `${timestampText}_${normalizedName}`);
    }
  }),
  limits: {
    fileSize: 16 * 1024 * 1024
  }
});

interface deviceDbBackupFileSpec {
  readonly fileName: string;
  readonly required: boolean;
}

interface deviceDbBackupManifestItem {
  fileName: string;
  copied: boolean;
  required: boolean;
  sizeBytes: number;
}

interface deviceDbBackupExportResult {
  backupDir: string;
  manifestPath: string;
  memoPath: string;
  fileCount: number;
}

interface deviceDbBackupRestoreResult {
  backupDir: string;
  restoredFileNames: string[];
  restoredFileCount: number;
}

const localServerDataDirectoryPath = path.resolve(process.cwd(), "data");
const localServerSecureBackupDirectoryPath = path.join(localServerDataDirectoryPath, "secure-backups");
const deviceDbBackupFileSpecs: ReadonlyArray<deviceDbBackupFileSpec> = [
  { fileName: "settings.json", required: true },
  { fileName: "securityState.json", required: true },
  { fileName: "keyStore.json", required: true },
  { fileName: "wrapped_secret.bin", required: false },
  { fileName: "wrapped_k_user.bin", required: false }
];

/**
 * @description バックアップや証跡ファイル名に使うタイムスタンプ文字列を返す。
 * @returns タイムスタンプ文字列。
 */
function createTimestampText(): string {
  return new Date().toISOString().replace(/[:.]/g, "-");
}

/**
 * @description 指定ディレクトリを必要に応じて作成する。
 * @param directoryPath 作成対象ディレクトリ。
 */
function ensureDirectoryExists(directoryPath: string): void {
  if (!fs.existsSync(directoryPath)) {
    fs.mkdirSync(directoryPath, { recursive: true });
  }
}

/**
 * @description 受け取ったバックアップルートを絶対パスへ正規化する。
 * @param backupRootText ユーザー指定のバックアップルート。
 * @returns 絶対パス。
 */
function resolveBackupRootDirectoryPath(backupRootText?: string): string {
  if (backupRootText === undefined || backupRootText.trim().length === 0) {
    return localServerSecureBackupDirectoryPath;
  }
  return path.isAbsolute(backupRootText) ? backupRootText : path.resolve(process.cwd(), backupRootText);
}

/**
 * @description device_db バックアップの最新スナップショットを探す。
 * @param backupRootDirectoryPath バックアップルート。
 * @returns 最新スナップショットの絶対パス。
 */
function resolveLatestDeviceDbBackupDirectoryPath(backupRootDirectoryPath: string): string {
  if (!fs.existsSync(backupRootDirectoryPath)) {
    throw new Error(`device_db backup root is not found. path=${backupRootDirectoryPath}`);
  }
  const latestSnapshotName = fs
    .readdirSync(backupRootDirectoryPath, { withFileTypes: true })
    .filter((dirent) => dirent.isDirectory() && dirent.name.startsWith("device-db-backup-"))
    .map((dirent) => dirent.name)
    .sort((left, right) => right.localeCompare(left))[0];
  if (latestSnapshotName === undefined) {
    throw new Error(`No device_db backup snapshot is found. path=${backupRootDirectoryPath}`);
  }
  return path.join(backupRootDirectoryPath, latestSnapshotName);
}

/**
 * @description device_db バックアップを作成する。
 * @param backupRootText バックアップルートの指定。
 * @returns バックアップ結果。
 */
function createDeviceDbBackupSnapshot(backupRootText?: string): deviceDbBackupExportResult {
  const backupRootDirectoryPath = resolveBackupRootDirectoryPath(backupRootText);
  ensureDirectoryExists(backupRootDirectoryPath);

  const timestampText = createTimestampText();
  const backupDir = path.join(backupRootDirectoryPath, `device-db-backup-${timestampText}`);
  ensureDirectoryExists(backupDir);

  const manifestItems: deviceDbBackupManifestItem[] = [];
  for (const fileSpec of deviceDbBackupFileSpecs) {
    const sourcePath = path.join(localServerDataDirectoryPath, fileSpec.fileName);
    const destinationPath = path.join(backupDir, fileSpec.fileName);
    if (fs.existsSync(sourcePath)) {
      fs.copyFileSync(sourcePath, destinationPath);
      const fileInfo = fs.statSync(sourcePath);
      manifestItems.push({
        fileName: fileSpec.fileName,
        copied: true,
        required: fileSpec.required,
        sizeBytes: fileInfo.size
      });
      continue;
    }
    if (fileSpec.required) {
      throw new Error(`Required device_db file is not found. path=${sourcePath}`);
    }
    manifestItems.push({
      fileName: fileSpec.fileName,
      copied: false,
      required: false,
      sizeBytes: 0
    });
  }

  const manifestPath = path.join(backupDir, "backup-manifest.json");
  const manifest = {
    backupType: "device-db-snapshot",
    version: 1,
    createdAt: new Date().toISOString(),
    installRoot: process.cwd(),
    sourceDataDirectory: localServerDataDirectoryPath,
    files: manifestItems
  };
  fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");

  const memoPath = path.join(backupDir, `restore-memo-device-db-${timestampText}.md`);
  const memoText = [
    `# 復旧メモ (${timestampText})`,
    "",
    "- [重要] 目的: LocalServer の device_db を復旧可能な形で退避する。",
    "- [厳守] このメモには機密値そのものを記載しない。",
    "- [重要] バックアップ対象:",
    ...manifestItems.map((item) => `  - ${item.fileName} (copied=${item.copied}, required=${item.required}, sizeBytes=${item.sizeBytes})`),
    "- [重要] 復元時は settings.html または restore-device-db-after-7090.ps1 相当の導線を使う。",
    "- [推奨] 復元後は settings.json、securityState.json、keyStore.json、wrapped_secret.bin、wrapped_k_user.bin の整合を確認する。",
    "- [禁止] 退避先を通常のサポートパッケージと混在させない。"
  ].join("\n");
  fs.writeFileSync(memoPath, `${memoText}\n`, "utf8");

  return {
    backupDir,
    manifestPath,
    memoPath,
    fileCount: manifestItems.length
  };
}

/**
 * @description device_db バックアップを復元する。
 * @param backupDirText 復元元のバックアップディレクトリ。
 * @returns 復元結果。
 */
function restoreDeviceDbBackupSnapshot(backupDirText?: string): deviceDbBackupRestoreResult {
  const backupRootDirectoryPath = resolveBackupRootDirectoryPath(undefined);
  const backupDir = backupDirText && backupDirText.trim().length > 0
    ? (path.isAbsolute(backupDirText) ? backupDirText : path.resolve(process.cwd(), backupDirText))
    : resolveLatestDeviceDbBackupDirectoryPath(backupRootDirectoryPath);

  if (!fs.existsSync(backupDir)) {
    throw new Error(`device_db backup directory is not found. path=${backupDir}`);
  }

  ensureDirectoryExists(localServerDataDirectoryPath);

  const restoredFileNames: string[] = [];
  for (const fileSpec of deviceDbBackupFileSpecs) {
    const backupFilePath = path.join(backupDir, fileSpec.fileName);
    const destinationPath = path.join(localServerDataDirectoryPath, fileSpec.fileName);
    if (fs.existsSync(backupFilePath)) {
      fs.copyFileSync(backupFilePath, destinationPath);
      restoredFileNames.push(fileSpec.fileName);
      continue;
    }
    if (fileSpec.required) {
      throw new Error(`Required device_db backup file is not found. path=${backupFilePath}`);
    }
  }

  const manifestPath = path.join(backupDir, "backup-manifest.json");
  if (fs.existsSync(manifestPath)) {
    fs.copyFileSync(manifestPath, path.join(localServerDataDirectoryPath, "device-db-restore-manifest.json"));
  }

  return {
    backupDir,
    restoredFileNames,
    restoredFileCount: restoredFileNames.length
  };
}

app.use(express.json({ limit: "1mb" }));
app.use(express.static(path.resolve(process.cwd(), "public")));

interface adminLoginRequestBody {
  username: string;
  password: string;
}

interface adminLoginLockState {
  username: string;
  remoteAddress: string;
  consecutiveFailureCount: number;
  lockedUntilEpochMs: number;
}

interface adminPasswordChangeRequestBody {
  currentPassword: string;
  newPassword: string;
  reason?: string;
  scope?: string;
  expiresAt?: string;
}

interface apRolePasswordChangeRequestBody {
  ssid: string;
  role: "user" | "maintenance" | "admin" | "mfg";
  currentPassword: string;
  newPassword: string;
  reason?: string;
  scope?: string;
  expiresAt?: string;
}

interface credentialsRotationRequestBody {
  targetType: string;
  targetId: string;
  reason: string;
  scope?: string;
  expiresAt?: string;
}

interface securityState {
  adminUsername: string;
  adminPassword: string;
  apRoleAdminUsername: string;
  apRoleAdminPassword: string;
}

interface issueKDeviceRequestBody {
  targetDeviceName: string;
  pushToDevice?: boolean;
}

interface deviceDbBackupRequestBody {
  backupRoot?: string;
}

interface deviceDbRestoreRequestBody {
  backupDir?: string;
}

interface kUserBackupRequestBody {
  backupPassword: string;
  backupFilePath?: string;
}

interface recoveryRegistrationPlanStepDetail {
  title: string;
  endpoint: string;
  note: string;
}

interface recoveryRegistrationPlanResult {
  mode: "same-pc" | "external-device";
  title: string;
  summary: string;
  steps: recoveryRegistrationPlanStepDetail[];
}

interface recoveryRegistrationRestoreResult {
  mode: "same-pc" | "external-device";
  deviceDb?: deviceDbBackupRestoreResult;
  kUser?: {
    imported: true;
    keyFingerprint: string;
    source: string;
  };
}

interface securePingRequestBody {
  targetDeviceName: string;
  plainText?: string;
  timeoutMs?: number;
}

interface wifiUsbSelectionRequestBody {
  interfaceName: string;
}

interface apConnectRequestBody {
  ssid: string;
}

interface apBatchNetworkSettings {
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
}

interface apBatchStartRequestBody {
  targetSsids?: string[];
  targetDeviceNameBySsid?: Record<string, string>;
  networkSettings: apBatchNetworkSettings;
  requestReboot?: boolean;
  statusWaitTimeoutSeconds?: number;
}

interface apBatchItemResult {
  ssid: string;
  status: "pending" | "processing" | "completed" | "failed";
  targetDeviceName: string;
  publicId: string;
  firmwareVersion: string;
  configVersion: string;
  errorDetail: string;
  updatedAt: string;
}

interface apBatchRunResult {
  batchId: string;
  startedAt: string;
  finishedAt: string;
  requestReboot: boolean;
  statusWaitTimeoutSeconds: number;
  totalCount: number;
  completedCount: number;
  failedCount: number;
  itemResults: apBatchItemResult[];
}

interface apConnectivityDiagnosticRequestBody {
  mqttUrl: string;
  mqttPort: number;
  timeServerUrl: string;
  timeServerPort: number;
}

interface apManagedFileUpsertRequestBody {
  ssid: string;
  targetArea: "images" | "certs";
  path: string;
  dataBase64: string;
  expectedSha256?: string;
}

interface apManagedFileDeleteRequestBody {
  ssid: string;
  targetArea: "images" | "certs";
  path: string;
}

const runtimeSecurityState = loadSecurityState();
const adminLoginLockStateMap = new Map<string, adminLoginLockState>();
const apBatchRunMap = new Map<string, apBatchRunResult>();

/**
 * @description 障害時再登録フローの案内を組み立てる。
 * @param requestBody 再登録案内要求。
 * @returns 案内結果。
 */
function buildRecoveryReRegistrationPlan(requestBody: recoveryReRegistrationPlanRequestBody): recoveryRegistrationPlanResult {
  const targetDeviceId = (requestBody.targetDeviceId ?? "").trim();
  const targetDeviceName = (requestBody.targetDeviceName ?? "").trim();
  const keyVersion = (requestBody.keyVersion ?? "").trim();
  if (requestBody.mode === "same-pc") {
    return {
      mode: "same-pc",
      title: "同一PC復旧プラン",
      summary: "wrapped_secret/device_db を先に戻し、必要なら key-rotation で current/previous 整合を閉じる。",
      steps: [
        {
          title: "device_db を復元する",
          endpoint: "POST /api/settings/backups/device-db/restore",
          note: "settings.html から device_db を復元し、wrapped_secret.bin / wrapped_k_user.bin を含む最小集合を戻す。"
        },
        {
          title: "k-user 状態を確認する",
          endpoint: "GET /api/admin/keys/k-user/status",
          note: "発行済みか、fingerprint と deviceKeyCount が期待どおりかを確認する。"
        },
        {
          title: "必要なら key-rotation を実行する",
          endpoint: "POST /api/workflows/key-rotation/start",
          note: `targetDeviceId=${targetDeviceId || "(未指定)"} targetDeviceName=${targetDeviceName || "(未指定)"} keyVersion=${keyVersion || "(未指定)"}`.trim()
        },
        {
          title: "workflow の完了判定を確認する",
          endpoint: "GET /api/workflows/{workflowId}",
          note: "completed/OK か failed を確認し、失敗時は証跡を残して再試行可否を判断する。"
        }
      ]
    };
  }
  return {
    mode: "external-device",
    title: "別PC・機材交換時の再登録プラン",
    summary: "暗号化バックアップから k-user を復元し、対象個体ごとに pairing をやり直す。",
    steps: [
      {
        title: "k-user 暗号化バックアップを復元する",
        endpoint: "POST /api/settings/backups/k-user/import",
        note: "LocalServer の settings.html または管理画面の導線から暗号化バックアップファイルを戻す。"
      },
      {
        title: "対象個体の pairing をやり直す",
        endpoint: "POST /api/workflows/pairing/start",
        note: `targetDeviceId=${targetDeviceId || "(未指定)"} targetDeviceName=${targetDeviceName || "(未指定)"}`.trim()
      },
      {
        title: "k-device / 状態更新を確認する",
        endpoint: "GET /api/pairing/state",
        note: "raw key を表示せず、completed/OK と AP 側 state=applied を確認する。"
      },
      {
        title: "workflow の完了判定を確認する",
        endpoint: "GET /api/workflows/{workflowId}",
        note: "completed/OK か failed を確認し、失敗時は証跡を残して再試行可否を判断する。"
      }
    ]
  };
}

/**
 * @description 障害時再登録フローの復元処理を実行する。
 * @param requestBody 復元要求。
 * @returns 復元結果。
 */
async function runRecoveryReRegistrationRestore(
  requestBody: recoveryReRegistrationRestoreRequestBody
): Promise<recoveryRegistrationRestoreResult> {
  const mode = requestBody.mode;
  const restoreDeviceDb = requestBody.restoreDeviceDb === true;
  const restoreKUser = requestBody.restoreKUser === true;
  if (!restoreDeviceDb && !restoreKUser) {
    throw new Error("runRecoveryReRegistrationRestore failed. at least one restore target is required.");
  }

  const restoreResult: recoveryRegistrationRestoreResult = {
    mode
  };

  if (restoreDeviceDb) {
    restoreResult.deviceDb = restoreDeviceDbBackupSnapshot(requestBody.deviceDbBackupDir);
  }

  if (restoreKUser) {
    const backupPassword = (requestBody.kUserBackupPassword ?? "").trim();
    const backupFilePath = (requestBody.kUserBackupFilePath ?? "").trim();
    if (backupPassword.length === 0) {
      throw new Error("runRecoveryReRegistrationRestore failed. kUserBackupPassword is required when restoreKUser is true.");
    }
    if (backupFilePath.length === 0) {
      throw new Error("runRecoveryReRegistrationRestore failed. kUserBackupFilePath is required when restoreKUser is true.");
    }
    restoreResult.kUser = await localKeyService.importKUserBackup(backupPassword, backupFilePath);
  }

  return restoreResult;
}

/**
 * @description Pairing workflow を直接開始する。
 * @param rawRequestBody workflow 開始要求の生データ。
 * @returns workflow 状態。
 */
async function startPairingWorkflowFromRequestBody(
  rawRequestBody: Partial<pairingWorkflowStartRequestBody & apConfigureRequestBody & { keyDeviceBase64?: string }>
): Promise<secretCoreWorkflowStatusResult> {
  if (!USE_SECRET_CORE) {
    throw new Error("pairing workflow start failed. SecretCore is disabled.");
  }
  const requestBody = await resolvePairingWorkflowStartRequestBody(rawRequestBody);
  return await secretCoreFacade.runPairingSession(requestBody);
}

/**
 * @description KeyRotation workflow を直接開始する。
 * @param rawRequestBody workflow 開始要求の生データ。
 * @returns workflow 状態。
 */
async function startKeyRotationWorkflowFromRequestBody(
  rawRequestBody: Partial<keyRotationWorkflowStartRequestBody & apConfigureRequestBody & { keyDeviceBase64?: string }>
): Promise<secretCoreWorkflowStatusResult> {
  if (!USE_SECRET_CORE) {
    throw new Error("key-rotation workflow start failed. SecretCore is disabled.");
  }
  // [重要] 新しい k-user を先に発行し、その後に k-device を再導出した request を workflow へ渡す。
  // 理由: KeyRotation は旧系統の鍵を再投入する処理ではなく、新系統への切替を対象とするため。
  await localKeyService.issueKUser();
  const requestBody = await resolvePairingWorkflowStartRequestBody(rawRequestBody);
  return await secretCoreFacade.runKeyRotationSession(requestBody);
}

/**
 * @description 障害時再登録フローの一括実行を行う。
 * @param requestBody 一括実行要求。
 * @returns 一括実行結果。
 */
async function runRecoveryReRegistrationExecute(
  requestBody: recoveryReRegistrationExecuteRequestBody
): Promise<recoveryReRegistrationExecuteResponse> {
  const workflowRequestBody = requestBody.workflowRequestBody ?? {};
  if (requestBody.mode === "same-pc") {
    const restoreResult = await runRecoveryReRegistrationRestore({
      mode: requestBody.mode,
      restoreDeviceDb: requestBody.restoreDeviceDb,
      deviceDbBackupDir: requestBody.deviceDbBackupDir,
      restoreKUser: false
    });
    const workflowStatus = await startKeyRotationWorkflowFromRequestBody(
      workflowRequestBody as Partial<keyRotationWorkflowStartRequestBody & apConfigureRequestBody & { keyDeviceBase64?: string }>
    );
    return {
      mode: requestBody.mode,
      restore: restoreResult,
      workflow: {
        workflowId: workflowStatus.workflowId,
        workflowType: "key-rotation",
        state: workflowStatus.state,
        result: workflowStatus.result,
        errorSummary: workflowStatus.errorSummary,
        detail: workflowStatus.detail
      }
    };
  }
  const restoreResult = await runRecoveryReRegistrationRestore({
    mode: requestBody.mode,
    restoreDeviceDb: false,
    restoreKUser: requestBody.restoreKUser,
    kUserBackupPassword: requestBody.kUserBackupPassword,
    kUserBackupFilePath: requestBody.kUserBackupFilePath
  });
  const workflowStatus = await startPairingWorkflowFromRequestBody(
    workflowRequestBody as Partial<pairingWorkflowStartRequestBody & apConfigureRequestBody & { keyDeviceBase64?: string }>
  );
  return {
    mode: requestBody.mode,
    restore: restoreResult,
    workflow: {
      workflowId: workflowStatus.workflowId,
      workflowType: "pairing",
      state: workflowStatus.state,
      result: workflowStatus.result,
      errorSummary: workflowStatus.errorSummary,
      detail: workflowStatus.detail
    }
  };
}

/**
 * @description [003-0011][厳守] eFuse 操作 API 拒否。
 * eFuse / Secure Boot / Flash Encryption の書込みは ProductionTool 専用。LocalServer では提供しない。
 * 理由: IF仕様書「eFuse 操作 API は LocalServer 側へ実装しない」を担保する。
 */
app.all(/^\/api\/(admin\/efuse|commands\/efuse)/, (_request: Request, response: Response) => {
  response.status(404).json({
    result: "NG",
    detail: "eFuse operations are ProductionTool-only. Not implemented in LocalServer."
  });
});

/**
 * @description APIヘルスチェック。
 */
app.get("/api/health", (_request: Request, response: Response) => {
  response.json({
    status: "ok",
    service: "local-server",
    now: new Date().toISOString()
  });
});

/**
 * @description デバイス一覧を返すAPI。
 * [003-0008] connectionMode / apWebUrl を付与し、APモード機体の Web遷移ボタン表示に利用する。
 */
app.get("/api/devices", (_request: Request, response: Response) => {
  const devices = registry.listDevices().map((device) => {
    const isApMode = device.wifiSsid.startsWith(config.apSsidPrefix);
    return {
      ...device,
      connectionMode: isApMode ? "ap" : "mqtt",
      apWebUrl: isApMode ? "http://192.168.4.1/" : ""
    };
  });
  response.json({ devices });
});

/**
 * @description 現在のローカル設定を返すAPI。
 */
app.get("/api/settings", (_request: Request, response: Response) => {
  try {
    const currentSettings = settingsStore.getSettings();
    const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
    const metadata = readOtaFirmwareMetadata(activeFirmwarePath, false);
    response.json({
      settings: currentSettings,
      activeFirmwarePath,
      firmwareInfo: metadata
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description ローカル設定を更新するAPI。
 */
app.put("/api/settings", (request: Request, response: Response) => {
  try {
    const requestBody = request.body as Partial<localServerSettings>;
    const updatedSettings = settingsStore.updateSettings(requestBody);
    localHistoryStore.setRetentionDays(updatedSettings.localHistoryRetentionDays);
    response.json({
      result: "OK",
      settings: updatedSettings
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description device_db のバックアップを作成するAPI。
 * @remarks
 * - [重要] 退避先は既定で `data/secure-backups` とする。
 * - [厳守] 管理者認証後にのみ実行できる。
 */
app.post("/api/settings/backups/device-db/export", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as deviceDbBackupRequestBody;
    const backupResult = createDeviceDbBackupSnapshot(requestBody?.backupRoot);
    response.json({
      result: "OK",
      ...backupResult
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description device_db のバックアップを復元するAPI。
 * @remarks
 * - [重要] 復元元未指定時は secure-backups 配下の最新スナップショットを使う。
 * - [厳守] 管理者認証後にのみ実行できる。
 */
app.post("/api/settings/backups/device-db/restore", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as deviceDbRestoreRequestBody;
    const restoreResult = restoreDeviceDbBackupSnapshot(requestBody?.backupDir);
    response.json({
      result: "OK",
      ...restoreResult
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description k-user の暗号化バックアップを出力するAPI。
 * @remarks
 * - [重要] 既定の出力先は `data/secure-backups` 配下のタイムスタンプ付きファイルとする。
 * - [厳守] バックアップパスワードが空文字のときは拒否する。
 */
app.post("/api/settings/backups/k-user/export", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as kUserBackupRequestBody;
    const backupPassword = (requestBody?.backupPassword ?? "").trim();
    if (backupPassword.length === 0) {
      throw new Error("k-user backup export failed. backupPassword is required.");
    }
    const backupFilePathText = (requestBody?.backupFilePath ?? "").trim();
    const backupFilePath = backupFilePathText.length > 0
      ? path.resolve(process.cwd(), backupFilePathText)
      : path.join(localServerSecureBackupDirectoryPath, `k-user-backup-${createTimestampText()}.enc.json`);
    const exportResult = await localKeyService.exportKUserBackup(backupPassword, backupFilePath);
    response.json({
      result: "OK",
      ...exportResult,
      securityWarning: [
        "このファイルには機密鍵情報が含まれています。",
        "使用後は即座に安全削除してください（SSD では上書き削除は保証されません）。",
        "長期保管する場合は暗号化ストレージ（BitLocker / FileVault / LUKS）上に置いてください。"
      ].join(" ")
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description k-user の暗号化バックアップを復元するAPI。
 * @remarks
 * - [重要] 復元先は SecretCore が wrapped_k_user.bin に書き戻す。
 * - [厳守] バックアップパスワードとバックアップファイルパスの双方を明示する。
 */
app.post("/api/settings/backups/k-user/import", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as kUserBackupRequestBody;
    const backupPassword = (requestBody?.backupPassword ?? "").trim();
    const backupFilePathText = (requestBody?.backupFilePath ?? "").trim();
    if (backupPassword.length === 0) {
      throw new Error("k-user backup import failed. backupPassword is required.");
    }
    if (backupFilePathText.length === 0) {
      throw new Error("k-user backup import failed. backupFilePath is required.");
    }
    const backupFilePath = path.resolve(process.cwd(), backupFilePathText);
    const importResult = await localKeyService.importKUserBackup(backupPassword, backupFilePath);
    response.json({
      result: "OK",
      ...importResult
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description 管理者ログインAPI。
 */
app.post("/api/admin/auth/login", (request: Request, response: Response) => {
  try {
    const requestBody = request.body as adminLoginRequestBody;
    if (requestBody === undefined || requestBody === null) {
      throw new Error("admin login failed. request body is required.");
    }
    const username = (requestBody.username ?? "").trim();
    const password = requestBody.password ?? "";
    if (username.length === 0 || password.length === 0) {
      throw new Error("admin login failed. username/password is required.");
    }
    const remoteAddress = resolveAdminLoginRemoteAddress(request);
    const adminLoginLockKey = resolveAdminLoginLockKey(username, remoteAddress);
    const activeAdminLoginLock = getActiveAdminLoginLockState(adminLoginLockKey);
    if (activeAdminLoginLock !== null) {
      appendSecurityAuditLog("localAdminLoginLocked", {
        username,
        remoteAddress,
        consecutiveFailureCount: activeAdminLoginLock.consecutiveFailureCount,
        retryAfterSeconds: activeAdminLoginLock.retryAfterSeconds,
        lockedUntil: activeAdminLoginLock.lockedUntilIso,
        reason: "lockout active"
      });
      response.status(403).json({
        result: "NG",
        detail: "too many failed attempts. wait 5 minutes before retrying.",
        retryAfterSeconds: activeAdminLoginLock.retryAfterSeconds,
        lockedUntil: activeAdminLoginLock.lockedUntilIso
      });
      return;
    }
    if (username !== runtimeSecurityState.adminUsername || password !== runtimeSecurityState.adminPassword) {
      const failedLoginResult = registerAdminLoginFailure(adminLoginLockKey, username, remoteAddress);
      if (failedLoginResult.isLocked) {
        appendSecurityAuditLog("localAdminLoginLocked", {
          username,
          remoteAddress,
          consecutiveFailureCount: failedLoginResult.consecutiveFailureCount,
          retryAfterSeconds: failedLoginResult.retryAfterSeconds,
          lockedUntil: failedLoginResult.lockedUntilIso,
          reason: "threshold reached"
        });
        response.status(403).json({
          result: "NG",
          detail: "too many failed attempts. wait 5 minutes before retrying.",
          retryAfterSeconds: failedLoginResult.retryAfterSeconds,
          lockedUntil: failedLoginResult.lockedUntilIso
        });
        return;
      }
      appendSecurityAuditLog("localAdminLoginRejected", {
        username,
        remoteAddress,
        consecutiveFailureCount: failedLoginResult.consecutiveFailureCount,
        remainingAttempts: failedLoginResult.remainingAttempts
      });
      response.status(401).json({
        result: "NG",
        detail: "authentication failed",
        remainingAttempts: failedLoginResult.remainingAttempts
      });
      return;
    }
    clearAdminLoginLockState(adminLoginLockKey);
    const nextToken = crypto.randomUUID();
    adminSessionMap.set(nextToken, Date.now() + adminSessionTtlMs);
    appendSecurityAuditLog("localAdminLoginSucceeded", {
      username,
      remoteAddress,
      sessionExpiresInSeconds: Math.floor(adminSessionTtlMs / 1000)
    });
    response.json({
      result: "OK",
      token: nextToken,
      expiresInSeconds: Math.floor(adminSessionTtlMs / 1000)
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 管理者ログアウトAPI。
 */
app.post("/api/admin/auth/logout", (request: Request, response: Response) => {
  const token = extractAdminToken(request);
  if (token.length > 0) {
    adminSessionMap.delete(token);
  }
  response.json({ result: "OK" });
});

/**
 * @description LocalServer 管理者パスワードを変更するAPI。
 */
app.post("/api/admin/auth/password/change", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as adminPasswordChangeRequestBody;
    const currentPassword = String(requestBody?.currentPassword ?? "");
    const newPassword = String(requestBody?.newPassword ?? "");
    const reason = String(requestBody?.reason ?? "").trim();
    const scope = String(requestBody?.scope ?? "").trim();
    const expiresAt = String(requestBody?.expiresAt ?? "").trim();
    if (currentPassword.length === 0 || newPassword.length === 0) {
      throw new Error("admin password change failed. currentPassword/newPassword is required.");
    }
    if (newPassword.length < 8) {
      throw new Error("admin password change failed. newPassword must be at least 8 characters.");
    }
    if (currentPassword !== runtimeSecurityState.adminPassword) {
      response.status(401).json({
        result: "NG",
        detail: "currentPassword mismatch"
      });
      appendSecurityAuditLog("localAdminPasswordChangeRejected", {
        reason: "currentPassword mismatch",
        changedBy: runtimeSecurityState.adminUsername
      });
      return;
    }
    runtimeSecurityState.adminPassword = newPassword;
    saveSecurityState(runtimeSecurityState);
    adminSessionMap.clear();
    appendSecurityAuditLog("localAdminPasswordChanged", {
      changedBy: runtimeSecurityState.adminUsername,
      reason,
      scope,
      expiresAt,
      result: "OK"
    });
    response.json({
      result: "OK",
      detail: "password changed. re-login is required."
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description APロールパスワードを変更するAPI。
 */
app.post("/api/admin/ap/password/change", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as apRolePasswordChangeRequestBody;
    const ssid = String(requestBody?.ssid ?? "").trim();
    const role = String(requestBody?.role ?? "").trim().toLowerCase();
    const currentPassword = String(requestBody?.currentPassword ?? "");
    const newPassword = String(requestBody?.newPassword ?? "");
    const reason = String(requestBody?.reason ?? "").trim();
    const scope = String(requestBody?.scope ?? "").trim();
    const expiresAt = String(requestBody?.expiresAt ?? "").trim();
    if (ssid.length === 0) {
      throw new Error("ap role password change failed. ssid is required.");
    }
    if (!["user", "maintenance", "admin", "mfg"].includes(role)) {
      throw new Error(`ap role password change failed. invalid role=${role}`);
    }
    if (currentPassword.length === 0 || newPassword.length === 0) {
      throw new Error("ap role password change failed. currentPassword/newPassword is required.");
    }
    if (newPassword.length < 8) {
      throw new Error("ap role password change failed. newPassword must be at least 8 characters.");
    }
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap role password change failed. wifiUsbInterfaceName is empty.");
    }
    await connectToMaintenanceAccessPoint(currentSettings.wifiUsbInterfaceName, ssid);
    const loginResult = await loginToMaintenanceAp();
    await postMaintenanceApJson(
      "/api/auth/password/change",
      {
        role,
        currentPassword,
        newPassword,
        reason,
        scope,
        expiresAt
      },
      loginResult.token
    );
    if (role === "admin") {
      runtimeSecurityState.apRoleAdminPassword = newPassword;
      saveSecurityState(runtimeSecurityState);
    }
    appendSecurityAuditLog("apRolePasswordChanged", {
      ssid,
      role,
      changedBy: runtimeSecurityState.adminUsername,
      reason,
      scope,
      expiresAt,
      result: "OK"
    });
    response.json({
      result: "OK",
      ssid,
      role
    });
  } catch (apiError) {
    appendSecurityAuditLog("apRolePasswordChangeFailed", {
      changedBy: runtimeSecurityState.adminUsername,
      detail: getErrorMessage(apiError)
    });
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 例外的な固定値継続や運用都合を監査ログへ登録するAPI。
 */
app.post("/api/admin/credentials/rotation", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as credentialsRotationRequestBody;
    const targetType = String(requestBody?.targetType ?? "").trim();
    const targetId = String(requestBody?.targetId ?? "").trim();
    const reason = String(requestBody?.reason ?? "").trim();
    const scope = String(requestBody?.scope ?? "").trim();
    const expiresAt = String(requestBody?.expiresAt ?? "").trim();
    if (targetType.length === 0 || targetId.length === 0 || reason.length === 0) {
      throw new Error("credentials rotation failed. targetType/targetId/reason is required.");
    }
    appendSecurityAuditLog("credentialRotationRegistered", {
      changedBy: runtimeSecurityState.adminUsername,
      targetType,
      targetId,
      reason,
      scope,
      expiresAt,
      result: "registered"
    });
    response.json({
      result: "OK",
      targetType,
      targetId,
      detail: "rotation/exception record saved"
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 管理者向けデバイス統合一覧API。
 */
app.get("/api/admin/devices", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const devices = registry.listDevices().map((device) => {
      const isApMode = device.wifiSsid.startsWith(config.apSsidPrefix);
      return {
        ...device,
        connectionMode: isApMode ? "ap" : "mqtt",
        apWebUrl: isApMode ? "http://192.168.4.1/" : ""
      };
    });
    response.json({
      result: "OK",
      devices
    });
  } catch (apiError) {
    response.status(401).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP探索/接続で利用するWi-Fi USB候補一覧を返すAPI。
 */
app.get("/api/admin/wifi-usb/interfaces", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const currentSettings = settingsStore.getSettings();
    const interfaces = await listWifiUsbInterfaceCandidates();
    response.json({
      result: "OK",
      selectedInterfaceName: currentSettings.wifiUsbInterfaceName,
      interfaces
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP探索/接続に使うWi-Fi USBインタフェースを保存するAPI。
 */
app.post("/api/admin/wifi-usb/selection", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as wifiUsbSelectionRequestBody;
    const interfaceName = (requestBody?.interfaceName ?? "").trim();
    const updatedSettings = settingsStore.updateSettings({
      wifiUsbInterfaceName: interfaceName
    });
    response.json({
      result: "OK",
      selectedInterfaceName: updatedSettings.wifiUsbInterfaceName
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 指定Wi-Fi USBで AP-esp32lab-* を探索するAPI。
 */
app.get("/api/admin/ap/scan", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap scan failed. wifiUsbInterfaceName is empty. select interface first.");
    }
    const scanResult = await scanMaintenanceAccessPoints(currentSettings.wifiUsbInterfaceName);
    response.json({
      result: "OK",
      wifiUsbInterfaceName: currentSettings.wifiUsbInterfaceName,
      apList: scanResult
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 指定APへ Wi-Fi USB を接続するAPI。
 */
app.post("/api/admin/ap/connect", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as apConnectRequestBody;
    const ssid = (requestBody?.ssid ?? "").trim();
    if (ssid.length === 0) {
      throw new Error("ap connect failed. ssid is required.");
    }
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap connect failed. wifiUsbInterfaceName is empty.");
    }
    await connectToMaintenanceAccessPoint(currentSettings.wifiUsbInterfaceName, ssid);
    response.json({
      result: "OK",
      wifiUsbInterfaceName: currentSettings.wifiUsbInterfaceName,
      connectedSsid: ssid
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP接続中ESP32から現在のネットワーク設定を取得するAPI。
 */
app.get("/api/admin/ap/settings", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const ssid = String(request.query.ssid ?? "").trim();
    if (ssid.length === 0) {
      throw new Error("ap settings failed. ssid query is required.");
    }
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap settings failed. wifiUsbInterfaceName is empty.");
    }
    await connectToMaintenanceAccessPoint(currentSettings.wifiUsbInterfaceName, ssid);
    const loginResult = await loginToMaintenanceAp();
    const apSettings = await getMaintenanceApJson("/api/settings/network", loginResult.token);
    response.json({
      result: "OK",
      ssid,
      settings: apSettings
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 7065試験用: AP側APIへ未認証でアクセスし、401応答を確認する。
 * 事前: connectToMaintenanceAccessPoint で AP 接続済みであること（ap/settings 等の直後）。
 */
app.get("/api/admin/ap/test-unauth", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const ssid = String(request.query.ssid ?? "").trim();
    const apiPath = String(request.query.path ?? "/api/settings/network").trim();
    if (ssid.length === 0) {
      throw new Error("ap test-unauth failed. ssid query is required.");
    }
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap test-unauth failed. wifiUsbInterfaceName is empty.");
    }
    await connectToMaintenanceAccessPoint(currentSettings.wifiUsbInterfaceName, ssid);
    const fetchResponse = await fetchWithTimeout(`${config.apHttpBaseUrl}${apiPath}`, {
      method: "GET",
      headers: { "Content-Type": "application/json" }
    });
    const bodyText = await fetchResponse.text();
    response.json({
      result: "OK",
      ssid,
      apiPath,
      statusCode: fetchResponse.status,
      bodyPreview: bodyText.slice(0, 200)
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP接続中ESP32へネットワーク設定/k-deviceを投入するAPI。
 */
app.post("/api/admin/ap/configure", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as apConfigureRequestBody;
    const ssid = (requestBody?.ssid ?? "").trim();
    if (ssid.length === 0) {
      throw new Error("ap configure failed. ssid is required.");
    }
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap configure failed. wifiUsbInterfaceName is empty.");
    }
    await connectToMaintenanceAccessPoint(currentSettings.wifiUsbInterfaceName, ssid);
    const loginResult = await loginToMaintenanceAp();
    let keyDeviceBase64 = (requestBody?.keyDeviceBase64 ?? "").trim();
    const targetDeviceName = (requestBody?.targetDeviceName ?? "").trim();
    let keyDeviceSkippedReason = "";
    if (keyDeviceBase64.length === 0 && targetDeviceName.length > 0) {
      try {
        keyDeviceBase64 = await localKeyService.getKDeviceBase64(targetDeviceName);
      } catch (keyDeviceError) {
        const keyDeviceErrorMessage = getErrorMessage(keyDeviceError);
        // [重要] k-device未発行でもネットワーク設定投入は継続する。
        // 理由: 初期導入時にネットワーク先行設定が必要なケースがあるため。
        keyDeviceBase64 = "";
        keyDeviceSkippedReason = keyDeviceErrorMessage;
      }
    }

    const networkPayload = {
      wifiSsid: requestBody.wifiSsid,
      wifiPass: requestBody.wifiPass,
      mqttUrl: requestBody.mqttUrl,
      mqttUrlName: requestBody.mqttUrlName ?? "",
      mqttUser: requestBody.mqttUser,
      mqttPass: requestBody.mqttPass,
      mqttPort: requestBody.mqttPort,
      mqttTls: requestBody.mqttTls,
      mqttTlsCaCertPem: requestBody.mqttTlsCaCertPem ?? "",
      mqttTlsCertIssueNo: requestBody.mqttTlsCertIssueNo ?? "",
      mqttTlsCertSetAt: requestBody.mqttTlsCertSetAt ?? "",
      serverUrl: requestBody.serverUrl ?? "",
      serverUrlName: requestBody.serverUrlName ?? "",
      serverUser: requestBody.serverUser ?? "",
      serverPass: requestBody.serverPass ?? "",
      serverPort: requestBody.serverPort ?? 443,
      serverTls: requestBody.serverTls ?? true,
      otaUrl: requestBody.otaUrl ?? "",
      otaUrlName: requestBody.otaUrlName ?? "",
      otaUser: requestBody.otaUser ?? "",
      otaPass: requestBody.otaPass ?? "",
      otaPort: requestBody.otaPort ?? 443,
      otaTls: requestBody.otaTls ?? true,
      timeServerUrl: requestBody.timeServerUrl ?? "",
      timeServerUrlName: requestBody.timeServerUrlName ?? "",
      timeServerPort: requestBody.timeServerPort ?? 123,
      timeServerTls: requestBody.timeServerTls ?? false,
      keyDevice: keyDeviceBase64
    };
    await postMaintenanceApJson("/api/settings/network", networkPayload, loginResult.token);
    if (requestBody.requestReboot !== false) {
      await postMaintenanceApJson("/api/system/reboot", {}, loginResult.token);
    }
    response.json({
      result: "OK",
      ssid,
      targetDeviceName,
      keyDeviceApplied: keyDeviceBase64.length > 0,
      keyDeviceSkippedReason,
      rebootRequested: requestBody.requestReboot !== false
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP一括メンテナンスを順次実行するAPI。
 */
app.post("/api/admin/ap/batch/start", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as apBatchStartRequestBody;
    if (requestBody === undefined || requestBody === null) {
      throw new Error("ap batch start failed. request body is required.");
    }
    if (requestBody.networkSettings === undefined || requestBody.networkSettings === null) {
      throw new Error("ap batch start failed. networkSettings is required.");
    }
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap batch start failed. wifiUsbInterfaceName is empty.");
    }

    let targetSsidList = Array.isArray(requestBody.targetSsids)
      ? requestBody.targetSsids.map((ssid) => String(ssid ?? "").trim()).filter((ssid) => ssid.length > 0)
      : [];
    if (targetSsidList.length === 0) {
      const scannedApList = await scanMaintenanceAccessPoints(currentSettings.wifiUsbInterfaceName);
      targetSsidList = scannedApList.map((item) => item.ssid);
    }
    if (targetSsidList.length === 0) {
      throw new Error("ap batch start failed. target ssid list is empty.");
    }

    const requestReboot = requestBody.requestReboot !== false;
    const statusWaitTimeoutSeconds = Math.max(10, Number(requestBody.statusWaitTimeoutSeconds ?? 90));
    const batchId = `ap-batch-${Date.now()}-${Math.floor(Math.random() * 100000)}`;
    const startedAt = new Date().toISOString();
    const itemResults: apBatchItemResult[] = targetSsidList.map((ssid) => ({
      ssid,
      status: "pending",
      targetDeviceName: "",
      publicId: "",
      firmwareVersion: "",
      configVersion: "",
      errorDetail: "",
      updatedAt: startedAt
    }));

    for (let index = 0; index < itemResults.length; index += 1) {
      const itemResult = itemResults[index];
      itemResult.status = "processing";
      itemResult.updatedAt = new Date().toISOString();
      try {
        await connectToMaintenanceAccessPoint(currentSettings.wifiUsbInterfaceName, itemResult.ssid);
        const loginResult = await loginToMaintenanceAp();
        const targetDeviceName = String(requestBody.targetDeviceNameBySsid?.[itemResult.ssid] ?? "").trim();
        itemResult.targetDeviceName = targetDeviceName;
        let keyDeviceBase64 = "";
        if (targetDeviceName.length > 0) {
          keyDeviceBase64 = await localKeyService.getKDeviceBase64(targetDeviceName);
        }
        const networkSettings = requestBody.networkSettings;
        await postMaintenanceApJson(
          "/api/settings/network",
          {
            wifiSsid: networkSettings.wifiSsid,
            wifiPass: networkSettings.wifiPass,
            mqttUrl: networkSettings.mqttUrl,
            mqttUrlName: networkSettings.mqttUrlName ?? "",
            mqttUser: networkSettings.mqttUser,
            mqttPass: networkSettings.mqttPass,
            mqttPort: networkSettings.mqttPort,
            mqttTls: networkSettings.mqttTls,
            mqttTlsCaCertPem: networkSettings.mqttTlsCaCertPem ?? "",
            mqttTlsCertIssueNo: networkSettings.mqttTlsCertIssueNo ?? "",
            mqttTlsCertSetAt: networkSettings.mqttTlsCertSetAt ?? "",
            serverUrl: networkSettings.serverUrl ?? "",
            serverUrlName: networkSettings.serverUrlName ?? "",
            serverUser: networkSettings.serverUser ?? "",
            serverPass: networkSettings.serverPass ?? "",
            serverPort: networkSettings.serverPort ?? 443,
            serverTls: networkSettings.serverTls ?? true,
            otaUrl: networkSettings.otaUrl ?? "",
            otaUrlName: networkSettings.otaUrlName ?? "",
            otaUser: networkSettings.otaUser ?? "",
            otaPass: networkSettings.otaPass ?? "",
            otaPort: networkSettings.otaPort ?? 443,
            otaTls: networkSettings.otaTls ?? true,
            timeServerUrl: networkSettings.timeServerUrl ?? "",
            timeServerUrlName: networkSettings.timeServerUrlName ?? "",
            timeServerPort: networkSettings.timeServerPort ?? 123,
            timeServerTls: networkSettings.timeServerTls ?? false,
            keyDevice: keyDeviceBase64
          },
          loginResult.token
        );
        if (requestReboot) {
          await postMaintenanceApJson("/api/system/reboot", {}, loginResult.token);
          const statusResult = await waitForStatusRecoveryByDevice(targetDeviceName, statusWaitTimeoutSeconds);
          itemResult.publicId = statusResult.publicId;
          itemResult.firmwareVersion = statusResult.firmwareVersion;
          itemResult.configVersion = statusResult.configVersion;
        }
        itemResult.status = "completed";
        itemResult.updatedAt = new Date().toISOString();
      } catch (batchError) {
        itemResult.status = "failed";
        itemResult.errorDetail = getErrorMessage(batchError);
        itemResult.updatedAt = new Date().toISOString();
      }
    }

    const completedCount = itemResults.filter((item) => item.status === "completed").length;
    const failedCount = itemResults.filter((item) => item.status === "failed").length;
    const batchResult: apBatchRunResult = {
      batchId,
      startedAt,
      finishedAt: new Date().toISOString(),
      requestReboot,
      statusWaitTimeoutSeconds,
      totalCount: itemResults.length,
      completedCount,
      failedCount,
      itemResults
    };
    apBatchRunMap.set(batchId, batchResult);
    appendSecurityAuditLog("apBatchMaintenanceCompleted", {
      batchId,
      completedCount,
      failedCount,
      totalCount: itemResults.length
    });
    response.json({
      result: "OK",
      ...batchResult
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP一括メンテナンス実行結果を返すAPI。
 */
app.get("/api/admin/ap/batch/:batchId", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const batchId = String(request.params.batchId ?? "").trim();
    if (batchId.length === 0) {
      throw new Error("ap batch get failed. batchId is required.");
    }
    const batchResult = apBatchRunMap.get(batchId);
    if (batchResult === undefined) {
      response.status(404).json({
        result: "NG",
        detail: `batch result not found. batchId=${batchId}`
      });
      return;
    }
    response.json({
      result: "OK",
      ...batchResult
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP接続中ESP32の `/images` `/certs` へ1ファイルを局所更新するAPI。
 */
app.post("/api/admin/ap/files/upsert", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as apManagedFileUpsertRequestBody;
    const ssid = (requestBody?.ssid ?? "").trim();
    const targetArea = String(requestBody?.targetArea ?? "").trim().toLowerCase();
    const managedPath = String(requestBody?.path ?? "").trim();
    const dataBase64 = String(requestBody?.dataBase64 ?? "").trim();
    const expectedSha256 = String(requestBody?.expectedSha256 ?? "").trim().toLowerCase();
    if (ssid.length === 0) {
      throw new Error("ap files upsert failed. ssid is required.");
    }
    if (targetArea !== "images" && targetArea !== "certs") {
      throw new Error(`ap files upsert failed. invalid targetArea=${targetArea}`);
    }
    if (managedPath.length === 0) {
      throw new Error("ap files upsert failed. path is required.");
    }
    if (dataBase64.length === 0) {
      throw new Error("ap files upsert failed. dataBase64 is required.");
    }
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap files upsert failed. wifiUsbInterfaceName is empty.");
    }
    await connectToMaintenanceAccessPoint(currentSettings.wifiUsbInterfaceName, ssid);
    const loginResult = await loginToMaintenanceAp();
    await postMaintenanceApJson(
      "/api/files/upsert",
      {
        targetArea,
        path: managedPath,
        dataBase64,
        expectedSha256
      },
      loginResult.token
    );
    response.json({
      result: "OK",
      ssid,
      targetArea,
      path: managedPath
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP接続中ESP32の `/images` `/certs` から1ファイルを削除するAPI。
 */
app.post("/api/admin/ap/files/delete", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as apManagedFileDeleteRequestBody;
    const ssid = (requestBody?.ssid ?? "").trim();
    const targetArea = String(requestBody?.targetArea ?? "").trim().toLowerCase();
    const managedPath = String(requestBody?.path ?? "").trim();
    if (ssid.length === 0) {
      throw new Error("ap files delete failed. ssid is required.");
    }
    if (targetArea !== "images" && targetArea !== "certs") {
      throw new Error(`ap files delete failed. invalid targetArea=${targetArea}`);
    }
    if (managedPath.length === 0) {
      throw new Error("ap files delete failed. path is required.");
    }
    const currentSettings = settingsStore.getSettings();
    if (currentSettings.wifiUsbInterfaceName.trim().length === 0) {
      throw new Error("ap files delete failed. wifiUsbInterfaceName is empty.");
    }
    await connectToMaintenanceAccessPoint(currentSettings.wifiUsbInterfaceName, ssid);
    const loginResult = await loginToMaintenanceAp();
    await postMaintenanceApJson(
      "/api/files/delete",
      {
        targetArea,
        path: managedPath
      },
      loginResult.token
    );
    response.json({
      result: "OK",
      ssid,
      targetArea,
      path: managedPath
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description AP投入予定のMQTT/NTP到達性をLocalServer側から診断するAPI。
 */
app.post("/api/admin/ap/diagnose-connectivity", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as apConnectivityDiagnosticRequestBody;
    const mqttHost = normalizeHostInput(String(requestBody?.mqttUrl ?? ""));
    const mqttPort = Number(requestBody?.mqttPort ?? 0);
    const timeServerHost = normalizeHostInput(String(requestBody?.timeServerUrl ?? ""));
    const timeServerPort = Number(requestBody?.timeServerPort ?? 0);
    if (mqttHost.length === 0 || mqttPort <= 0) {
      throw new Error("ap diagnose-connectivity failed. mqttUrl/mqttPort is invalid.");
    }
    if (timeServerHost.length === 0 || timeServerPort <= 0) {
      throw new Error("ap diagnose-connectivity failed. timeServerUrl/timeServerPort is invalid.");
    }
    const mqttTcpResult = await testTcpConnectivity(mqttHost, mqttPort, 3000);
    const ntpUdpResult = await testNtpUdpConnectivity(timeServerHost, timeServerPort, 3000);
    response.json({
      result: "OK",
      mqtt: mqttTcpResult,
      ntp: ntpUdpResult
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description k-user発行状態取得API。
 */
app.get("/api/admin/keys/k-user/status", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const status = await localKeyService.getKUserStatus();
    response.json({
      result: "OK",
      ...status
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description k-user発行API。
 */
app.post("/api/admin/keys/k-user/issue", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const issuedResult = await localKeyService.issueKUser();
    response.json({
      result: "OK",
      keyType: "k-user",
      issuedAt: issuedResult.issuedAt,
      keyFingerprint: issuedResult.keyFingerprint
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description k-device発行API。
 */
app.post("/api/admin/keys/k-device/issue", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as issueKDeviceRequestBody;
    const targetDeviceName = (requestBody?.targetDeviceName ?? "").trim();
    if (targetDeviceName.length === 0) {
      throw new Error("k-device issue failed. targetDeviceName is required.");
    }
    const issueResult = await localKeyService.issueKDevice(targetDeviceName);
    const pushToDevice = requestBody?.pushToDevice !== false;
    if (pushToDevice) {
      await gateway.requestSet([targetDeviceName], "keyDeviceSet", {
        keyDevice: issueResult.keyDeviceBase64
      });
    }
    response.json({
      result: "OK",
      keyType: "k-device",
      targetDeviceName: issueResult.targetDeviceName,
      issuedAt: issueResult.issuedAt,
      keyFingerprint: issueResult.keyFingerprint,
      pushedToDevice: pushToDevice
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 障害時再登録フローの案内を返すAPI。
 * @remarks
 * - [重要] 実処理は既存の device_db 退避/復元、k-user 暗号化バックアップ、pairing / key-rotation API を組み合わせる。
 * - [厳守] このAPIは案内専用とし、raw key や秘密復元結果を返さない。
 */
app.post("/api/admin/recovery/re-registration/plan", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as recoveryReRegistrationPlanRequestBody;
    const mode = requestBody?.mode;
    if (mode !== "same-pc" && mode !== "external-device") {
      throw new Error("recovery re-registration plan failed. mode is required.");
    }
    const result = buildRecoveryReRegistrationPlan(requestBody);
    response.json({
      result: "OK",
      ...result
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 障害時再登録フローの復元処理を実行するAPI。
 * @remarks
 * - [重要] 同一PC復旧と別PC再登録のどちらでも、まず保存状態を戻すことを優先する。
 * - [厳守] k-user 復元時は backupPassword と backupFilePath の両方が必要。
 */
app.post("/api/admin/recovery/re-registration/restore", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as recoveryReRegistrationRestoreRequestBody;
    if (requestBody.mode !== "same-pc" && requestBody.mode !== "external-device") {
      throw new Error("recovery re-registration restore failed. mode is required.");
    }
    const restoreResult = await runRecoveryReRegistrationRestore(requestBody);
    response.json({
      result: "OK",
      ...restoreResult
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 障害時再登録フローを復元から workflow 実行まで一括で進めるAPI。
 * @remarks
 * - [重要] `same-pc` は `device_db` 復元後に key-rotation を開始する。
 * - [重要] `external-device` は `k-user` 復元後に pairing を開始する。
 */
app.post("/api/admin/recovery/re-registration/execute", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as recoveryReRegistrationExecuteRequestBody;
    if (requestBody.mode !== "same-pc" && requestBody.mode !== "external-device") {
      throw new Error("recovery re-registration execute failed. mode is required.");
    }
    const executeResult = await runRecoveryReRegistrationExecute(requestBody);
    response.json({
      result: "OK",
      ...executeResult
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description MQTT接続中ESP32へメンテナンス再起動指令を送信するAPI。
 */
app.post("/api/admin/commands/maintenance-reboot", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as commandRequestBody;
    validateCommandBody("maintenance-reboot", requestBody);
    await gateway.requestCall(requestBody.targetNames, "maintenance", {
      requestType: "maintenance"
    });
    response.json({
      result: "OK",
      command: "maintenance",
      targetNames: requestBody.targetNames
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description k-device暗号化で securePing 相互通信確認を行うAPI。
 */
app.post("/api/admin/commands/secure-ping", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as securePingRequestBody;
    const targetDeviceName = (requestBody?.targetDeviceName ?? "").trim();
    if (targetDeviceName.length === 0) {
      throw new Error("secure-ping failed. targetDeviceName is required.");
    }
    const requestId = `secure-ping-${Date.now()}-${Math.floor(Math.random() * 100000)}`;
    const plainPayloadText = JSON.stringify({
      requestId,
      pingText: requestBody?.plainText ?? "secure ping from localserver",
      sentAt: new Date().toISOString()
    });
    const secureEcho = await (async () => {
      const encryptedRequest = await localKeyService.encryptByKDevice(targetDeviceName, plainPayloadText);
      try {
        return await gateway.requestSecurePing(
          targetDeviceName,
          requestId,
          {
            ivBase64: encryptedRequest.ivBase64,
            cipherBase64: encryptedRequest.cipherBase64,
            tagBase64: encryptedRequest.tagBase64
          },
          requestBody?.timeoutMs ?? 15000
        );
      } finally {
        encryptedRequest.ivBase64 = "";
        encryptedRequest.cipherBase64 = "";
        encryptedRequest.tagBase64 = "";
      }
    })();
    const decryptedResponseText = await (async () => {
      try {
        return await localKeyService.decryptByKDevice(targetDeviceName, {
          ivBase64: secureEcho.ivBase64,
          cipherBase64: secureEcho.cipherBase64,
          tagBase64: secureEcho.tagBase64
        });
      } finally {
        secureEcho.ivBase64 = "";
        secureEcho.cipherBase64 = "";
        secureEcho.tagBase64 = "";
      }
    })();
    response.json({
      result: "OK",
      requestId,
      targetDeviceName,
      decryptedResponse: JSON.parse(decryptedResponseText)
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description OTAファームウェアをアップロードするAPI。
 */
app.post(
  "/api/settings/firmware-upload",
  firmwareUploadMiddleware.single("firmware"),
  (request: Request, response: Response) => {
    try {
      if (request.file === undefined) {
        throw new Error("firmware upload failed. file is required.");
      }
      const updatedSettings = settingsStore.setUploadedFirmwareFileName(request.file.filename);
      const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
      const metadata = readOtaFirmwareMetadata(activeFirmwarePath, true);
      response.json({
        result: "OK",
        settings: updatedSettings,
        activeFirmwarePath,
        firmwareInfo: metadata
      });
    } catch (apiError) {
      response.status(400).json({
        result: "NG",
        detail: getErrorMessage(apiError)
      });
    }
  }
);

/**
 * @description status要求コマンド発行API。
 */
app.post("/api/commands/status", async (request: Request, response: Response) => {
  try {
    const requestBody = request.body as commandRequestBody;
    validateCommandBody("status", requestBody);
    await gateway.requestStatus(requestBody.targetNames);
    response.json({
      result: "OK",
      command: "status",
      targetNames: requestBody.targetNames
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      command: "status",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description クラウドモード時に firmware を S3 へアップロードし presigned GET URL を返す。
 * cloudMqttEnabled=true かつ otaS3Bucket 設定済みの場合のみ呼ぶこと。
 */
async function generateCloudOtaFirmwareUrl(cfg: appConfig, firmwarePath: string, firmwareVersion: string): Promise<string> {
  const s3 = new S3Client({ region: "ap-northeast-1" });
  const s3Key = `${cfg.otaS3KeyPrefix}${firmwareVersion}/firmware.bin`;
  const fileBuffer = fs.readFileSync(firmwarePath);
  await s3.send(new PutObjectCommand({
    Bucket: cfg.otaS3Bucket,
    Key: s3Key,
    Body: fileBuffer,
    ContentType: "application/octet-stream"
  }));
  const presignedUrl = await getSignedUrl(
    s3,
    new GetObjectCommand({ Bucket: cfg.otaS3Bucket, Key: s3Key }),
    { expiresIn: cfg.otaPresignTtlSeconds }
  );
  console.log(`generateCloudOtaFirmwareUrl success. s3Key=${s3Key} ttl=${cfg.otaPresignTtlSeconds}s`);
  return presignedUrl;
}

/**
 * @description OTA要求コマンド発行API。
 * [003-0001][厳守] OTA実行には管理者認証必須。同一セッション内3時間有効、ブラウザ終了で失効。
 */
app.post("/api/commands/ota", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as otaCommandRequestBody;
    validateCommandBody("ota", requestBody);
    const currentSettings = settingsStore.getSettings();
    const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
    const otaFirmwareMetadata = readOtaFirmwareMetadata(activeFirmwarePath, true);
    const otaManifestUrl = requestBody.manifestUrl ?? `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/manifest.json`;
    const otaFirmwareVersion = requestBody.firmwareVersion ?? currentSettings.otaFirmwareVersion;
    const otaFirmwareUrl = requestBody.firmwareUrl ?? (
      config.cloudMqttEnabled && config.otaS3Bucket
        ? await generateCloudOtaFirmwareUrl(config, activeFirmwarePath, otaFirmwareVersion)
        : `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`
    );
    const otaSha256 = requestBody.sha256 ?? otaFirmwareMetadata.sha256;
    const timeoutSeconds = requestBody.timeoutSeconds ?? 120;
    await gateway.requestOta(requestBody.targetNames, {
      manifestUrl: otaManifestUrl,
      firmwareUrl: otaFirmwareUrl,
      firmwareVersion: otaFirmwareVersion,
      sha256: otaSha256,
      timeoutSeconds
    });
    response.json({
      result: "OK",
      command: "otaStart",
      targetNames: requestBody.targetNames,
      manifestUrl: otaManifestUrl,
      firmwareUrl: otaFirmwareUrl,
      firmwareVersion: otaFirmwareVersion,
      sha256: otaSha256
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      command: "otaStart",
      detail: errMsg
    });
  }
});

/**
 * @description Rust 側の Pairing workflow を開始するAPI。
 * @remarks
 * - [重要] 直接 `requestedSettings` を渡す正規経路と、既存 `ap/configure` 系入力からの移行経路の両方を受け付ける。
 * - [厳守] `requestedSettings` の必須項目不足時は `SecretCore` を呼び出さない。
 * - [進捗][2026-03-16] 現時点の `SecretCore` は workflow 骨格のみを返すため、本 API の主目的は TS 側入力境界の固定と状態取得経路の先行整備である。
 */
app.post("/api/workflows/pairing/start", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const workflowStatus = await startPairingWorkflowFromRequestBody(
      request.body as Partial<pairingWorkflowStartRequestBody & apConfigureRequestBody & { keyDeviceBase64?: string }>
    );
    response.json({
      result: "OK",
      workflow: workflowStatus
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description Rust 側の KeyRotation workflow を開始するAPI。
 * @remarks
 * - [重要] 入力形は Pairing workflow と同じとし、TS 側は新 `keyVersion` と設定の完全性だけを保証する。
 * - [厳守] workflow 開始後の対ESP32通信や完了判定は Rust 側へ委譲する。
 */
app.post("/api/workflows/key-rotation/start", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const workflowStatus = await startKeyRotationWorkflowFromRequestBody(
      request.body as Partial<keyRotationWorkflowStartRequestBody & apConfigureRequestBody & { keyDeviceBase64?: string }>
    );
    response.json({
      result: "OK",
      workflow: workflowStatus
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description Rust 側の Production workflow を開始するAPI。
 * @remarks
 * - [厳守] TS 側は実行条件と dry-run 意図の検証までに限定し、高リスク手順本体は保持しない。
 * - [重要] 現段階では workflow 境界固定が主目的であり、不可逆処理の本体は後続実装とする。
 */
app.post("/api/workflows/production/start", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    if (!USE_SECRET_CORE) {
      throw new Error("production workflow start failed. SecretCore is disabled.");
    }
    const requestBody = request.body as productionWorkflowStartRequestBody;
    validateProductionWorkflowStartRequestBody("production workflow start", requestBody);
    const workflowStatus = await secretCoreFacade.runProductionSecureFlow(requestBody);
    response.json({
      result: "OK",
      workflow: workflowStatus
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description Rust 側の OTA workflow を開始するAPI。
 * [重要] 初回実装は単一対象機のみを受け付ける。理由: workflow の完了判定と監査単位を明確にするため。
 */
app.post("/api/workflows/signed-ota/start", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    if (!USE_SECRET_CORE) {
      throw new Error("signed-ota workflow start failed. SecretCore is disabled.");
    }
    const requestBody = request.body as otaCommandRequestBody;
    validateCommandBody("signed-ota-start", requestBody);
    const targetDeviceName = resolveSingleTargetDeviceName(requestBody.targetNames, "signed-ota-start");
    const currentSettings = settingsStore.getSettings();
    const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
    const otaFirmwareMetadata = readOtaFirmwareMetadata(activeFirmwarePath, true);
    const otaManifestUrl = requestBody.manifestUrl ?? `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/manifest.json`;
    const otaFirmwareVersion = requestBody.firmwareVersion ?? currentSettings.otaFirmwareVersion;
    const otaFirmwareUrl = requestBody.firmwareUrl ?? (
      config.cloudMqttEnabled && config.otaS3Bucket
        ? await generateCloudOtaFirmwareUrl(config, activeFirmwarePath, otaFirmwareVersion)
        : `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`
    );
    const otaSha256 = requestBody.sha256 ?? otaFirmwareMetadata.sha256;
    const timeoutSeconds = requestBody.timeoutSeconds ?? 120;
    const workflowStatus = await secretCoreFacade.runSignedOtaCommand(targetDeviceName, {
      manifestUrl: otaManifestUrl,
      firmwareUrl: otaFirmwareUrl,
      firmwareVersion: otaFirmwareVersion,
      sha256: otaSha256,
      timeoutSeconds
    });
    response.json({
      result: "OK",
      workflow: workflowStatus
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description Rust 側 workflow 状態を取得するAPI。
 */
app.get("/api/workflows/:workflowId", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    if (!USE_SECRET_CORE) {
      throw new Error("get workflow status failed. SecretCore is disabled.");
    }
    const workflowId = String(request.params.workflowId ?? "").trim();
    if (workflowId.length === 0) {
      throw new Error("get workflow status failed. workflowId is required.");
    }
    const workflowStatus = await secretCoreFacade.getWorkflowStatus(workflowId);
    response.json({
      result: "OK",
      workflow: workflowStatus
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description OTA 不正署名 command を管理者限定で publish する試験API。
 * @remarks
 * - [重要] 実鍵を返さずに「署名不一致時の拒否」を再現するため、サーバー内で正規署名生成後に1文字だけ改ざんする。
 * - [厳守] `signature` 以外の payload は正規値を使い、失敗要因を署名不一致へ限定する。
 * - [禁止] 本APIを通常運用導線へ組み込まない。試験専用とする。
 */
app.post("/api/admin/tests/ota/tampered-signature", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    if (!USE_SECRET_CORE) {
      throw new Error("tampered ota signature test failed. SecretCore is disabled.");
    }
    const requestBody = request.body as otaTamperedSignatureTestRequestBody;
    const targetDeviceName = String(requestBody?.targetDeviceName ?? "").trim();
    if (targetDeviceName.length === 0) {
      throw new Error("tampered ota signature test failed. targetDeviceName is required.");
    }
    const currentSettings = settingsStore.getSettings();
    const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
    const otaFirmwareMetadata = readOtaFirmwareMetadata(activeFirmwarePath, true);
    const otaManifestUrl = requestBody.manifestUrl ?? `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/manifest.json`;
    const otaFirmwareVersion = requestBody.firmwareVersion ?? currentSettings.otaFirmwareVersion;
    const otaFirmwareUrl = requestBody.firmwareUrl ?? (
      config.cloudMqttEnabled && config.otaS3Bucket
        ? await generateCloudOtaFirmwareUrl(config, activeFirmwarePath, otaFirmwareVersion)
        : `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`
    );
    const otaSha256 = requestBody.sha256 ?? otaFirmwareMetadata.sha256;
    const timeoutSeconds = requestBody.timeoutSeconds ?? 120;
    const unsignedPayload = {
      v: 1,
      DstID: targetDeviceName,
      SrcID: config.sourceId,
      id: `tampered-ota-${Date.now()}-${Math.floor(Math.random() * 100000)}`,
      ts: new Date().toISOString(),
      op: "call",
      sub: "otaStart",
      sigAlg: "HMAC-SHA256",
      args: {
        requestType: "otaStart",
        manifestUrl: otaManifestUrl,
        firmwareUrl: otaFirmwareUrl,
        firmwareVersion: otaFirmwareVersion,
        sha256: otaSha256,
        timeoutSeconds
      }
    };
    const normalizedPayloadText = JSON.stringify(unsignedPayload);
    const topic = `esp32lab/call/otaStart/${targetDeviceName}`;
    const tamperedSignatureResult = await (async () => {
      const signatureResult = await localKeyService.signByKDevice(targetDeviceName, normalizedPayloadText);
      try {
        const originalSignatureBase64 = signatureResult.signatureBase64;
        // [重要][修正 2026-05-10] Base64末尾は padding / 未使用bit の影響を受けるため、先頭側の有効文字を改ざんする。
        // 理由: デコード後の HMAC バイト列を確実に変化させ、署名不一致試験を正しく成立させるため。
        const tamperedFirstCharacter = originalSignatureBase64.startsWith("A") ? "B" : "A";
        const tamperedSignatureBase64 = `${tamperedFirstCharacter}${originalSignatureBase64.slice(1)}`;
        const encodedPayloadText = await serverPayloadSecurityService.encodeOutgoingPayload(
          targetDeviceName,
          JSON.stringify({
            ...unsignedPayload,
            signature: tamperedSignatureBase64
          })
        );
        await secretCoreFacade.publishMqttMessage(topic, encodedPayloadText, 1);
        return {
          originalSignaturePreview: `${originalSignatureBase64.slice(0, 8)}...`,
          tamperedSignaturePreview: `${tamperedSignatureBase64.slice(0, 8)}...`
        };
      } finally {
        signatureResult.signatureBase64 = "";
      }
    })();
    response.json({
      result: "OK",
      command: "otaStart",
      mode: "tampered-signature",
      targetDeviceName,
      firmwareVersion: otaFirmwareVersion,
      topic,
      requestId: unsignedPayload.id,
      originalSignaturePreview: tamperedSignatureResult.originalSignaturePreview,
      tamperedSignaturePreview: tamperedSignatureResult.tamperedSignaturePreview
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description `keyDeviceSet` の不正署名 command を管理者限定で publish する試験API。
 * @remarks
 * - [重要] 実鍵を返さずに「署名不一致時の拒否」を再現するため、サーバー内で正規署名生成後に1文字だけ改ざんする。
 * - [厳守] `signature` 以外の payload は正規値を使い、失敗要因を署名不一致へ限定する。
 * - [禁止] 本APIを通常運用導線へ組み込まない。試験専用とする。
 */
app.post("/api/admin/tests/settings/key-device/tampered-signature", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as settingTamperedSignatureTestRequestBody;
    const targetDeviceName = String(requestBody?.targetDeviceName ?? "").trim();
    if (targetDeviceName.length === 0) {
      throw new Error("tampered keyDeviceSet signature test failed. targetDeviceName is required.");
    }
    const tamperedKeyDeviceBase64 = crypto.randomBytes(32).toString("base64");
    const unsignedPayload = {
      v: 1,
      DstID: targetDeviceName,
      SrcID: config.sourceId,
      id: `tampered-key-device-${Date.now()}-${Math.floor(Math.random() * 100000)}`,
      ts: new Date().toISOString(),
      op: "set",
      sub: "keyDeviceSet",
      sigAlg: "HMAC-SHA256",
      args: {
        keyDevice: tamperedKeyDeviceBase64
      }
    };
    const normalizedPayloadText = JSON.stringify(unsignedPayload);
    const topic = `esp32lab/set/keyDeviceSet/${targetDeviceName}`;
    const tamperedSignatureResult = await (async () => {
      const signatureResult = await localKeyService.signByKDevice(targetDeviceName, normalizedPayloadText);
      try {
        const originalSignatureBase64 = signatureResult.signatureBase64;
        // [重要][修正 2026-05-10] Base64末尾は padding / 未使用bit の影響を受けるため、先頭側の有効文字を改ざんする。
        // 理由: デコード後の HMAC バイト列を確実に変化させ、署名不一致試験を正しく成立させるため。
        const tamperedFirstCharacter = originalSignatureBase64.startsWith("A") ? "B" : "A";
        const tamperedSignatureBase64 = `${tamperedFirstCharacter}${originalSignatureBase64.slice(1)}`;
        const encodedPayloadText = await serverPayloadSecurityService.encodeOutgoingPayload(
          targetDeviceName,
          JSON.stringify({
            ...unsignedPayload,
            signature: tamperedSignatureBase64
          })
        );
        if (MQTT_TRANSPORT_MODE === "rust") {
          await secretCoreFacade.publishMqttMessage(topic, encodedPayloadText, 1);
        } else {
          throw new Error("tampered keyDeviceSet signature test failed. rust transport is required for direct tampered publish.");
        }
        return {
          originalSignaturePreview: `${originalSignatureBase64.slice(0, 8)}...`,
          tamperedSignaturePreview: `${tamperedSignatureBase64.slice(0, 8)}...`
        };
      } finally {
        signatureResult.signatureBase64 = "";
      }
    })();
    response.json({
      result: "OK",
      command: "keyDeviceSet",
      mode: "tampered-signature",
      targetDeviceName,
      topic,
      requestId: unsignedPayload.id,
      tamperedKeyDeviceFingerprint: crypto.createHash("sha256").update(Buffer.from(tamperedKeyDeviceBase64, "base64")).digest("hex").slice(0, 16),
      tamperedKeyDevicePreview: `${tamperedKeyDeviceBase64.slice(0, 8)}...`,
      originalSignaturePreview: tamperedSignatureResult.originalSignaturePreview,
      tamperedSignaturePreview: tamperedSignatureResult.tamperedSignaturePreview
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description rollback試験モードを切替えるコマンド発行API。
 */
app.post("/api/commands/rollback-test", async (request: Request, response: Response) => {
  try {
    const requestBody = request.body as rollbackTestCommandRequestBody;
    validateCommandBody("rollback-test", requestBody);
    if (requestBody.mode !== "enable" && requestBody.mode !== "disable") {
      throw new Error(`rollback-test mode is invalid. mode=${String(requestBody.mode)}`);
    }
    const subCommandName = requestBody.mode === "enable" ? "rollbackTestEnable" : "rollbackTestDisable";
    await gateway.requestCall(requestBody.targetNames, subCommandName, {
      requestType: "rollbackTest",
      mode: requestBody.mode
    });
    response.json({
      result: "OK",
      command: subCommandName,
      targetNames: requestBody.targetNames
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      command: "rollback-test",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 汎用setコマンド発行API。
 */
app.post("/api/commands/set", async (request: Request, response: Response) => {
  try {
    const requestBody = request.body as genericCommandRequestBody;
    validateGenericCommandBody("set", requestBody);
    await gateway.requestSet(requestBody.targetNames, requestBody.subCommand, requestBody.args ?? {});
    response.json({
      result: "OK",
      command: "set",
      subCommand: requestBody.subCommand,
      targetNames: requestBody.targetNames
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      command: "set",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description 汎用getコマンド発行API。
 */
app.post("/api/commands/get", async (request: Request, response: Response) => {
  try {
    const requestBody = request.body as genericCommandRequestBody;
    validateGenericCommandBody("get", requestBody);
    await gateway.requestGet(requestBody.targetNames, requestBody.subCommand, requestBody.args ?? {});
    response.json({
      result: "OK",
      command: "get",
      subCommand: requestBody.subCommand,
      targetNames: requestBody.targetNames
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      command: "get",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description networkコマンド発行API。
 */
app.post("/api/commands/network", async (request: Request, response: Response) => {
  try {
    const requestBody = request.body as genericCommandRequestBody;
    validateGenericCommandBody("network", requestBody);
    await gateway.requestNetwork(requestBody.targetNames, requestBody.subCommand, requestBody.args ?? {});
    response.json({
      result: "OK",
      command: "network",
      subCommand: requestBody.subCommand,
      targetNames: requestBody.targetNames
    });
  } catch (apiError) {
    response.status(400).json({
      result: "NG",
      command: "network",
      detail: getErrorMessage(apiError)
    });
  }
});

/**
 * @description SQLite 履歴をフィルタして平文ファイル（JSON Lines）へエクスポートするAPI。
 * @remarks
 * - [厳守] 管理者セッション必須。理由: 運用履歴の持ち出しは監査対象とするため。
 * - [重要] 成功時は `exportHistory` 行と `security-audit.log` へ記録する（DB仕様書 3章）。
 */
app.post("/api/admin/local-history/export", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as localHistoryExportRequestBody;
    const runResult = runLocalHistoryExportToDisk({
      triggerType: "manual",
      requestBody,
      executedBy: runtimeSecurityState.adminUsername
    });
    response.json({
      result: "OK",
      exportPath: runResult.exportPath,
      recordCount: runResult.recordCount,
      filter: runResult.filterForAudit
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

/**
 * @description ローカル履歴 SQLite ファイルを削除し、空のスキーマで再接続する API。
 * @remarks
 * - [厳守] 管理者セッション必須。証跡が失われるため `confirm` 固定トークンを要求する。
 */
app.post("/api/admin/local-history/delete-database", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as localHistoryDeleteDatabaseRequestBody;
    if (requestBody === undefined || requestBody === null || requestBody.confirm !== LOCAL_HISTORY_DELETE_DB_CONFIRM) {
      throw new Error(
        `delete-database failed. confirm must be exactly "${LOCAL_HISTORY_DELETE_DB_CONFIRM}"`
      );
    }
    localHistoryStore.deleteDatabaseFilesAndReopen();
    const resetAt = new Date().toISOString();
    const resetAtJst = formatRecordedAtJstFromIsoUtc(resetAt);
    try {
      localHistoryStore.recordServerEvent({
        eventType: "localHistoryDatabaseRecreated",
        localServerId: config.sourceId,
        detail: JSON.stringify({
          reason: "admin_reset",
          note: "main db and WAL/SHM removed; empty schema recreated"
        }),
        recordedAt: resetAt,
        recordedAtJst: resetAtJst.length > 0 ? resetAtJst : ""
      });
    } catch {
      /* 監査副次録の失敗は API 成功を阻害しない */
    }
    appendSecurityAuditLog("localHistoryDatabaseReset", {
      executedBy: runtimeSecurityState.adminUsername,
      dbPath: config.localHistoryDbPath
    });
    response.json({ result: "OK" });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({
      result: "NG",
      detail: errMsg
    });
  }
});

app.get("/api/admin/broker-mode", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const currentMode = settingsStore.getSettings().brokerMode;
    response.json({ result: "OK", mode: currentMode });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({ result: "NG", detail: errMsg });
  }
});

app.post("/api/admin/broker-mode", async (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as { mode?: string };
    const requestedMode = (requestBody?.mode ?? "").trim();
    if (requestedMode !== "local" && requestedMode !== "cloud") {
      throw new Error(`broker-mode switch failed. mode must be "local" or "cloud". got=${requestedMode}`);
    }
    const currentMode = settingsStore.getSettings().brokerMode;
    if (currentMode === requestedMode) {
      response.json({ result: "OK", mode: requestedMode, changed: false });
      return;
    }
    if (requestedMode === "cloud") {
      if (config.cloudIotEndpoint.length === 0) {
        throw new Error("broker-mode switch failed. AWS_IOT_ENDPOINT is not configured.");
      }
      if (config.cloudIotClientCertPath.length === 0) {
        throw new Error("broker-mode switch failed. AWS_IOT_CLIENT_CERT_PATH is not configured.");
      }
    }
    console.log(`broker-mode switch requested. from=${currentMode} to=${requestedMode}`);
    await gateway.disconnect();
    gateway = buildGateway(requestedMode);
    attachGatewayEvents(gateway);
    settingsStore.updateSettings({ brokerMode: requestedMode });
    gateway.connect();
    console.log(`broker-mode switch complete. mode=${requestedMode}`);
    response.json({ result: "OK", mode: requestedMode, changed: true });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError =
      errMsg.includes("admin token is required") ||
      errMsg.includes("admin token is not found") ||
      errMsg.includes("admin token expired");
    response.status(isAuthError ? 401 : 400).json({ result: "NG", detail: errMsg });
  }
});

app.get("/api/admin/network/firewall-config", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const settings = settingsStore.getSettings();
    const subnet = settings.iotLanSubnet;
    const scriptPath = "scripts\\applyIoTFirewallRules.ps1";
    const applyCommand = `powershell -ExecutionPolicy Bypass -File "${scriptPath}" -IoTSubnet "${subnet}" -IoTNetworkAdapterName "イーサネット 2"`;
    const rollbackCommand = `powershell -ExecutionPolicy Bypass -File "${scriptPath}" -Rollback`;
    response.json({
      result: "OK",
      iotLanSubnet: subnet,
      applyCommand,
      rollbackCommand,
      note: "管理者権限の PowerShell でコマンドを実行してください。LocalServer のインストールルートで実行すること。"
    });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError = errMsg.includes("admin session");
    response.status(isAuthError ? 401 : 500).json({ result: "NG", detail: errMsg });
  }
});

app.post("/api/admin/network/iot-subnet", (request: Request, response: Response) => {
  try {
    requireAdminSession(request);
    const requestBody = request.body as { iotLanSubnet?: string };
    const newSubnet = (requestBody?.iotLanSubnet ?? "").trim();
    if (newSubnet.length === 0) {
      throw new Error("iot-subnet update failed. iotLanSubnet is empty.");
    }
    const updatedSettings = settingsStore.updateSettings({ iotLanSubnet: newSubnet });
    response.json({ result: "OK", iotLanSubnet: updatedSettings.iotLanSubnet });
  } catch (apiError) {
    const errMsg = getErrorMessage(apiError);
    const isAuthError = errMsg.includes("admin session");
    response.status(isAuthError ? 401 : 400).json({ result: "NG", detail: errMsg });
  }
});

/**
 * @description OTA用manifestを返す。
 */
app.get("/ota/manifest.json", (_request: Request, response: Response) => {
  const currentSettings = settingsStore.getSettings();
  const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
  const otaFirmwareMetadata = readOtaFirmwareMetadata(activeFirmwarePath, false);
  response.json({
    version: currentSettings.otaFirmwareVersion,
    fileName: otaFirmwareMetadata.fileName,
    firmwareUrl: `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`,
    sha256: otaFirmwareMetadata.sha256,
    fileSize: otaFirmwareMetadata.fileSize,
    generatedAt: new Date().toISOString(),
    fileExists: otaFirmwareMetadata.fileExists
  });
});

/**
 * @description OTA配布ファイルを返す。
 */
app.get("/ota/firmware.bin", (_request: Request, response: Response) => {
  const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
  if (!fs.existsSync(activeFirmwarePath)) {
    response.status(404).json({
      result: "NG",
      detail: "firmware.bin not found"
    });
    return;
  }
  response.sendFile(activeFirmwarePath);
});

/**
 * @description `package.json` の `version` を読む（SQLite 起動履歴用）。
 * @returns セマンティック版文字列。読取失敗時は `unknown`。
 */
function readLocalServerPackageVersion(): string {
  try {
    const packageJsonPath = path.join(__dirname, "..", "package.json");
    const packageJsonRaw = fs.readFileSync(packageJsonPath, "utf-8");
    const packageJsonParsed = JSON.parse(packageJsonRaw) as { version?: string };
    return typeof packageJsonParsed.version === "string" ? packageJsonParsed.version : "unknown";
  } catch {
    return "unknown";
  }
}

const httpServer = app.listen(config.httpPort, () => {
  console.log(`LocalServer HTTP started. httpPort=${config.httpPort}`);
  try {
    const startupRecordedAt = new Date().toISOString();
    const startupRecordedAtJst = formatRecordedAtJstFromIsoUtc(startupRecordedAt);
    localHistoryStore.recordServerEvent({
      eventType: "localServerStarted",
      localServerId: config.sourceId,
      detail: JSON.stringify({
        packageVersion: readLocalServerPackageVersion(),
        httpPort: config.httpPort,
        mqttTransportMode: MQTT_TRANSPORT_MODE,
        useSecretCore: USE_SECRET_CORE,
        hostName: os.hostname(),
        nodeVersion: process.version,
        processId: process.pid,
        timezoneNote: "recordedAt is UTC ISO8601; recordedAtJst is Asia/Tokyo (JST)"
      }),
      recordedAt: startupRecordedAt,
      recordedAtJst: startupRecordedAtJst.length > 0 ? startupRecordedAtJst : ""
    });
  } catch (startupHistoryError) {
    console.warn(`LocalServer: recordServerEvent(localServerStarted) failed. detail=${getErrorMessage(startupHistoryError)}`);
  }
  try {
    localHistoryStore.purgeExpired();
  } catch (purgeError) {
    const purgeMessage = purgeError instanceof Error ? purgeError.message : String(purgeError);
    console.error(`LocalServer: initial history purge failed. error=${purgeMessage}`);
  }
  setInterval(() => {
    try {
      localHistoryStore.purgeExpired();
    } catch (purgeError) {
      const purgeMessage = purgeError instanceof Error ? purgeError.message : String(purgeError);
      console.error(`LocalServer: scheduled history purge failed. error=${purgeMessage}`);
    }
  }, config.localHistoryPurgeIntervalMs).unref();

  if (config.localHistoryScheduledExportEnabled) {
    try {
      const scheduledExportBody = parseScheduledExportFilterJson(config.localHistoryScheduledExportFilterJson);
      setInterval(() => {
        try {
          const scheduledResult = runLocalHistoryExportToDisk({
            triggerType: "scheduled",
            requestBody: scheduledExportBody,
            executedBy: LOCAL_HISTORY_SCHEDULED_EXECUTOR
          });
          console.info(
            `LocalServer: scheduled history export OK. recordCount=${scheduledResult.recordCount} path=${scheduledResult.exportPath}`
          );
        } catch (exportError) {
          const exportMessage = exportError instanceof Error ? exportError.message : String(exportError);
          console.error(`LocalServer: scheduled history export failed. error=${exportMessage}`);
        }
      }, config.localHistoryScheduledExportIntervalMs).unref();
      console.info(
        `LocalServer: scheduled history export enabled. intervalMs=${config.localHistoryScheduledExportIntervalMs}`
      );
    } catch (filterError) {
      const filterMessage = filterError instanceof Error ? filterError.message : String(filterError);
      console.error(`LocalServer: scheduled history export NOT started (invalid filter JSON). error=${filterMessage}`);
    }
  }
});

const webSocketServer = new WebSocketServer({
  server: httpServer,
  path: config.wsPath
});

webSocketServer.on("connection", (clientSocket) => {
  clientSocket.send(
    JSON.stringify({
      eventName: "devices",
      devices: registry.listDevices()
    })
  );
});

/**
 * @description ブローカーモードに応じた deviceTransport を生成する。
 * @param mode "local" = ローカル Mosquitto、"cloud" = AWS IoT Core。
 */
function buildGateway(mode: "local" | "cloud"): deviceTransport {
  if (mode === "cloud") {
    const cloudGateway = createCloudMqttGateway(config, registry, localKeyService, localHistoryStore);
    if (cloudGateway !== undefined) {
      return cloudGateway;
    }
    console.warn("buildGateway: cloud mode requested but createCloudMqttGateway returned undefined. Falling back to local.");
  }
  return new mqttGateway(config, registry, localKeyService, secretCoreFacade, MQTT_TRANSPORT_MODE, localHistoryStore);
}

/** @description 直前のオンラインデバイス名セット（offline→online遷移検知用）。 */
const previousOnlineDeviceSet = new Set<string>();

/**
 * @description オンラインデバイスへ get/trh を送信する。
 * @param targetNames 送信先。省略時は全オンラインデバイス。
 * @param reason ログ用の送信理由。
 */
async function sendGetTrhToOnlineDevices(targetNames?: string[], reason?: string): Promise<void> {
  const destinationList = targetNames ?? registry.listDevices()
    .filter((deviceItem) => deviceItem.onlineState === "online")
    .map((deviceItem) => deviceItem.deviceName);
  if (destinationList.length === 0) {
    return;
  }
  try {
    await gateway.requestGet(destinationList, "trh", {});
    console.log(`get/trh sent. targets=${destinationList.join(",")} reason=${reason ?? "manual"}`);
  } catch (trhRequestError) {
    console.error(`get/trh send failed. reason=${getErrorMessage(trhRequestError)}`);
  }
}

/**
 * @description ゲートウェイのイベントリスナーを登録する。モード切替後に新インスタンスへ再適用する。
 * @param gw 登録対象ゲートウェイ。
 */
function attachGatewayEvents(gw: deviceTransport): void {
  gw.on("otaProgressUpdated", () => {
    broadcastDeviceList();
  });

  gw.on("trhUpdated", () => {
    broadcastDeviceList();
  });

  gw.on("statusUpdated", (status) => {
    broadcastDeviceList();
    const normalizedDeviceName = status.topic.split("/").at(-1) ?? status.srcId;
    const normalizedOnlineState = status.onlineState.trim().toLowerCase();
    const wasOnline = previousOnlineDeviceSet.has(normalizedDeviceName);
    const isNowOnline = normalizedOnlineState.includes("online");
    if (isNowOnline) {
      previousOnlineDeviceSet.add(normalizedDeviceName);
    } else {
      previousOnlineDeviceSet.delete(normalizedDeviceName);
    }
    if (!wasOnline && isNowOnline) {
      setTimeout(() => {
        sendGetTrhToOnlineDevices([normalizedDeviceName], "offline-to-online");
      }, 3000);
    }
  });

  gw.on("deviceStateUpdated", (deviceState) => {
    broadcastDeviceList();
    if (deviceState.onlineState === "online") {
      previousOnlineDeviceSet.add(deviceState.deviceName);
      return;
    }
    previousOnlineDeviceSet.delete(deviceState.deviceName);
  });

  gw.on("connected", async () => {
    console.log("MQTT connected.");
    if (config.statusRequestOnBoot) {
      setTimeout(async () => {
        try {
          await gateway.requestStatus("all");
          console.log("Startup status request sent.");
        } catch (statusRequestError) {
          console.error(`Startup status request failed. reason=${getErrorMessage(statusRequestError)}`);
        }
      }, config.statusRequestBootDelayMs);
    }
  });

  gw.on("disconnected", () => {
    console.warn("MQTT disconnected.");
  });
}

attachGatewayEvents(gateway);

/**
 * @description SecretCore の IPC 受付準備完了を待機する。
 * @returns 準備完了時は true。
 */
async function waitForSecretCoreReady(): Promise<boolean> {
  if (!USE_SECRET_CORE) {
    return true;
  }
  // KeyManager 初期化と Named Pipe 作成は ack 直後も数秒かかることがあるため、余裕を持たせる。
  const maxAttempts = 80;
  const waitMs = 300;
  for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
    try {
      const isHealthy = await secretCoreFacade.checkHealth(false);
      if (isHealthy) {
        console.log(`SecretCore ready. attempt=${attempt}`);
        return true;
      }
    } catch (healthError) {
      if (attempt === maxAttempts) {
        console.warn(`waitForSecretCoreReady last attempt failed. reason=${getErrorMessage(healthError)}`);
      }
    }
    await new Promise<void>((resolve) => {
      setTimeout(resolve, waitMs);
    });
  }
  console.warn("SecretCore readiness wait timed out. MQTT connect continues with current state.");
  return false;
}

async function bootstrapRuntime(): Promise<void> {
  secretCoreManager.start();
  try {
    await secretCoreManager.waitForBootstrapAck();
  } catch (bootstrapError) {
    const bootstrapMessage = getErrorMessage(bootstrapError);
    console.error(`SecretCore bootstrap failed. detail=${bootstrapMessage}`);
    secretCoreManager.stop();
    process.exit(1);
  }
  await waitForSecretCoreReady();
  gateway.connect();
}

process.on("SIGINT", () => {
  console.info("SIGINT received. Stopping SecretCore...");
  secretCoreManager.stop();
  process.exit(0);
});

process.on("SIGTERM", () => {
  console.info("SIGTERM received. Stopping SecretCore...");
  secretCoreManager.stop();
  process.exit(0);
});

void bootstrapRuntime();

/** [重要] 5分(300秒)ごとに全オンラインデバイスへ get/trh を送信する。 */
const trhPollingIntervalMs = 5 * 60 * 1000;
setInterval(() => {
  sendGetTrhToOnlineDevices(undefined, "periodic-5min");
}, trhPollingIntervalMs);

startOtaHttpsServer();

/**
 * @description WebSocket接続中クライアントへデバイス一覧を配信する。
 */
function broadcastDeviceList(): void {
  const payloadText = JSON.stringify({
    eventName: "devices",
    devices: registry.listDevices()
  });
  for (const clientSocket of webSocketServer.clients) {
    if (clientSocket.readyState === clientSocket.OPEN) {
      clientSocket.send(payloadText);
    }
  }
}

/**
 * @description OTA配布向けHTTPSサーバーを起動する。
 */
function startOtaHttpsServer(): void {
  if (!fs.existsSync(config.otaHttpsCertPath) || !fs.existsSync(config.otaHttpsKeyPath)) {
    console.warn(
      `OTA HTTPS disabled. certOrKeyMissing certPath=${config.otaHttpsCertPath} keyPath=${config.otaHttpsKeyPath}`
    );
    return;
  }

  const otaApp = express();
  // [重要] imagePackageApply で利用する ZIP 配布物を HTTPS で提供する。
  // [厳守] `/assets` は `public/assets` 配下のみ公開し、他ディレクトリを露出しない。
  otaApp.use("/assets", express.static(path.resolve(process.cwd(), "public/assets")));
  otaApp.get("/ota/manifest.json", (_request: Request, response: Response) => {
    const currentSettings = settingsStore.getSettings();
    const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
    const otaFirmwareMetadata = readOtaFirmwareMetadata(activeFirmwarePath, false);
    response.json({
      version: currentSettings.otaFirmwareVersion,
      fileName: otaFirmwareMetadata.fileName,
      firmwareUrl: `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`,
      sha256: otaFirmwareMetadata.sha256,
      fileSize: otaFirmwareMetadata.fileSize,
      generatedAt: new Date().toISOString(),
      fileExists: otaFirmwareMetadata.fileExists
    });
  });
  otaApp.get("/ota/firmware.bin", (_request: Request, response: Response) => {
    const activeFirmwarePath = settingsStore.resolveActiveFirmwarePath();
    if (!fs.existsSync(activeFirmwarePath)) {
      response.status(404).json({
        result: "NG",
        detail: "firmware.bin not found"
      });
      return;
    }
    response.sendFile(activeFirmwarePath);
  });

  const otaServer = https.createServer(
    {
      cert: fs.readFileSync(config.otaHttpsCertPath),
      key: fs.readFileSync(config.otaHttpsKeyPath)
    },
    otaApp
  );

  otaServer.listen(config.otaHttpsPort, () => {
    console.log(`OTA HTTPS started. httpsPort=${config.otaHttpsPort}`);
  });
}

/**
 * @description 履歴エクスポート API 本文を検証し、ストア向けクエリへ変換する。
 * @param requestBody POST 本文。
 * @returns `historyExportQuery`。
 */
function parseHistoryExportQueryFromRequest(requestBody: localHistoryExportRequestBody): historyExportQuery {
  if (requestBody === undefined || requestBody === null) {
    throw new Error("parseHistoryExportQueryFromRequest failed. requestBody is null");
  }
  const combineCandidate = requestBody.fieldCombine ?? "AND";
  if (combineCandidate !== "AND" && combineCandidate !== "OR") {
    throw new Error(
      `parseHistoryExportQueryFromRequest failed. fieldCombine must be AND or OR. actual=${String(combineCandidate)}`
    );
  }

  let sources: historyExportQuery["sources"];
  const sourcesRaw = requestBody.sources;
  if (sourcesRaw === undefined || sourcesRaw === null || (Array.isArray(sourcesRaw) && sourcesRaw.length === 0)) {
    sources = ["deviceStatus", "command", "serverEvent"];
  } else {
    if (!Array.isArray(sourcesRaw)) {
      throw new Error("parseHistoryExportQueryFromRequest failed. sources must be an array");
    }
    const nextSources: Array<"deviceStatus" | "command" | "serverEvent"> = [];
    for (const entry of sourcesRaw) {
      if (entry !== "deviceStatus" && entry !== "command" && entry !== "serverEvent") {
        throw new Error(`parseHistoryExportQueryFromRequest failed. invalid sources entry=${String(entry)}`);
      }
      if (!nextSources.includes(entry)) {
        nextSources.push(entry);
      }
    }
    sources = nextSources.length > 0 ? nextSources : ["deviceStatus", "command", "serverEvent"];
  }

  const fromCandidate = requestBody.fromAt?.trim() ?? "";
  if (fromCandidate.length > 0 && Number.isNaN(Date.parse(fromCandidate))) {
    throw new Error(`parseHistoryExportQueryFromRequest failed. fromAt is not valid ISO8601. value=${fromCandidate}`);
  }
  const toCandidate = requestBody.toAt?.trim() ?? "";
  if (toCandidate.length > 0 && Number.isNaN(Date.parse(toCandidate))) {
    throw new Error(`parseHistoryExportQueryFromRequest failed. toAt is not valid ISO8601. value=${toCandidate}`);
  }

  const cmdCandidate = requestBody.command?.trim() ?? "";
  const subCandidate = requestBody.subCommand?.trim() ?? "";
  const devCandidate = requestBody.deviceNo?.trim() ?? "";

  return {
    sources,
    fieldCombine: combineCandidate,
    commandName: cmdCandidate.length > 0 ? cmdCandidate : undefined,
    subCommand: subCandidate.length > 0 ? subCandidate : undefined,
    deviceNo: devCandidate.length > 0 ? devCandidate : undefined,
    fromAt: fromCandidate.length > 0 ? fromCandidate : undefined,
    toAt: toCandidate.length > 0 ? toCandidate : undefined
  };
}

/**
 * @description エクスポートファイルのベース名をファイルシステム安全な形へ正規化する。
 * @param raw 任意入力。
 * @returns 正規化後ベース名（空なら既定）。
 */
function sanitizeHistoryExportFileBaseName(raw: string | undefined): string {
  const fallbackName = "history-export";
  if (raw === undefined || raw.trim().length === 0) {
    return fallbackName;
  }
  const cleaned = raw.trim().replace(/[^a-zA-Z0-9._-]/g, "_").slice(0, 80);
  return cleaned.length > 0 ? cleaned : fallbackName;
}

/**
 * @description `LOCAL_HISTORY_SCHEDULED_EXPORT_FILTER_JSON` を検証し API 本文形へ変換する。
 * @param filterJsonRaw 環境変数文字列（空ならフィルタなし）。
 * @returns `localHistoryExportRequestBody`。
 */
function parseScheduledExportFilterJson(filterJsonRaw: string): localHistoryExportRequestBody {
  const trimmed = filterJsonRaw.trim();
  if (trimmed.length === 0) {
    return {};
  }
  let parsedRoot: unknown;
  try {
    parsedRoot = JSON.parse(trimmed);
  } catch (parseError) {
    throw new Error(`parseScheduledExportFilterJson failed. JSON.parse error. reason=${getErrorMessage(parseError)}`);
  }
  if (parsedRoot === null || typeof parsedRoot !== "object" || Array.isArray(parsedRoot)) {
    throw new Error("parseScheduledExportFilterJson failed. root must be a non-array object");
  }
  const body = parsedRoot as localHistoryExportRequestBody;
  parseHistoryExportQueryFromRequest(body);
  return body;
}

/**
 * @description SQLite 履歴をフィルタして JSON Lines ファイルへ書き出し、`exportHistory` と監査ログへ記録する。
 * @param options 実行モード・本文・実行者。
 * @returns 出力パス・件数・監査用フィルタ。
 */
function runLocalHistoryExportToDisk(options: {
  triggerType: "manual" | "scheduled";
  requestBody: localHistoryExportRequestBody;
  executedBy: string;
}): { exportPath: string; recordCount: number; filterForAudit: Record<string, unknown>; filterJson: string } {
  const exportQuery = parseHistoryExportQueryFromRequest(options.requestBody);
  const fileBaseNameRaw = options.requestBody.fileBaseName;
  const filterForAudit: Record<string, unknown> = {
    triggerType: options.triggerType,
    sources: exportQuery.sources,
    fieldCombine: exportQuery.fieldCombine,
    command: exportQuery.commandName ?? null,
    subCommand: exportQuery.subCommand ?? null,
    deviceNo: exportQuery.deviceNo ?? null,
    fromAt: exportQuery.fromAt ?? null,
    toAt: exportQuery.toAt ?? null,
    fileBaseName: fileBaseNameRaw ?? null
  };
  const filterJson = JSON.stringify(filterForAudit);

  const deviceRows = exportQuery.sources.includes("deviceStatus")
    ? localHistoryStore.queryDeviceStatusForExport(exportQuery)
    : [];
  const commandRows = exportQuery.sources.includes("command")
    ? localHistoryStore.queryCommandForExport(exportQuery)
    : [];
  const serverEventRows = exportQuery.sources.includes("serverEvent")
    ? localHistoryStore.queryServerEventForExport(exportQuery)
    : [];
  const recordCount = deviceRows.length + commandRows.length + serverEventRows.length;

  if (!fs.existsSync(config.localHistoryExportDir)) {
    fs.mkdirSync(config.localHistoryExportDir, { recursive: true });
  }
  const timestampText = new Date().toISOString().replace(/[:.]/g, "-");
  const defaultBase =
    options.triggerType === "scheduled" ? "scheduled-history-export" : sanitizeHistoryExportFileBaseName(undefined);
  const safeBase =
    fileBaseNameRaw !== undefined && fileBaseNameRaw.trim().length > 0
      ? sanitizeHistoryExportFileBaseName(fileBaseNameRaw)
      : defaultBase;
  const fileName = `${timestampText}_${safeBase}.jsonl`;
  const exportPath = path.join(config.localHistoryExportDir, fileName);

  const metaLine = {
    kind: "meta",
    exportedAt: new Date().toISOString(),
    recordCount,
    filter: filterForAudit,
    sources: exportQuery.sources,
    triggerType: options.triggerType,
    timeReferenceNote: "Each record includes recordedAt (UTC ISO8601) and recordedAtJst (Asia/Tokyo, suffix JST) where applicable."
  };
  const lineTexts: string[] = [JSON.stringify(metaLine)];
  for (const row of deviceRows) {
    lineTexts.push(JSON.stringify({ kind: "deviceStatus", record: row }));
  }
  for (const row of commandRows) {
    lineTexts.push(JSON.stringify({ kind: "command", record: row }));
  }
  for (const row of serverEventRows) {
    lineTexts.push(JSON.stringify({ kind: "serverEvent", record: row }));
  }
  fs.writeFileSync(exportPath, `${lineTexts.join("\n")}\n`, "utf-8");
  try {
    fs.chmodSync(exportPath, 0o600);
  } catch (chmodError) {
    console.warn(`local-history export chmod skipped. path=${exportPath} detail=${getErrorMessage(chmodError)}`);
  }

  const executedAt = new Date().toISOString();
  localHistoryStore.recordExportHistory({
    triggerType: options.triggerType,
    filterJson,
    exportPath,
    recordCount,
    executedBy: options.executedBy,
    executedAt
  });
  appendSecurityAuditLog("localHistoryExported", {
    exportPath,
    recordCount,
    executedBy: options.executedBy,
    triggerType: options.triggerType,
    filter: filterForAudit
  });

  return { exportPath, recordCount, filterForAudit, filterJson };
}

/**
 * @description API入力の基本検証を行う。
 * @param commandName コマンド名。
 * @param requestBody API本文。
 */
function validateCommandBody(commandName: string, requestBody: commandRequestBody | otaCommandRequestBody): void {
  if (requestBody === undefined || requestBody === null) {
    throw new Error(`validateCommandBody failed. commandName=${commandName} requestBody is null`);
  }
  if (requestBody.targetNames === undefined || requestBody.targetNames === null) {
    throw new Error(`validateCommandBody failed. commandName=${commandName} targetNames is required`);
  }
  if (requestBody.targetNames !== "all" && (!Array.isArray(requestBody.targetNames) || requestBody.targetNames.length === 0)) {
    throw new Error(`validateCommandBody failed. commandName=${commandName} targetNames must be non-empty array or "all"`);
  }
}

/**
 * @description 単一対象機のみを許可するAPI入力を検証し、対象デバイス名を返す。
 * @param targetNames API入力の targetNames。
 * @param commandName 検証対象コマンド名。
 * @returns 単一対象デバイス名。
 */
function resolveSingleTargetDeviceName(targetNames: string[] | "all", commandName: string): string {
  if (targetNames === "all") {
    throw new Error(`resolveSingleTargetDeviceName failed. commandName=${commandName} targetNames="all" is not allowed.`);
  }
  const normalizedTargetList = targetNames.map((targetName) => targetName.trim()).filter((targetName) => targetName.length > 0);
  if (normalizedTargetList.length !== 1) {
    throw new Error(
      `resolveSingleTargetDeviceName failed. commandName=${commandName} single target is required. actualCount=${normalizedTargetList.length}`
    );
  }
  return normalizedTargetList[0];
}

/**
 * @description set/get/network向けの入力検証を行う。
 * @param commandName コマンド名。
 * @param requestBody API本文。
 */
function validateGenericCommandBody(commandName: string, requestBody: genericCommandRequestBody): void {
  validateCommandBody(commandName, requestBody);
  if (requestBody.subCommand === undefined || requestBody.subCommand === null || requestBody.subCommand.trim().length === 0) {
    throw new Error(`validateGenericCommandBody failed. commandName=${commandName} subCommand is required`);
  }
}

/**
 * @description Pairing workflow 開始要求を正規化する。
 * @remarks
 * - [重要] `requestedSettings` を直接受ける正規経路を優先する。
 * - [進捗][2026-03-16] 既存 `ap/configure` 系入力からの移行を容易にするため、同形式からの変換経路も許容する。
 * - [重要] `keyDeviceBase64` が省略された場合は、既知機に限り LocalServer 保持情報から補完して workflow 入力を完成させる。
 * @param rawRequestBody 受信した API 本文。
 * @returns 正規化済み Pairing workflow 開始要求。
 */
async function resolvePairingWorkflowStartRequestBody(
  rawRequestBody: Partial<pairingWorkflowStartRequestBody & apConfigureRequestBody & { keyDeviceBase64?: string }>
): Promise<pairingWorkflowStartRequestBody> {
  if (rawRequestBody === undefined || rawRequestBody === null) {
    throw new Error("resolvePairingWorkflowStartRequestBody failed. rawRequestBody is null.");
  }

  const normalizedRequestBody: pairingWorkflowStartRequestBody = {
    targetDeviceId: String(rawRequestBody.targetDeviceId ?? "").trim(),
    sessionId: String(rawRequestBody.sessionId ?? "").trim(),
    keyVersion: String(rawRequestBody.keyVersion ?? "").trim(),
    requestedSettings: (rawRequestBody.requestedSettings ?? null) as pairingRequestedSettings
  };

  if (rawRequestBody.requestedSettings !== undefined && rawRequestBody.requestedSettings !== null) {
    validatePairingWorkflowStartRequestBody("resolvePairingWorkflowStartRequestBody", normalizedRequestBody);
    return normalizedRequestBody;
  }

  const targetDeviceName = resolveTargetDeviceNameFromWorkflowTargetId(normalizedRequestBody.targetDeviceId);
  let keyDeviceBase64 = String(rawRequestBody.keyDeviceBase64 ?? "").trim();
  if (keyDeviceBase64.length === 0) {
    keyDeviceBase64 = await localKeyService.getKDeviceBase64(targetDeviceName);
  }

  return buildPairingWorkflowStartRequestBodyFromApConfigure(
    {
      targetDeviceId: normalizedRequestBody.targetDeviceId,
      sessionId: normalizedRequestBody.sessionId,
      keyVersion: normalizedRequestBody.keyVersion
    },
    rawRequestBody as apConfigureRequestBody,
    keyDeviceBase64
  );
}

/**
 * @description workflow 入力の `targetDeviceId` から LocalServer 内部の対象デバイス名を解決する。
 * @remarks
 * - [重要] 既知機は `deviceName` / `publicId` / `srcId` / `dstId` / `macAddr` の順で照合する。
 * - [推奨] 未解決時は入力値をそのまま返し、`SecretCore` 側の正式実装で最終照合する。
 * - [理由] 既存 `k-device` 取得経路は `targetDeviceName` 前提であり、IF 仕様の `targetDeviceId` と一時的に橋渡しする必要があるため。
 * @param targetDeviceId workflow 入力の対象識別子。
 * @returns k-device 解決に用いる対象デバイス名。
 */
function resolveTargetDeviceNameFromWorkflowTargetId(targetDeviceId: string): string {
  const normalizedTargetDeviceId = String(targetDeviceId ?? "").trim();
  if (normalizedTargetDeviceId.length === 0) {
    throw new Error("resolveTargetDeviceNameFromWorkflowTargetId failed. targetDeviceId is empty.");
  }

  const matchedDevice = registry.listDevices().find((deviceStateItem) => {
    return [
      deviceStateItem.deviceName,
      deviceStateItem.publicId,
      deviceStateItem.srcId,
      deviceStateItem.dstId,
      deviceStateItem.macAddr
    ].some((candidateValue) => candidateValue.trim() === normalizedTargetDeviceId);
  });

  return matchedDevice?.deviceName ?? normalizedTargetDeviceId;
}

/**
 * @description Production workflow 開始要求の最小妥当性を検証する。
 * @param functionName 呼び出し元関数名。
 * @param requestBody Production workflow 開始要求。
 */
function validateProductionWorkflowStartRequestBody(
  functionName: string,
  requestBody: productionWorkflowStartRequestBody
): void {
  if (requestBody === undefined || requestBody === null) {
    throw new Error(`validateProductionWorkflowStartRequestBody failed. functionName=${functionName} requestBody is null.`);
  }
  if (String(requestBody.targetDeviceId ?? "").trim().length === 0) {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} targetDeviceId is required.`
    );
  }
  if (String(requestBody.runId ?? "").trim().length === 0) {
    throw new Error(`validateProductionWorkflowStartRequestBody failed. functionName=${functionName} runId is required.`);
  }
  if (requestBody.productionSettings === undefined || requestBody.productionSettings === null) {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} productionSettings is required.`
    );
  }
  if (typeof requestBody.productionSettings.dryRun !== "boolean") {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} productionSettings.dryRun must be boolean.`
    );
  }
  if (
    requestBody.productionSettings.allowIrreversibleExecution !== undefined &&
    typeof requestBody.productionSettings.allowIrreversibleExecution !== "boolean"
  ) {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} productionSettings.allowIrreversibleExecution must be boolean when provided.`
    );
  }
  if (
    requestBody.productionSettings.stepPlan !== undefined &&
    (!Array.isArray(requestBody.productionSettings.stepPlan) ||
      requestBody.productionSettings.stepPlan.some((stepName) => typeof stepName !== "string" || stepName.trim().length === 0))
  ) {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} productionSettings.stepPlan must be string array.`
    );
  }
  if (
    requestBody.productionSettings.expectedMac !== undefined &&
    requestBody.productionSettings.expectedMac.trim().length > 0 &&
    !/^[0-9a-fA-F:-]{12,17}$/.test(requestBody.productionSettings.expectedMac.trim())
  ) {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} productionSettings.expectedMac format is invalid.`
    );
  }
  if (
    requestBody.productionSettings.expectedFirmwareVersion !== undefined &&
    requestBody.productionSettings.expectedFirmwareVersion.trim().length === 0
  ) {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} productionSettings.expectedFirmwareVersion must not be empty when provided.`
    );
  }
  if (
    requestBody.productionSettings.minimumFreeHeapBytes !== undefined &&
    (typeof requestBody.productionSettings.minimumFreeHeapBytes !== "number" ||
      Number.isNaN(requestBody.productionSettings.minimumFreeHeapBytes) ||
      requestBody.productionSettings.minimumFreeHeapBytes < 0)
  ) {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} productionSettings.minimumFreeHeapBytes must be non-negative number.`
    );
  }
  if (
    requestBody.productionSettings.minimumStackMarginBytes !== undefined &&
    (typeof requestBody.productionSettings.minimumStackMarginBytes !== "number" ||
      Number.isNaN(requestBody.productionSettings.minimumStackMarginBytes) ||
      requestBody.productionSettings.minimumStackMarginBytes < 0)
  ) {
    throw new Error(
      `validateProductionWorkflowStartRequestBody failed. functionName=${functionName} productionSettings.minimumStackMarginBytes must be non-negative number.`
    );
  }
  validateOptionalProductionApCredentials(functionName, requestBody);
  if (
    requestBody.productionSettings.precheckSnapshot !== undefined &&
    requestBody.productionSettings.precheckSnapshot !== null
  ) {
    validateProductionWorkflowPrecheckSnapshot(functionName, requestBody.productionSettings.precheckSnapshot);
  }
}

/**
 * @description Production workflow の AP 接続情報の整合性を検証する。
 * @param functionName 呼び出し元関数名。
 * @param requestBody Production workflow 開始要求。
 */
function validateOptionalProductionApCredentials(
  functionName: string,
  requestBody: productionWorkflowStartRequestBody
): void {
  const apBaseUrl = String(requestBody.productionSettings.apBaseUrl ?? "").trim();
  const apUsername = String(requestBody.productionSettings.apUsername ?? "").trim();
  const apPassword = String(requestBody.productionSettings.apPassword ?? "");
  const providedFieldCount = [apBaseUrl, apUsername, apPassword].filter((fieldValue) => fieldValue.length > 0).length;
  if (providedFieldCount !== 0 && providedFieldCount !== 3) {
    throw new Error(
      `validateOptionalProductionApCredentials failed. functionName=${functionName} apBaseUrl/apUsername/apPassword must be provided together.`
    );
  }
  if (apBaseUrl.length > 0) {
    try {
      const parsedUrl = new URL(apBaseUrl);
      if (parsedUrl.protocol !== "http:" && parsedUrl.protocol !== "https:") {
        throw new Error("invalid protocol");
      }
    } catch {
      throw new Error(
        `validateOptionalProductionApCredentials failed. functionName=${functionName} productionSettings.apBaseUrl must be valid http/https URL.`
      );
    }
  }
}

/**
 * @description Production workflow の事前チェック観測値を検証する。
 * @param functionName 呼び出し元関数名。
 * @param precheckSnapshot 事前チェック観測値。
 */
function validateProductionWorkflowPrecheckSnapshot(
  functionName: string,
  precheckSnapshot: productionWorkflowPrecheckSnapshot
): void {
  const booleanFieldNameList: Array<keyof productionWorkflowPrecheckSnapshot> = [
    "targetDeviceMatched",
    "powerStable",
    "firmwareVersionApproved",
    "keyIdVerified",
    "unsecuredStateConfirmed",
    "operatorAuthenticated",
    "stackMarginOk",
    "heapMarginOk"
  ];
  for (const fieldName of booleanFieldNameList) {
    const fieldValue = precheckSnapshot[fieldName];
    if (fieldValue !== undefined && typeof fieldValue !== "boolean") {
      throw new Error(
        `validateProductionWorkflowPrecheckSnapshot failed. functionName=${functionName} field=${fieldName} must be boolean when provided.`
      );
    }
  }
  const numberFieldNameList: Array<keyof productionWorkflowPrecheckSnapshot> = [
    "measuredFreeHeapBytes",
    "measuredMinStackMarginBytes"
  ];
  for (const fieldName of numberFieldNameList) {
    const fieldValue = precheckSnapshot[fieldName];
    if (fieldValue !== undefined && (typeof fieldValue !== "number" || Number.isNaN(fieldValue))) {
      throw new Error(
        `validateProductionWorkflowPrecheckSnapshot failed. functionName=${functionName} field=${fieldName} must be number when provided.`
      );
    }
  }
}

/**
 * @description 管理者セッショントークン文字列をHTTPヘッダから抽出する。
 * @param request Expressリクエスト。
 * @returns 抽出トークン。未設定時は空文字。
 */
function extractAdminToken(request: Request): string {
  const bearerValue = request.header("authorization");
  if (bearerValue !== undefined && bearerValue.toLowerCase().startsWith("bearer ")) {
    return bearerValue.slice("bearer ".length).trim();
  }
  const customHeaderToken = request.header("x-admin-token");
  return customHeaderToken === undefined ? "" : customHeaderToken.trim();
}

/**
 * @description 管理者セッションの有効性を検証する。
 * @param request Expressリクエスト。
 */
function requireAdminSession(request: Request): void {
  const token = extractAdminToken(request);
  if (token.length === 0) {
    throw new Error("requireAdminSession failed. admin token is required.");
  }
  const expireAtEpochMs = adminSessionMap.get(token);
  if (expireAtEpochMs === undefined) {
    throw new Error("requireAdminSession failed. admin token is not found.");
  }
  if (Date.now() > expireAtEpochMs) {
    adminSessionMap.delete(token);
    throw new Error("requireAdminSession failed. admin token expired.");
  }
}

/**
 * @description OSのネットワークインタフェースからWi-Fi USB候補を抽出する。
 * @returns 候補一覧。
 */
async function listWifiUsbInterfaceCandidates(): Promise<Array<{
  interfaceName: string;
  isLikelyWifi: boolean;
  isLikelyUsb: boolean;
  addressList: string[];
}>> {
  const interfacesObject = os.networkInterfaces();
  const osInterfaceNameList = Object.keys(interfacesObject);
  const netshInterfaceNameList = await listWifiInterfaceNamesByNetsh();
  const mergedInterfaceNameSet = new Set<string>([...osInterfaceNameList, ...netshInterfaceNameList]);
  const mergedInterfaceNameList = Array.from(mergedInterfaceNameSet);
  return mergedInterfaceNameList.map((interfaceName) => {
    const addressInfoList = interfacesObject[interfaceName] ?? [];
    const addressList = addressInfoList
      .filter((info) => info.internal === false)
      .map((info) => `${info.family}:${info.address}`);
    const loweredName = interfaceName.toLowerCase();
    const isLikelyWifi = loweredName.includes("wi-fi") || loweredName.includes("wifi") || loweredName.includes("wlan") || loweredName.includes("wireless");
    const isLikelyUsb = loweredName.includes("usb");
    return {
      interfaceName,
      isLikelyWifi,
      isLikelyUsb,
      addressList
    };
  });
}

/**
 * @description netshからWi-Fiインタフェース名一覧を取得する。
 * @returns インタフェース名一覧。
 */
async function listWifiInterfaceNamesByNetsh(): Promise<string[]> {
  if (process.platform !== "win32") {
    return [];
  }
  const outputText = await runNetshCommand("netsh wlan show interfaces");
  const lines = outputText.split(/\r?\n/);
  const resultNameList: string[] = [];
  for (const lineText of lines) {
    const nameMatch = lineText.match(/^\s*(?:Name|名前)\s*:\s*(.+)$/i);
    if (nameMatch === null) {
      continue;
    }
    const interfaceName = (nameMatch[1] ?? "").trim();
    if (interfaceName.length > 0) {
      resultNameList.push(interfaceName);
    }
  }
  return resultNameList;
}

/**
 * @description netshコマンドを実行する。
 * @param commandText 実行コマンド。
 * @returns 標準出力。
 */
async function runNetshCommand(commandText: string): Promise<string> {
  if (process.platform !== "win32") {
    throw new Error(`runNetshCommand failed. platform must be win32. actual=${process.platform}`);
  }
  const { stdout, stderr } = await execAsync(commandText, {
    windowsHide: true
  });
  if (stderr.trim().length > 0 && !stderr.includes("プロファイル")) {
    // netshは正常時でもstderrへ文言を出す場合があるため、致命的でないものは許容する。
    console.warn(`runNetshCommand warning. command=${commandText} stderr=${stderr.trim()}`);
  }
  return stdout;
}

/**
 * @description AP候補一覧をスキャンする。
 * @param interfaceName Wi-Fi USBインタフェース名。
 * @returns AP候補一覧。
 */
async function scanMaintenanceAccessPoints(interfaceName: string): Promise<Array<{ ssid: string; signal: string }>> {
  const escapedInterfaceName = interfaceName.replace(/"/g, "");
  const outputText = await runNetshCommand(`netsh wlan show networks mode=bssid interface="${escapedInterfaceName}"`);
  const lines = outputText.split(/\r?\n/);
  const resultList: Array<{ ssid: string; signal: string }> = [];
  let currentSsid = "";
  let currentSignal = "";
  for (const lineText of lines) {
    const ssidMatch = lineText.match(/^\s*SSID\s+\d+\s*:\s*(.*)$/i);
    if (ssidMatch !== null) {
      if (currentSsid.startsWith(config.apSsidPrefix)) {
        resultList.push({ ssid: currentSsid, signal: currentSignal });
      }
      currentSsid = (ssidMatch[1] ?? "").trim();
      currentSignal = "";
      continue;
    }
    const signalMatch = lineText.match(/^\s*Signal\s*:\s*(.*)$/i);
    if (signalMatch !== null) {
      currentSignal = (signalMatch[1] ?? "").trim();
    }
  }
  if (currentSsid.startsWith(config.apSsidPrefix)) {
    resultList.push({ ssid: currentSsid, signal: currentSignal });
  }
  return resultList;
}

/**
 * @description AP接続用のWLANプロファイルXMLを作成する。
 * @param ssid AP名。
 * @returns XML文字列。
 */
function buildWlanProfileXml(ssid: string): string {
  const escapedSsid = ssid.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  const escapedPassword = config.apWifiPassword.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  return `<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>${escapedSsid}</name>
  <SSIDConfig>
    <SSID>
      <name>${escapedSsid}</name>
    </SSID>
  </SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM>
    <security>
      <authEncryption>
        <authentication>WPA2PSK</authentication>
        <encryption>AES</encryption>
        <useOneX>false</useOneX>
      </authEncryption>
      <sharedKey>
        <keyType>passPhrase</keyType>
        <protected>false</protected>
        <keyMaterial>${escapedPassword}</keyMaterial>
      </sharedKey>
    </security>
  </MSM>
</WLANProfile>`;
}

/**
 * @description 指定APへ接続する。
 * @param interfaceName Wi-Fi USBインタフェース名。
 * @param ssid AP名。
 */
async function connectToMaintenanceAccessPoint(interfaceName: string, ssid: string): Promise<void> {
  const escapedInterfaceName = interfaceName.replace(/"/g, "");
  const escapedSsid = ssid.replace(/"/g, "");
  const alreadyConnectedSsid = await getConnectedSsid(escapedInterfaceName);
  if (alreadyConnectedSsid === escapedSsid) {
    return;
  }
  const profileDirectoryPath = path.resolve(process.cwd(), "data", "wifi-profiles");
  if (!fs.existsSync(profileDirectoryPath)) {
    fs.mkdirSync(profileDirectoryPath, { recursive: true });
  }
  const safeFileName = escapedSsid.replace(/[^a-zA-Z0-9._-]/g, "_");
  const profileFilePath = path.join(profileDirectoryPath, `${safeFileName}.xml`);
  fs.writeFileSync(profileFilePath, buildWlanProfileXml(escapedSsid), "utf-8");
  await runNetshCommand(`netsh wlan add profile filename="${profileFilePath}" interface="${escapedInterfaceName}"`);
  await runNetshCommand(`netsh wlan connect name="${escapedSsid}" ssid="${escapedSsid}" interface="${escapedInterfaceName}"`);
  const deadlineTime = Date.now() + 20000;
  while (Date.now() < deadlineTime) {
    const connectedSsid = await getConnectedSsid(escapedInterfaceName);
    if (connectedSsid === escapedSsid) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
  const finalConnectedSsid = await getConnectedSsid(escapedInterfaceName);
  throw new Error(
    `connectToMaintenanceAccessPoint failed. interface=${escapedInterfaceName} targetSsid=${escapedSsid} currentSsid=${finalConnectedSsid || "(empty)"} did not connect within timeout.`
  );
}

/**
 * @description 指定インタフェースの現在接続SSIDを返す。
 * @param interfaceName インタフェース名。
 * @returns SSID（未接続時は空）。
 */
async function getConnectedSsid(interfaceName: string): Promise<string> {
  const outputText = await runNetshCommand(`netsh wlan show interfaces interface="${interfaceName}"`);
  const lines = outputText.split(/\r?\n/);
  let inTargetInterfaceBlock = false;
  for (const lineText of lines) {
    const nameMatch = lineText.match(/^\s*(Name|名前)\s*:\s*(.*)$/i);
    if (nameMatch !== null) {
      const parsedInterfaceName = (nameMatch[2] ?? "").trim();
      inTargetInterfaceBlock = parsedInterfaceName === interfaceName;
      continue;
    }
    if (!inTargetInterfaceBlock) {
      continue;
    }
    const ssidMatch = lineText.match(/^\s*SSID\s*:\s*(.*)$/i);
    if (ssidMatch !== null) {
      return (ssidMatch[1] ?? "").trim();
    }
  }
  return "";
}

/**
 * @description APの管理者ログインを実行する。
 * @returns トークン。
 */
async function loginToMaintenanceAp(): Promise<{ token: string }> {
  const response = await fetchWithTimeout(`${config.apHttpBaseUrl}/api/auth/login`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      username: runtimeSecurityState.apRoleAdminUsername,
      password: runtimeSecurityState.apRoleAdminPassword
    })
  });
  const responseText = await response.text();
  const responseJson = parseJsonTextAsObject(responseText);
  if (!response.ok) {
    throw new Error(`loginToMaintenanceAp failed. status=${response.status} detail=${String(responseJson.detail ?? "")}`);
  }
  const token = String(responseJson.token ?? "");
  if (token.length === 0) {
    throw new Error("loginToMaintenanceAp failed. token is empty.");
  }
  return { token };
}

/**
 * @description LocalServerの認証状態（管理者/AP管理者）を読み込む。
 * @returns 認証状態。
 */
function loadSecurityState(): securityState {
  const defaultState: securityState = {
    adminUsername: config.adminUsername,
    adminPassword: config.adminPassword,
    apRoleAdminUsername: config.apRoleAdminUsername,
    apRoleAdminPassword: config.apRoleAdminPassword
  };
  const stateDirectoryPath = path.dirname(securityStateFilePath);
  if (!fs.existsSync(stateDirectoryPath)) {
    fs.mkdirSync(stateDirectoryPath, { recursive: true });
  }
  if (!fs.existsSync(securityStateFilePath)) {
    fs.writeFileSync(securityStateFilePath, `${JSON.stringify(defaultState, null, 2)}\n`, "utf-8");
    return defaultState;
  }
  try {
    const rawText = fs.readFileSync(securityStateFilePath, "utf-8");
    const parsedJson = JSON.parse(rawText) as Partial<securityState>;
    const loadedState: securityState = {
      adminUsername: String(parsedJson.adminUsername ?? defaultState.adminUsername).trim(),
      adminPassword: String(parsedJson.adminPassword ?? defaultState.adminPassword),
      apRoleAdminUsername: String(parsedJson.apRoleAdminUsername ?? defaultState.apRoleAdminUsername).trim(),
      apRoleAdminPassword: String(parsedJson.apRoleAdminPassword ?? defaultState.apRoleAdminPassword)
    };
    if (loadedState.adminUsername.length === 0 || loadedState.adminPassword.length === 0) {
      throw new Error("loadSecurityState failed. admin username/password is empty.");
    }
    if (loadedState.apRoleAdminUsername.length === 0 || loadedState.apRoleAdminPassword.length === 0) {
      throw new Error("loadSecurityState failed. ap admin username/password is empty.");
    }
    return loadedState;
  } catch (stateError) {
    throw new Error(`loadSecurityState failed. path=${securityStateFilePath} reason=${getErrorMessage(stateError)}`);
  }
}

/**
 * @description LocalServerの認証状態（管理者/AP管理者）を保存する。
 * @param nextState 保存対象。
 */
function saveSecurityState(nextState: securityState): void {
  const normalizedState: securityState = {
    adminUsername: nextState.adminUsername.trim(),
    adminPassword: nextState.adminPassword,
    apRoleAdminUsername: nextState.apRoleAdminUsername.trim(),
    apRoleAdminPassword: nextState.apRoleAdminPassword
  };
  if (normalizedState.adminUsername.length === 0 || normalizedState.adminPassword.length === 0) {
    throw new Error("saveSecurityState failed. admin username/password is empty.");
  }
  if (normalizedState.apRoleAdminUsername.length === 0 || normalizedState.apRoleAdminPassword.length === 0) {
    throw new Error("saveSecurityState failed. ap admin username/password is empty.");
  }
  fs.writeFileSync(securityStateFilePath, `${JSON.stringify(normalizedState, null, 2)}\n`, "utf-8");
}

/**
 * @description セキュリティ運用の監査ログを追記し、OS ログへも同内容を二重書き込みする。
 * @remarks
 * - [重要] 監査ログはファイル保存と OS ログ出力を同時に行う。
 * - [厳守] 監査ファイルは 0600 相当へ寄せる。
 * - [推奨] OS ログ出力は失敗してもファイル保存を優先する。
 * @param eventType 監査イベント種別。
 * @param detailJson 監査詳細。
 */
function appendSecurityAuditLog(eventType: string, detailJson: Record<string, unknown>): void {
  ensureSecurityAuditSinkReady();
  const logRecord = {
    loggedAt: new Date().toISOString(),
    eventType,
    detail: detailJson
  };
  fs.appendFileSync(securityAuditFilePath, `${JSON.stringify(logRecord)}\n`, "utf-8");
  writeSecurityAuditOsLog(logRecord);
}

/**
 * @description 監査ログの保存先ディレクトリとファイル権限を整える。
 * @remarks
 * - [厳守] 監査ログディレクトリは作成時に 0700 相当、ファイルは 0600 相当へ寄せる。
 * - [禁止] 権限調整に失敗しても監査記録自体を止めない。理由: 記録欠落の方が危険なため。
 */
function ensureSecurityAuditSinkReady(): void {
  if (!fs.existsSync(securityAuditDirectoryPath)) {
    fs.mkdirSync(securityAuditDirectoryPath, { recursive: true, mode: 0o700 });
  }
  try {
    fs.chmodSync(securityAuditDirectoryPath, 0o700);
  } catch (chmodError) {
    console.warn(`ensureSecurityAuditSinkReady warning. directory chmod failed. detail=${getErrorMessage(chmodError)}`);
  }
  if (!fs.existsSync(securityAuditFilePath)) {
    fs.closeSync(fs.openSync(securityAuditFilePath, "a", 0o600));
  }
  try {
    fs.chmodSync(securityAuditFilePath, 0o600);
  } catch (chmodError) {
    console.warn(`ensureSecurityAuditSinkReady warning. file chmod failed. detail=${getErrorMessage(chmodError)}`);
  }
}

/**
 * @description 監査ログを OS 側へ短い要約として書き込む。
 * @remarks
 * - [重要] Windows はイベントログ、macOS は syslog 相当の logger を優先する。
 * - [厳守] OS ログ失敗は例外化せず、ファイル監査を優先する。
 * @param logRecord 監査レコード。
 */
function writeSecurityAuditOsLog(logRecord: { loggedAt: string; eventType: string; detail: Record<string, unknown> }): void {
  const detailText = JSON.stringify(logRecord.detail);
  const summaryText = truncateSecurityAuditOsMessage(
    `[LocalServer][security-audit] loggedAt=${logRecord.loggedAt} eventType=${logRecord.eventType} detail=${detailText}`
  );
  try {
    if (process.platform === "win32") {
      const result = spawnSync(
        "eventcreate",
        ["/T", "INFORMATION", "/ID", "1000", "/L", "APPLICATION", "/SO", "LocalServer", "/D", summaryText],
        { windowsHide: true, stdio: "ignore" }
      );
      if (result.status !== 0) {
        console.warn(
          `writeSecurityAuditOsLog warning. eventcreate failed. status=${result.status} signal=${result.signal ?? ""}`
        );
      }
      return;
    }
    const result = spawnSync("logger", ["-t", "LocalServer", summaryText], { windowsHide: true, stdio: "ignore" });
    if (result.status !== 0) {
      console.warn(`writeSecurityAuditOsLog warning. logger failed. status=${result.status} signal=${result.signal ?? ""}`);
    }
  } catch (osLogError) {
    console.warn(`writeSecurityAuditOsLog warning. detail=${getErrorMessage(osLogError)}`);
  }
}

/**
 * @description OS ログ向けメッセージを安全な長さへ切り詰める。
 * @param message 元メッセージ。
 * @returns 切り詰め後メッセージ。
 */
function truncateSecurityAuditOsMessage(message: string): string {
  const maxLength = 900;
  if (message.length <= maxLength) {
    return message;
  }
  return `${message.slice(0, maxLength - 20)}...(truncated)`;
}

/**
 * @description 管理者ログイン時の送信元IPを取得する。
 * @param request Express の request。
 * @returns 監査・ロック判定に使う送信元識別子。
 */
function resolveAdminLoginRemoteAddress(request: Request): string {
  const forwardedForHeader = request.headers["x-forwarded-for"];
  if (typeof forwardedForHeader === "string" && forwardedForHeader.trim().length > 0) {
    return forwardedForHeader.split(",")[0].trim();
  }
  if (Array.isArray(forwardedForHeader) && forwardedForHeader.length > 0) {
    return String(forwardedForHeader[0] ?? "").trim();
  }
  return request.ip || request.socket.remoteAddress || "unknown";
}

/**
 * @description 管理者ログイン失敗回数を管理するキーを生成する。
 * @param username 入力されたユーザー名。
 * @param remoteAddress 送信元IP。
 * @returns ユーザー名と送信元IPの複合キー。
 */
function resolveAdminLoginLockKey(username: string, remoteAddress: string): string {
  return `${username.trim().toLowerCase()}@@${remoteAddress.trim()}`;
}

/**
 * @description 現在有効な管理者ログインロック状態を取得する。
 * @param adminLoginLockKey ロック管理キー。
 * @returns 有効なロック情報。ロックされていない場合は null。
 */
function getActiveAdminLoginLockState(
  adminLoginLockKey: string
): { consecutiveFailureCount: number; retryAfterSeconds: number; lockedUntilIso: string } | null {
  const storedState = adminLoginLockStateMap.get(adminLoginLockKey);
  if (storedState === undefined) {
    return null;
  }
  if (storedState.lockedUntilEpochMs <= 0) {
    return null;
  }
  const currentEpochMs = Date.now();
  if (storedState.lockedUntilEpochMs <= currentEpochMs) {
    adminLoginLockStateMap.delete(adminLoginLockKey);
    return null;
  }
  return {
    consecutiveFailureCount: storedState.consecutiveFailureCount,
    retryAfterSeconds: Math.max(1, Math.ceil((storedState.lockedUntilEpochMs - currentEpochMs) / 1000)),
    lockedUntilIso: new Date(storedState.lockedUntilEpochMs).toISOString()
  };
}

/**
 * @description 管理者ログイン失敗を記録し、必要ならロック状態へ移行する。
 * @param adminLoginLockKey ロック管理キー。
 * @param username 入力されたユーザー名。
 * @param remoteAddress 送信元IP。
 * @returns 失敗回数とロック状態の要約。
 */
function registerAdminLoginFailure(
  adminLoginLockKey: string,
  username: string,
  remoteAddress: string
): {
  consecutiveFailureCount: number;
  isLocked: boolean;
  remainingAttempts: number;
  retryAfterSeconds: number;
  lockedUntilIso: string | null;
} {
  const previousState = adminLoginLockStateMap.get(adminLoginLockKey);
  const nextFailureCount = (previousState?.consecutiveFailureCount ?? 0) + 1;
  const thresholdReached = nextFailureCount >= adminLoginLockoutThreshold;
  const nextLockedUntilEpochMs = thresholdReached ? Date.now() + adminLoginLockoutDurationMs : 0;
  adminLoginLockStateMap.set(adminLoginLockKey, {
    username,
    remoteAddress,
    consecutiveFailureCount: nextFailureCount,
    lockedUntilEpochMs: nextLockedUntilEpochMs
  });
  return {
    consecutiveFailureCount: nextFailureCount,
    isLocked: thresholdReached,
    remainingAttempts: Math.max(0, adminLoginLockoutThreshold - nextFailureCount),
    retryAfterSeconds: thresholdReached ? Math.ceil(adminLoginLockoutDurationMs / 1000) : 0,
    lockedUntilIso: thresholdReached ? new Date(nextLockedUntilEpochMs).toISOString() : null
  };
}

/**
 * @description 管理者ログイン成功後に失敗回数とロック状態を解除する。
 * @param adminLoginLockKey ロック管理キー。
 */
function clearAdminLoginLockState(adminLoginLockKey: string): void {
  adminLoginLockStateMap.delete(adminLoginLockKey);
}

/**
 * @description AP側APIへJSON POSTする。
 * @param apiPath APIパス。
 * @param bodyJson 本文JSON。
 * @param token 認証トークン。
 */
async function postMaintenanceApJson(apiPath: string, bodyJson: Record<string, unknown>, token: string): Promise<void> {
  const response = await fetchWithTimeout(`${config.apHttpBaseUrl}${apiPath}`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${token}`
    },
    body: JSON.stringify(bodyJson)
  });
  if (!response.ok) {
    const responseText = await response.text();
    throw new Error(`postMaintenanceApJson failed. path=${apiPath} status=${response.status} body=${responseText}`);
  }
}

/**
 * @description AP側APIへJSON GETする。
 * @param apiPath APIパス。
 * @param token 認証トークン。
 * @returns 応答JSON。
 */
async function getMaintenanceApJson(apiPath: string, token: string): Promise<Record<string, unknown>> {
  const response = await fetchWithTimeout(`${config.apHttpBaseUrl}${apiPath}`, {
    method: "GET",
    headers: {
      Authorization: `Bearer ${token}`
    }
  });
  const responseText = await response.text();
  const responseJson = parseJsonTextAsObject(responseText);
  if (!response.ok) {
    throw new Error(`getMaintenanceApJson failed. path=${apiPath} status=${response.status} body=${responseText}`);
  }
  return responseJson;
}

/**
 * @description AP更新後の最終成功判定として status 応答を待機する。
 * @param targetDeviceName 対象デバイス名。空の場合は online の最新端末を採用。
 * @param timeoutSeconds 待機秒数。
 * @returns 判定結果。
 */
async function waitForStatusRecoveryByDevice(
  targetDeviceName: string,
  timeoutSeconds: number
): Promise<{ publicId: string; firmwareVersion: string; configVersion: string }> {
  if (MQTT_TRANSPORT_MODE === "rust") {
    const statusRecoveryResult = await secretCoreFacade.waitForStatusRecovery(targetDeviceName, timeoutSeconds);
    return {
      publicId: statusRecoveryResult.publicId,
      firmwareVersion: statusRecoveryResult.firmwareVersion,
      configVersion: statusRecoveryResult.configVersion
    };
  }
  const deadlineMs = Date.now() + (Math.max(1, timeoutSeconds) * 1000);
  while (Date.now() <= deadlineMs) {
    const deviceList = registry.listDevices();
    let foundDevice = targetDeviceName.length > 0
      ? deviceList.find((device) => device.deviceName === targetDeviceName)
      : deviceList.find((device) => device.onlineState === "online");
    if (foundDevice !== undefined && foundDevice.onlineState === "online") {
      return {
        publicId: foundDevice.publicId,
        firmwareVersion: foundDevice.firmwareVersion,
        configVersion: foundDevice.configVersion
      };
    }
    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
  throw new Error(
    `waitForStatusRecoveryByDevice failed. timeout seconds=${timeoutSeconds} targetDeviceName=${targetDeviceName || "(empty)"}`
  );
}

/**
 * @description タイムアウト付きfetchを実行する。
 * @param url URL文字列。
 * @param init 初期化オプション。
 * @returns fetchレスポンス。
 */
async function fetchWithTimeout(url: string, init: globalThis.RequestInit): Promise<globalThis.Response> {
  const abortController = new AbortController();
  const timeoutHandle = setTimeout(() => abortController.abort(), apHttpRequestTimeoutMs);
  try {
    return await fetch(url, {
      ...init,
      signal: abortController.signal
    });
  } catch (errorValue) {
    if (errorValue instanceof Error && errorValue.name === "AbortError") {
      throw new Error(`fetchWithTimeout failed. request timeout ${apHttpRequestTimeoutMs}ms. url=${url}`);
    }
    throw errorValue;
  } finally {
    clearTimeout(timeoutHandle);
  }
}

/**
 * @description JSON文字列を安全にオブジェクト化する。
 * @param jsonText JSON文字列。
 * @returns オブジェクト。
 */
function parseJsonTextAsObject(jsonText: string): Record<string, unknown> {
  try {
    const parsedValue = JSON.parse(jsonText) as unknown;
    if (typeof parsedValue === "object" && parsedValue !== null) {
      return parsedValue as Record<string, unknown>;
    }
  } catch {
    // plain textの場合は後段で空オブジェクトとして扱う。
  }
  return {};
}

/**
 * @description URLまたはホスト表記をホスト名へ正規化する。
 * @param rawHostOrUrl 生入力。
 * @returns 正規化後ホスト名。
 */
function normalizeHostInput(rawHostOrUrl: string): string {
  const inputText = rawHostOrUrl.trim();
  if (inputText.length === 0) {
    return "";
  }
  if (inputText.includes("://")) {
    try {
      const parsedUrl = new URL(inputText);
      return parsedUrl.hostname.trim();
    } catch {
      return inputText;
    }
  }
  const slashIndex = inputText.indexOf("/");
  if (slashIndex >= 0) {
    return inputText.slice(0, slashIndex).trim();
  }
  return inputText;
}

/**
 * @description TCP到達性を診断する。
 * @param host ホスト。
 * @param port ポート。
 * @param timeoutMs タイムアウトms。
 * @returns 診断結果。
 */
async function testTcpConnectivity(host: string, port: number, timeoutMs: number): Promise<Record<string, unknown>> {
  const startedAt = Date.now();
  return await new Promise((resolve) => {
    const socket = new net.Socket();
    let finished = false;
    const done = (ok: boolean, errorDetail: string) => {
      if (finished) {
        return;
      }
      finished = true;
      socket.destroy();
      resolve({
        host,
        port,
        protocol: "tcp",
        ok,
        durationMs: Date.now() - startedAt,
        errorDetail
      });
    };
    socket.setTimeout(timeoutMs);
    socket.once("connect", () => done(true, ""));
    socket.once("timeout", () => done(false, `timeout ${timeoutMs}ms`));
    socket.once("error", (socketError) => done(false, getErrorMessage(socketError)));
    socket.connect(port, host);
  });
}

/**
 * @description NTP(UDP)の応答有無を診断する。
 * @param host ホスト。
 * @param port ポート。
 * @param timeoutMs タイムアウトms。
 * @returns 診断結果。
 */
async function testNtpUdpConnectivity(host: string, port: number, timeoutMs: number): Promise<Record<string, unknown>> {
  const startedAt = Date.now();
  return await new Promise((resolve) => {
    const udpSocket = dgram.createSocket("udp4");
    let finished = false;
    const done = (ok: boolean, errorDetail: string, responseBytes: number) => {
      if (finished) {
        return;
      }
      finished = true;
      udpSocket.close();
      resolve({
        host,
        port,
        protocol: "udp-ntp",
        ok,
        durationMs: Date.now() - startedAt,
        responseBytes,
        errorDetail
      });
    };
    const timeoutHandle = setTimeout(() => {
      done(false, `timeout ${timeoutMs}ms`, 0);
    }, timeoutMs);
    udpSocket.once("error", (udpError) => {
      clearTimeout(timeoutHandle);
      done(false, getErrorMessage(udpError), 0);
    });
    udpSocket.once("message", (messageBuffer) => {
      clearTimeout(timeoutHandle);
      done(true, "", messageBuffer.length);
    });
    const ntpPacket = Buffer.alloc(48);
    ntpPacket[0] = 0x1b;
    udpSocket.send(ntpPacket, port, host, (sendError) => {
      if (sendError !== null) {
        clearTimeout(timeoutHandle);
        done(false, getErrorMessage(sendError), 0);
      }
    });
  });
}

/**
 * @description 不明型エラーを文字列化する。
 * @param errorValue 例外値。
 * @returns エラーメッセージ。
 */
function getErrorMessage(errorValue: unknown): string {
  return errorValue instanceof Error ? errorValue.message : String(errorValue);
}

/**
 * @description OTA配布ファームウェアのメタ情報を取得する。
 * @param throwIfMissing trueならファイル未存在時に例外を投げる。
 * @returns メタ情報。
 */
function readOtaFirmwareMetadata(firmwarePath: string, throwIfMissing: boolean): {
  fileExists: boolean;
  fileName: string;
  fileSize: number;
  sha256: string;
} {
  const fileExists = fs.existsSync(firmwarePath);
  const fileName = path.basename(firmwarePath);
  if (!fileExists) {
    if (throwIfMissing) {
      throw new Error(`readOtaFirmwareMetadata failed. firmwarePath=${firmwarePath} file not found`);
    }
    return {
      fileExists: false,
      fileName,
      fileSize: 0,
      sha256: ""
    };
  }

  const firmwareBuffer = fs.readFileSync(firmwarePath);
  return {
    fileExists: true,
    fileName,
    fileSize: firmwareBuffer.byteLength,
    sha256: crypto.createHash("sha256").update(firmwareBuffer).digest("hex")
  };
}
