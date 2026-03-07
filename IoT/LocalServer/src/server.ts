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
import express, { Request, Response } from "express";
import { WebSocketServer } from "ws";
import { loadConfig } from "./config";
import { DeviceRegistry } from "./deviceRegistry";
import { mqttGateway } from "./mqttGateway";
import { commandRequestBody, otaCommandRequestBody } from "./types";

const config = loadConfig();
const app = express();
const registry = new DeviceRegistry(config.statusOfflineTimeoutSeconds);
const gateway = new mqttGateway(config, registry);

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
    const otaManifestUrl = requestBody.manifestUrl ?? `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/manifest.json`;
    const otaFirmwareUrl = requestBody.firmwareUrl ?? `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`;
    const otaFirmwareVersion = requestBody.firmwareVersion ?? config.otaFirmwareVersion;
    const timeoutSeconds = requestBody.timeoutSeconds ?? 120;
    await gateway.requestOta(requestBody.targetNames, {
      manifestUrl: otaManifestUrl,
      firmwareUrl: otaFirmwareUrl,
      firmwareVersion: otaFirmwareVersion,
      timeoutSeconds
    });
    response.json({
      result: "OK",
      command: "otaStart",
      targetNames: requestBody.targetNames,
      manifestUrl: otaManifestUrl,
      firmwareUrl: otaFirmwareUrl,
      firmwareVersion: otaFirmwareVersion
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
  const firmwareExists = fs.existsSync(config.otaFirmwarePath);
  const firmwareFileName = path.basename(config.otaFirmwarePath);
  response.json({
    version: config.otaFirmwareVersion,
    fileName: firmwareFileName,
    firmwareUrl: `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`,
    generatedAt: new Date().toISOString(),
    fileExists: firmwareExists
  });
});

/**
 * @description OTA配布ファイルを返す。
 */
app.get("/ota/firmware.bin", (_request: Request, response: Response) => {
  if (!fs.existsSync(config.otaFirmwarePath)) {
    response.status(404).json({
      result: "NG",
      detail: "firmware.bin not found"
    });
    return;
  }
  response.sendFile(config.otaFirmwarePath);
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
    response.json({
      version: config.otaFirmwareVersion,
      firmwareUrl: `https://${config.otaPublicHost}:${config.otaHttpsPort}/ota/firmware.bin`,
      generatedAt: new Date().toISOString()
    });
  });
  otaApp.get("/ota/firmware.bin", (_request: Request, response: Response) => {
    if (!fs.existsSync(config.otaFirmwarePath)) {
      response.status(404).json({
        result: "NG",
        detail: "firmware.bin not found"
      });
      return;
    }
    response.sendFile(config.otaFirmwarePath);
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
