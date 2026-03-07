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
import express, { Request, Response } from "express";
import multer from "multer";
import { WebSocketServer } from "ws";
import { loadConfig } from "./config";
import { DeviceRegistry } from "./deviceRegistry";
import { mqttGateway } from "./mqttGateway";
import { SettingsStore } from "./settingsStore";
import { commandRequestBody, otaCommandRequestBody, localServerSettings } from "./types";

const config = loadConfig();
const app = express();
const registry = new DeviceRegistry(config.statusOfflineTimeoutSeconds);
const gateway = new mqttGateway(config, registry);
const settingsStore = new SettingsStore(config);
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
 */
app.get("/api/devices", (_request: Request, response: Response) => {
  response.json({
    devices: registry.listDevices()
  });
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
 */
app.post("/api/commands/ota", async (request: Request, response: Response) => {
  try {
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
    response.status(400).json({
      result: "NG",
      command: "otaStart",
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

gateway.on("statusUpdated", () => {
  broadcastDeviceList();
});

gateway.on("otaProgressUpdated", () => {
  broadcastDeviceList();
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

gateway.connect();

setInterval(() => {
  registry.updateOfflineByTimeout();
  broadcastDeviceList();
}, 5000);

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
