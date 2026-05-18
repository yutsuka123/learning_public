/**
 * @file test7108BidirectionalE2E.mjs
 * @description 試験仕様書 **7108** — LocalServer ↔ デバイス 双方向テスト・履歴記録確認（009-0006）。
 *
 * 検証内容:
 * - status コマンドを発行し、commandHistory に記録されることを確認する。
 * - デバイスが status 応答を返し、deviceStatusHistory に記録されることを確認する。
 * - GET /api/admin/devices でデバイスの lastSeenAt が更新されていることを確認する。
 *
 * [前提]
 * - LocalServer 起動済み（npm run build + npm run start）。Node.js v22。
 * - MQTT ブローカ接続済みの ESP32 が 1 台以上 online。
 *
 * 実行例:
 *   cd IoT/LocalServer && npm run test:7108
 *   node scripts/test7108BidirectionalE2E.mjs --baseUrl http://127.0.0.1:3100
 *   node scripts/test7108BidirectionalE2E.mjs --targetDeviceName IoT_XXXXX
 */

import path from "path";
import { fileURLToPath } from "url";
import {
  loginAsAdmin,
  apiGetJson,
  apiPostJson,
  sleep,
  writeJsonReport,
  createTimestampText
} from "./testCommon.mjs";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));

/**
 * @param {string[]} argv
 * @returns {{ baseUrl: string; targetDeviceName: string; maxWaitMs: number }}
 */
