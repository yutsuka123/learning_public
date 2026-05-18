/**
 * @file test7102LiveMqttHistory.mjs
 * @description 試験仕様書 **7102** の **MQTT 実機フロー**（`status` 受信 → `deviceStatusHistory` 追記）を API 経由で検証する。
 *
 * @remarks
 * - [重要] **ブローカ接続済み ESP32** が前提。`POST /api/commands/status` で応答させ、管理者エクスポート API で件数が増えることを確認する。
 * - [厳守] 事前に `npm run build`。管理者パスワードは `dist/config.js` の `loadConfig()`（`.env`）由来。
 * - [制限] `--targetDeviceName` 省略時は `GET /api/devices` のうち **`onlineState` が online 系**の先頭を使う。
 * - [重要][2026-05-17] **`--spawnLocalServer`** を付けると `dist/server.js` を子プロセスで起動し、検証後に **`taskkill`** する（3100 未使用時向け）。既に LocalServer を起動している場合は **フラグなし**で `--baseUrl` のみ指定する。
 *
 * 実行例:
 *   cd IoT/LocalServer && npm run test:7102:live
 *   node scripts/test7102LiveMqttHistory.mjs --baseUrl http://127.0.0.1:3100 --targetDeviceName IoT_XXXXX
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { spawn } from "child_process";
import { loginAsAdmin } from "./testCommon.mjs";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");
const distServerPath = path.join(projectRootPath, "dist", "server.js");

/**
 * @param {string[]} argv
 * @returns {{ baseUrl: string; targetDeviceName: string; maxWaitMs: number; spawnLocalServer: boolean }}
 */
function parseArgs(argv) {
  const filtered = argv.filter((argItem) => argItem !== "--spawnLocalServer");
  const spawnLocalServer = argv.includes("--spawnLocalServer");
  const baseIdx = filtered.indexOf("--baseUrl");
  const baseUrl =
    baseIdx >= 0 && filtered[baseIdx + 1] ? String(filtered[baseIdx + 1]).trim() : "http://127.0.0.1:3100";
  const nameIdx = filtered.indexOf("--targetDeviceName");
  const targetDeviceName = nameIdx >= 0 && filtered[nameIdx + 1] ? String(filtered[nameIdx + 1]).trim() : "";
  const waitIdx = filtered.indexOf("--maxWaitMs");
  const rawWait = waitIdx >= 0 && filtered[waitIdx + 1] ? Number.parseInt(String(filtered[waitIdx + 1]), 10) : Number.NaN;
  const maxWaitMs = Number.isFinite(rawWait) && rawWait >= 10000 ? rawWait : 90000;
  return { baseUrl, targetDeviceName, maxWaitMs, spawnLocalServer };
}

/**
 * @param {number} ms
 * @returns {Promise<void>}
 */
function sleep(ms) {
  return new Promise((resolvePromise) => setTimeout(resolvePromise, ms));
}

/**
 * @param {number} pid
 */
function killProcessTreeWindows(pid) {
  try {
    const killer = spawn("taskkill", ["/PID", String(pid), "/T", "/F"], {
      stdio: "ignore",
      windowsHide: true
    });
    killer.on("error", () => {});
  } catch {
    /* 続行 */
  }
}

/**
 * @param {string} baseUrl
 * @param {number} maxWaitMs
 * @returns {Promise<void>}
 */
async function waitForLocalServerHealth(baseUrl, maxWaitMs) {
  const startMs = Date.now();
  while (Date.now() - startMs < maxWaitMs) {
    try {
      const healthResponse = await fetch(`${baseUrl}/api/health`);
      if (healthResponse.ok) {
        console.info(`[INFO] LocalServer health OK. waitedMs=${Date.now() - startMs} baseUrl=${baseUrl}`);
        return;
      }
    } catch {
      /* まだ起動中 */
    }
    await sleep(1000);
  }
  throw new Error(`waitForLocalServerHealth timeout. baseUrl=${baseUrl} maxWaitMs=${maxWaitMs}`);
}

/**
 * @param {Response} response
 * @param {string} contextLabel
 * @returns {Promise<any>}
 */
