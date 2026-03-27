/**
 * @file checkDeviceOnlineState.mjs
 * @description 対象機の online 状態を確認する簡易スクリプト
 */

import { getAdminDevices, loginAsAdmin } from "./testCommon.mjs";

const targetDeviceName = process.argv[2] ?? "IoT_F0D0F94EB580";
const baseUrl = "http://127.0.0.1:3100";

async function main() {
  const loginResult = await loginAsAdmin(baseUrl);
  const devices = await getAdminDevices(baseUrl, loginResult.token);
  const target = devices.find(
    (d) => d.targetName === targetDeviceName || d.deviceName === targetDeviceName
  );
  if (!target) {
    throw new Error(`target device not found. targetDeviceName=${targetDeviceName}`);
  }
  console.log(
    JSON.stringify(
      {
        onlineState: target.onlineState,
        firmwareVersion: target.firmwareVersion,
        lastSeenAt: target.lastSeenAt,
        detail: target.detail
      },
      null,
      2
    )
  );
}

try {
  await main();
} catch (error) {
  console.error(
    `checkDeviceOnlineState failed. error=${error instanceof Error ? error.message : String(error)}`
  );
  process.exit(1);
}
