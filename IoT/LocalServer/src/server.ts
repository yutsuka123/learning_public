/**
 * @file server.ts
 * @description LocalServer本体。Web UI・REST API・MQTT連携・OTA配布エンドポイントを提供する。
 * @remarks
 * - [重要] 起動時にstatus要求を送信し、ESP32オンライン状態を初期同期する。
 * - [厳守] OTA配布はHTTPSを優先し、証明書未配置時は警告を出して無効化する。
 * - [禁止] OTA配布URLへ機密情報（認証値）を埋め込まない。
 */

import fs from "fs";
import path from "path";
import https from "https";
import crypto from "crypto";
import os from "os";
import net from "net";
import dgram from "dgram";
import { exec } from "child_process";
import { promisify } from "util";
import express, { Request, Response } from "express";
import multer from "multer";
import { WebSocketServer } from "ws";
import { loadConfig } from "./config";
import { DeviceRegistry } from "./deviceRegistry";
import { deviceTransport } from "./deviceTransport";
import { mqttGateway } from "./mqttGateway";
import { SettingsStore } from "./settingsStore";
import { keyService } from "./keyService";
import { commandRequestBody, otaCommandRequestBody, rollbackTestCommandRequestBody, localServerSettings, genericCommandRequestBody } from "./types";
import { SecretCoreManager } from "./secretCoreManager";
import { SecretCoreIpcClient } from "./secretCoreIpcClient";
import { SecretCoreFacade } from "./secretCoreFacade";

const config = loadConfig();
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
const gateway: deviceTransport = new mqttGateway(
  config,
  registry,
  localKeyService,
  secretCoreFacade,
  MQTT_TRANSPORT_MODE
);
const adminSessionMap = new Map<string, number>();
const adminSessionTtlMs = 3 * 60 * 60 * 1000;
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

app.use(express.json({ limit: "1mb" }));
app.use(express.static(path.resolve(process.cwd(), "public")));

interface adminLoginRequestBody {
  username: string;
  password: string;
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

interface apConfigureRequestBody {
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
const apBatchRunMap = new Map<string, apBatchRunResult>();

/**
 * @description [003-0011][厳守] eFuse 操作 API 拒否。
 * eFuse / Secure Boot / Flash Encryption の書込みは Production 専用。LocalServer では提供しない。
 * 理由: IF仕様書「eFuse 操作 API は LocalServer 側へ実装しない」を担保する。
 */
app.all(/^\/api\/(admin\/efuse|commands\/efuse)/, (_request: Request, response: Response) => {
  response.status(404).json({
    result: "NG",
    detail: "eFuse operations are Production-only. Not implemented in LocalServer."
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
    if (username !== runtimeSecurityState.adminUsername || password !== runtimeSecurityState.adminPassword) {
      response.status(401).json({
        result: "NG",
        detail: "authentication failed"
      });
      return;
    }
    const nextToken = crypto.randomUUID();
    adminSessionMap.set(nextToken, Date.now() + adminSessionTtlMs);
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
    const encryptedRequest = await localKeyService.encryptByKDevice(targetDeviceName, plainPayloadText);
    const secureEcho = await gateway.requestSecurePing(
      targetDeviceName,
      requestId,
      {
        ivBase64: encryptedRequest.ivBase64,
        cipherBase64: encryptedRequest.cipherBase64,
        tagBase64: encryptedRequest.tagBase64
      },
      requestBody?.timeoutMs ?? 15000
    );
    const decryptedResponseText = await localKeyService.decryptByKDevice(targetDeviceName, {
      ivBase64: secureEcho.ivBase64,
      cipherBase64: secureEcho.cipherBase64,
      tagBase64: secureEcho.tagBase64
    });
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
    const otaFirmwareUrl = requestBody.firmwareUrl ?? `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`;
    const otaFirmwareVersion = requestBody.firmwareVersion ?? currentSettings.otaFirmwareVersion;
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
    const otaFirmwareUrl = requestBody.firmwareUrl ?? `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`;
    const otaFirmwareVersion = requestBody.firmwareVersion ?? currentSettings.otaFirmwareVersion;
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

const httpServer = app.listen(config.httpPort, () => {
  console.log(`LocalServer HTTP started. httpPort=${config.httpPort}`);
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

gateway.on("otaProgressUpdated", () => {
  broadcastDeviceList();
});

gateway.on("trhUpdated", () => {
  broadcastDeviceList();
});

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

/** @description 直前のオンラインデバイス名セット（offline→online遷移検知用）。 */
const previousOnlineDeviceSet = new Set<string>();

gateway.on("statusUpdated", (status) => {
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

gateway.on("deviceStateUpdated", (deviceState) => {
  broadcastDeviceList();
  if (deviceState.onlineState === "online") {
    previousOnlineDeviceSet.add(deviceState.deviceName);
    return;
  }
  previousOnlineDeviceSet.delete(deviceState.deviceName);
});

gateway.on("connected", async () => {
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

gateway.on("disconnected", () => {
  console.warn("MQTT disconnected.");
});

/**
 * @description SecretCore の IPC 受付準備完了を待機する。
 * @returns 準備完了時は true。
 */
async function waitForSecretCoreReady(): Promise<boolean> {
  if (!USE_SECRET_CORE) {
    return true;
  }
  const maxAttempts = 20;
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
 * @description セキュリティ運用の監査ログを追記する。
 * @param eventType 監査イベント種別。
 * @param detailJson 監査詳細。
 */
function appendSecurityAuditLog(eventType: string, detailJson: Record<string, unknown>): void {
  if (!fs.existsSync(securityAuditDirectoryPath)) {
    fs.mkdirSync(securityAuditDirectoryPath, { recursive: true });
  }
  const logRecord = {
    loggedAt: new Date().toISOString(),
    eventType,
    detail: detailJson
  };
  fs.appendFileSync(securityAuditFilePath, `${JSON.stringify(logRecord)}\n`, "utf-8");
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