async function readJsonResponseBodyOrThrow(response, contextLabel) {
  const responseText = await response.text();
  const trimmed = responseText.trimStart();
  if (trimmed.startsWith("<!DOCTYPE") || trimmed.startsWith("<html")) {
    throw new Error(
      `${contextLabel}: expected JSON but received HTML. status=${response.status} preview=${responseText.slice(0, 120)}`
    );
  }
  try {
    return JSON.parse(responseText);
  } catch (parseError) {
    const reason = parseError instanceof Error ? parseError.message : String(parseError);
    throw new Error(`${contextLabel}: JSON.parse failed. status=${response.status} reason=${reason}`);
  }
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} deviceName
 * @returns {Promise<{ recordCount: number; exportPath: string }>}
 */
async function exportDeviceStatusCount(baseUrl, token, deviceName) {
  const exportBody = {
    sources: ["deviceStatus"],
    fieldCombine: "AND",
    deviceNo: deviceName,
    fileBaseName: `live7102-probe-${Date.now()}`
  };
  const authedResponse = await fetch(`${baseUrl}/api/admin/local-history/export`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${token}`
    },
    body: JSON.stringify(exportBody)
  });
  const authedJson = await readJsonResponseBodyOrThrow(authedResponse, "POST /api/admin/local-history/export");
  if (!authedResponse.ok || authedJson.result !== "OK") {
    throw new Error(
      `exportDeviceStatusCount failed. status=${authedResponse.status} body=${JSON.stringify(authedJson).slice(0, 400)}`
    );
  }
  return {
    recordCount: Number(authedJson.recordCount ?? 0),
    exportPath: String(authedJson.exportPath ?? "")
  };
}

/**
 * @param {string} baseUrl
 * @returns {Promise<string>}
 */
async function pickOnlineDeviceName(baseUrl) {
  const response = await fetch(`${baseUrl}/api/devices`);
  if (!response.ok) {
    throw new Error(`pickOnlineDeviceName failed. GET /api/devices status=${response.status}`);
  }
  const body = await readJsonResponseBodyOrThrow(response, "GET /api/devices");
  const devices = Array.isArray(body.devices) ? body.devices : [];
  for (const deviceItem of devices) {
    const name = String(deviceItem?.deviceName ?? "").trim();
    const onlineRaw = String(deviceItem?.onlineState ?? "").trim().toLowerCase();
    if (name.length === 0) {
      continue;
    }
    if (onlineRaw.includes("online")) {
      return name;
    }
  }
  throw new Error(
    `pickOnlineDeviceName failed. no online device in /api/devices. deviceCount=${devices.length} hint="--targetDeviceName でデバイス名を指定するか、ESP32 の MQTT online を確認"`
  );
}

/**
 * @param {string} baseUrl
 * @param {string} nameFromArg 空でなければそのまま返す。
 * @param {number} maxWaitMs 実機の MQTT 登場待ち。
 * @returns {Promise<string>}
 */
async function resolveDeviceNameWithWait(baseUrl, nameFromArg, maxWaitMs) {
  if (nameFromArg.length > 0) {
    return nameFromArg;
  }
  const startMs = Date.now();
  let attempt = 0;
  while (Date.now() - startMs < maxWaitMs) {
    try {
      return await pickOnlineDeviceName(baseUrl);
    } catch {
      /* 一覧未準備 */
    }
    if (attempt === 0 || attempt % 5 === 4) {
      try {
        await fetch(`${baseUrl}/api/commands/status`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ targetNames: "all" })
        });
      } catch {
        /* 続行 */
      }
    }
    attempt += 1;
    await sleep(2000);
  }
  throw new Error(
    `resolveDeviceNameWithWait: no online device within ${maxWaitMs}ms. hint="--targetDeviceName で実機名を指定" または MQTT 接続を確認`
  );
}

/**
 * @param {string} baseUrl
 * @param {string} nameFromArg
 * @param {number} maxWaitMs
 * @returns {Promise<void>}
 */
async function runLiveProbe(baseUrl, nameFromArg, maxWaitMs) {
  const healthResponse = await fetch(`${baseUrl}/api/health`);
  if (!healthResponse.ok) {
    throw new Error(
      `test7102LiveMqttHistory: LocalServer not reachable. GET /api/health status=${healthResponse.status} baseUrl=${baseUrl}`
    );
  }

  const deviceWaitMs = Math.min(120000, Math.max(60000, maxWaitMs));
  const deviceName = await resolveDeviceNameWithWait(baseUrl, nameFromArg, deviceWaitMs);
  console.info(`[INFO] 7102 live: using deviceName=${deviceName}`);
  const { token } = await loginAsAdmin(baseUrl);

  const baselinePack = await exportDeviceStatusCount(baseUrl, token, deviceName);
  const baseline = baselinePack.recordCount;
  console.info(
    `[INFO] 7102 live: baseline deviceStatus rows (filtered)=${baseline} deviceName=${deviceName} exportPath=${baselinePack.exportPath}`
  );

  const statusResponse = await fetch(`${baseUrl}/api/commands/status`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ targetNames: [deviceName] })
  });
  const statusJson = await readJsonResponseBodyOrThrow(statusResponse, "POST /api/commands/status");
  if (!statusResponse.ok || statusJson.result !== "OK") {
    throw new Error(
      `test7102LiveMqttHistory: status request failed. status=${statusResponse.status} detail=${JSON.stringify(statusJson)}`
    );
  }
  console.info(`[INFO] 7102 live: POST /api/commands/status OK. targetNames=[${deviceName}]`);

  const startMs = Date.now();
  let latest = baseline;
  while (Date.now() - startMs < maxWaitMs) {
    await sleep(3000);
    const nextPack = await exportDeviceStatusCount(baseUrl, token, deviceName);
    latest = nextPack.recordCount;
    if (latest > baseline) {
      const reportPath = path.join(
        projectRootPath,
        "logs",
        "test-reports",
        `7102-live-mqtt-${new Date().toISOString().replace(/[:.]/g, "-")}.json`
      );
      const reportDir = path.dirname(reportPath);
      if (!fs.existsSync(reportDir)) {
        fs.mkdirSync(reportDir, { recursive: true });
      }
      const report = {
        executedAt: new Date().toISOString(),
        baseUrl,
        deviceName,
        baselineDeviceStatusRows: baseline,
        afterDeviceStatusRows: latest,
        elapsedMs: Date.now() - startMs,
        maxWaitMs
      };
      fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`, "utf-8");
      console.info(
        `[OK] 7102 live: deviceStatusHistory increased. ${baseline} -> ${latest} (${Date.now() - startMs}ms) report=${reportPath}`
      );
      return;
    }
    console.info(`[INFO] 7102 live: polling... rows=${latest} elapsedMs=${Date.now() - startMs}`);
  }

  throw new Error(
    `test7102LiveMqttHistory: timeout. deviceStatus count did not increase after status command. baseline=${baseline} last=${latest} maxWaitMs=${maxWaitMs} deviceName=${deviceName}`
  );
}