function parseArgs(argv) {
  const baseUrlIdx = argv.indexOf("--baseUrl");
  const baseUrl =
    baseUrlIdx >= 0 && argv[baseUrlIdx + 1] ? String(argv[baseUrlIdx + 1]).trim() : "http://127.0.0.1:3100";
  const nameIdx = argv.indexOf("--targetDeviceName");
  const targetDeviceName = nameIdx >= 0 && argv[nameIdx + 1] ? String(argv[nameIdx + 1]).trim() : "";
  const waitIdx = argv.indexOf("--maxWaitMs");
  const rawWait = waitIdx >= 0 && argv[waitIdx + 1] ? Number.parseInt(String(argv[waitIdx + 1]), 10) : Number.NaN;
  const maxWaitMs = Number.isFinite(rawWait) && rawWait >= 10000 ? rawWait : 90000;
  return { baseUrl, targetDeviceName, maxWaitMs };
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {"deviceStatus" | "command"} source
 * @param {string} deviceName
 * @returns {Promise<number>}
 */
async function getHistoryCount(baseUrl, token, source, deviceName) {
  const exportBody = {
    sources: [source],
    fieldCombine: "AND",
    ...(deviceName ? { deviceNo: deviceName } : {}),
    fileBaseName: `7108-probe-${source}-${Date.now()}`
  };
  const result = await apiPostJson(baseUrl, token, "/api/admin/local-history/export", exportBody);
  if (result.result !== "OK") {
    throw new Error(`getHistoryCount failed. source=${source} detail=${JSON.stringify(result)}`);
  }
  return Number(result.recordCount ?? 0);
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} deviceName
 * @param {string} beforeLastSeenAt
 * @param {number} maxWaitMs
 * @returns {Promise<string>} 更新後の lastSeenAt
 */
async function waitForDeviceStatusUpdate(baseUrl, token, deviceName, beforeLastSeenAt, maxWaitMs) {
  const startMs = Date.now();
  while (Date.now() - startMs < maxWaitMs) {
    await sleep(3000);
    const devicesResult = await apiGetJson(baseUrl, token, "/api/admin/devices");
    const devices = Array.isArray(devicesResult.devices) ? devicesResult.devices : [];
    const target = devices.find((d) => d.deviceName === deviceName);
    if (target === undefined) {
      console.warn(`[WARN] device not found in /api/admin/devices. deviceName=${deviceName}`);
      continue;
    }
    const currentLastSeenAt = String(target.lastSeenAt ?? "");
    if (currentLastSeenAt !== beforeLastSeenAt && currentLastSeenAt.length > 0) {
      return currentLastSeenAt;
    }
  }
  throw new Error(
    `waitForDeviceStatusUpdate timeout. deviceName=${deviceName} maxWaitMs=${maxWaitMs} beforeLastSeenAt=${beforeLastSeenAt}`
  );
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} requestedName
 * @param {number} maxWaitMs
 * @returns {Promise<{ deviceName: string; lastSeenAt: string; onlineState: string }>}
 */
async function resolveOnlineDevice(baseUrl, token, requestedName, maxWaitMs) {
  const startMs = Date.now();
  while (Date.now() - startMs < maxWaitMs) {
    const devicesResult = await apiGetJson(baseUrl, token, "/api/admin/devices");
    const devices = Array.isArray(devicesResult.devices) ? devicesResult.devices : [];
    for (const d of devices) {
      const name = String(d.deviceName ?? "").trim();
      const online = String(d.onlineState ?? "").toLowerCase().includes("online");
      if (!online) continue;
      if (requestedName.length > 0 && name !== requestedName) continue;
      return {
        deviceName: name,
        lastSeenAt: String(d.lastSeenAt ?? ""),
        onlineState: String(d.onlineState ?? "")
      };
    }
    if (Date.now() - startMs < maxWaitMs) {
      await sleep(5000);
    }
  }
  throw new Error(
    `resolveOnlineDevice timeout. maxWaitMs=${maxWaitMs} requestedName=${requestedName || "(any)"} hint="ESP32 の MQTT online を確認"`
  );
}

async function main() {
  const { baseUrl, targetDeviceName, maxWaitMs } = parseArgs(process.argv.slice(2));
  const startedAt = new Date().toISOString();
  console.info(`[INFO] test7108 start. baseUrl=${baseUrl} targetDeviceName=${targetDeviceName || "(auto)"}`);

  const { token } = await loginAsAdmin(baseUrl);
  console.info("[OK] admin login");

  const device = await resolveOnlineDevice(baseUrl, token, targetDeviceName, 30000);
  console.info(`[OK] online device found. deviceName=${device.deviceName} lastSeenAt=${device.lastSeenAt}`);

  const beforeStatusCount = await getHistoryCount(baseUrl, token, "deviceStatus", device.deviceName);
  const beforeCommandCount = await getHistoryCount(baseUrl, token, "command", device.deviceName);
  console.info(
    `[INFO] baseline. deviceStatusHistory=${beforeStatusCount} commandHistory=${beforeCommandCount}`
  );

  const commandBody = { targetNames: [device.deviceName] };
  const commandResult = await apiPostJson(baseUrl, token, "/api/commands/status", commandBody);
  if (commandResult.result !== "OK") {
    throw new Error(`POST /api/commands/status failed. detail=${JSON.stringify(commandResult)}`);
  }
  const commandSentAt = new Date().toISOString();
  console.info(`[OK] status command dispatched. commandSentAt=${commandSentAt}`);

  const updatedLastSeenAt = await waitForDeviceStatusUpdate(
    baseUrl,
    token,
    device.deviceName,
    device.lastSeenAt,
    maxWaitMs
  );
  console.info(`[OK] device status response received. updatedLastSeenAt=${updatedLastSeenAt}`);

  const afterStatusCount = await getHistoryCount(baseUrl, token, "deviceStatus", device.deviceName);
  const afterCommandCount = await getHistoryCount(baseUrl, token, "command", device.deviceName);
  console.info(
    `[INFO] after. deviceStatusHistory=${afterStatusCount} commandHistory=${afterCommandCount}`
  );

  const statusIncrement = afterStatusCount - beforeStatusCount;
  const commandIncrement = afterCommandCount - beforeCommandCount;

  if (statusIncrement < 1) {
    throw new Error(
      `deviceStatusHistory did not increase. before=${beforeStatusCount} after=${afterStatusCount}`
    );
  }
  if (commandIncrement < 1) {
    throw new Error(
      `commandHistory did not increase. before=${beforeCommandCount} after=${afterCommandCount}`
    );
  }

  console.info(`[OK] deviceStatusHistory increased by ${statusIncrement}`);
  console.info(`[OK] commandHistory increased by ${commandIncrement}`);

  const report = {
    testId: "7108",
    result: "OK",
    startedAt,
    completedAt: new Date().toISOString(),
    baseUrl,
    deviceName: device.deviceName,
    onlineState: device.onlineState,
    commandSentAt,
    updatedLastSeenAt,
    before: { deviceStatusHistory: beforeStatusCount, commandHistory: beforeCommandCount },
    after: { deviceStatusHistory: afterStatusCount, commandHistory: afterCommandCount },
    increment: { deviceStatusHistory: statusIncrement, commandHistory: commandIncrement }
  };

  writeJsonReport("test7108-bidirectional-e2e", report);
  console.info(`[OK] test7108 PASSED. deviceStatusHistory+${statusIncrement} commandHistory+${commandIncrement}`);
  return report;
}

main().catch((err) => {
  console.error(`[NG] test7108 FAILED. ${err instanceof Error ? err.message : String(err)}`);
  process.exit(1);
});