/**
 * @returns {Promise<void>}
 */
async function main() {
  const { baseUrl, targetDeviceName, maxWaitMs, spawnLocalServer } = parseArgs(process.argv.slice(2));

  /** @type {import('child_process').ChildProcess | null} */
  let childProcess = null;
  if (spawnLocalServer) {
    if (!fs.existsSync(distServerPath)) {
      throw new Error(`test7102LiveMqttHistory: dist/server.js missing. run npm run build. path=${distServerPath}`);
    }
    console.info("[INFO] 7102 live: spawning LocalServer child (SecretCore bootstrap via server)...");
    childProcess = spawn(process.execPath, [distServerPath], {
      cwd: projectRootPath,
      env: { ...process.env },
      windowsHide: true,
      stdio: ["ignore", "pipe", "pipe"]
    });
    childProcess.stdout?.on("data", (chunk) => {
      process.stdout.write(chunk);
    });
    childProcess.stderr?.on("data", (chunk) => {
      process.stderr.write(chunk);
    });
    await waitForLocalServerHealth(baseUrl, 120000);
  }

  try {
    await runLiveProbe(baseUrl, targetDeviceName, maxWaitMs);
  } finally {
    if (childProcess !== null && typeof childProcess.pid === "number") {
      console.info("[INFO] 7102 live: stopping spawned LocalServer tree...");
      killProcessTreeWindows(childProcess.pid);
      await sleep(2000);
    }
  }
}

await main();
