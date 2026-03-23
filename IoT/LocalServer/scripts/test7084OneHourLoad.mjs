/**
 * @file test7084OneHourLoad.mjs
 * @description 7084: 1時間過負荷試験（1時間×10セット）の監視ループを半自動実行するスクリプト。
 *
 * 主仕様:
 * - 各セットで `status`、`secure-ping`、`/api/admin/devices` 監視を定周期で反復する
 * - オンライン喪失、secure-ping 失敗、API 異常応答が出た時点で停止する
 * - 各セットの観測履歴、TRH 値、版数、partition 情報を JSON レポートへ保存する
 *
 * [重要] heap/stack 高水位や watchdog の最終判定は ESP32 シリアルログ/外部記録と併読すること。
 * [厳守] stop condition に該当した場合は 006章/007章へ進まず、`試験記録書.md` と `問題点記録書.md` に転記する。
 * [制限事項] 本スクリプト単体では AP モード遷移やシリアルログ採取は自動化しない。
 */

import {
  apiPostJson,
  getAdminDevices,
  loginAsAdmin,
  pollDeviceUntil,
  sleep,
  writeJsonReport
} from "./testCommon.mjs";

const defaultBaseUrl = "http://127.0.0.1:3100";

/**
 * @param {string[]} argv
 * @returns {{
 *   baseUrl: string;
 *   targetDeviceName: string | null;
 *   sets: number;
 *   setDurationMinutes: number;
 *   statusIntervalSeconds: number;
 *   securePingIntervalSeconds: number;
 *   snapshotIntervalSeconds: number;
 *   offlineRecoveryTimeoutMilliseconds: number;
 * }}
 */
function parseArguments(argv) {
  return {
    baseUrl: readStringOption(argv, "--baseUrl", defaultBaseUrl),
    targetDeviceName: readNullableStringOption(argv, "--targetDeviceName"),
    sets: readNumberOption(argv, "--sets", 10),
    setDurationMinutes: readNumberOption(argv, "--setDurationMinutes", 60),
    statusIntervalSeconds: readNumberOption(argv, "--statusIntervalSeconds", 60),
    securePingIntervalSeconds: readNumberOption(argv, "--securePingIntervalSeconds", 300),
    snapshotIntervalSeconds: readNumberOption(argv, "--snapshotIntervalSeconds", 60),
    offlineRecoveryTimeoutMilliseconds: readNumberOption(argv, "--offlineRecoveryTimeoutMilliseconds", 20000)
  };
}

/**
 * @param {string[]} argv
 * @param {string} optionName
 * @param {string} defaultValue
 * @returns {string}
 */
function readStringOption(argv, optionName, defaultValue) {
  const optionIndex = argv.indexOf(optionName);
  if (optionIndex < 0 || !argv[optionIndex + 1]) {
    return defaultValue;
  }
  return String(argv[optionIndex + 1]);
}

/**
 * @param {string[]} argv
 * @param {string} optionName
 * @returns {string | null}
 */
function readNullableStringOption(argv, optionName) {
  const optionIndex = argv.indexOf(optionName);
  if (optionIndex < 0 || !argv[optionIndex + 1]) {
    return null;
  }
  const optionValue = String(argv[optionIndex + 1]).trim();
  return optionValue.length > 0 ? optionValue : null;
}

/**
 * @param {string[]} argv
 * @param {string} optionName
 * @param {number} defaultValue
 * @returns {number}
 */
function readNumberOption(argv, optionName, defaultValue) {
  const optionValue = readNullableStringOption(argv, optionName);
  if (!optionValue) {
    return defaultValue;
  }
  const parsedValue = Number(optionValue);
  if (!Number.isFinite(parsedValue) || parsedValue <= 0) {
    throw new Error(`readNumberOption failed. optionName=${optionName} optionValue=${optionValue}`);
  }
  return parsedValue;
}

/**
 * @param {any} deviceState
 * @returns {Record<string, unknown>}
 */
function createDeviceSnapshot(deviceState) {
  return {
    observedAt: new Date().toISOString(),
    targetName: String(deviceState.targetName ?? deviceState.deviceName ?? ""),
    onlineState: String(deviceState.onlineState ?? ""),
    firmwareVersion: String(deviceState.firmwareVersion ?? ""),
    runningPartition: String(deviceState.runningPartition ?? ""),
    bootPartition: String(deviceState.bootPartition ?? ""),
    nextUpdatePartition: String(deviceState.nextUpdatePartition ?? ""),
    temperatureC: deviceState.temperatureC ?? null,
    humidityRh: deviceState.humidityRh ?? null,
    pressureHpa: deviceState.pressureHpa ?? null,
    detail: String(deviceState.detail ?? ""),
    lastSeenAt: String(deviceState.lastSeenAt ?? "")
  };
}

/**
 * @param {string} targetDeviceName
 * @param {any[]} devices
 * @returns {any}
 */
function findTargetDevice(targetDeviceName, devices) {
  const deviceState = devices.find((deviceItem) => String(deviceItem.targetName ?? deviceItem.deviceName ?? "") === targetDeviceName);
  if (!deviceState) {
    throw new Error(`findTargetDevice failed. targetDeviceName=${targetDeviceName} deviceState is not found.`);
  }
  return deviceState;
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} targetDeviceName
 * @param {number} offlineRecoveryTimeoutMilliseconds
 * @returns {Promise<any>}
 */
async function collectDeviceSnapshotOrFail(baseUrl, token, targetDeviceName, offlineRecoveryTimeoutMilliseconds) {
  const devices = await getAdminDevices(baseUrl, token);
  const targetDevice = findTargetDevice(targetDeviceName, devices);
  if (String(targetDevice.onlineState ?? "") === "online") {
    return createDeviceSnapshot(targetDevice);
  }
  const recoveredDevice = await pollDeviceUntil(
    baseUrl,
    token,
    targetDeviceName,
    (deviceState) => String(deviceState.onlineState ?? "") === "online",
    offlineRecoveryTimeoutMilliseconds,
    2000
  );
  return createDeviceSnapshot(recoveredDevice);
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} targetDeviceName
 * @param {number} setNumber
 * @returns {Promise<Record<string, unknown>>}
 */
async function executeSecurePing(baseUrl, token, targetDeviceName, setNumber) {
  const securePingResponse = await apiPostJson(baseUrl, token, "/api/admin/commands/secure-ping", {
    targetDeviceName,
    plainText: `7084 secure ping set=${setNumber} at=${new Date().toISOString()}`,
    timeoutMs: 15000
  });
  return {
    observedAt: new Date().toISOString(),
    requestId: String(securePingResponse.requestId ?? ""),
    targetDeviceName: String(securePingResponse.targetDeviceName ?? ""),
    decryptedResponse: securePingResponse.decryptedResponse ?? null
  };
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} targetDeviceName
 * @returns {Promise<Record<string, unknown>>}
 */
async function executeStatusCommand(baseUrl, token, targetDeviceName) {
  const statusResponse = await apiPostJson(baseUrl, token, "/api/commands/status", {
    targetNames: [targetDeviceName]
  });
  return {
    observedAt: new Date().toISOString(),
    result: String(statusResponse.result ?? ""),
    targetNames: statusResponse.targetNames ?? [targetDeviceName]
  };
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} targetDeviceName
 * @param {number} setNumber
 * @param {ReturnType<typeof parseArguments>} options
 * @returns {Promise<Record<string, unknown>>}
 */
async function runSingleSet(baseUrl, token, targetDeviceName, setNumber, options) {
  const setDurationMilliseconds = options.setDurationMinutes * 60 * 1000;
  const statusIntervalMilliseconds = options.statusIntervalSeconds * 1000;
  const securePingIntervalMilliseconds = options.securePingIntervalSeconds * 1000;
  const snapshotIntervalMilliseconds = options.snapshotIntervalSeconds * 1000;
  const setStartedAtMilliseconds = Date.now();
  const setReport = {
    setNumber,
    startedAt: new Date().toISOString(),
    statusEvents: [],
    securePingEvents: [],
    deviceSnapshots: [],
    result: "OK",
    detail: ""
  };

  let nextStatusAtMilliseconds = Date.now();
  let nextSecurePingAtMilliseconds = Date.now();
  let nextSnapshotAtMilliseconds = Date.now();

  while (Date.now() - setStartedAtMilliseconds < setDurationMilliseconds) {
    const currentMilliseconds = Date.now();
    if (currentMilliseconds >= nextStatusAtMilliseconds) {
      const statusEvent = await executeStatusCommand(baseUrl, token, targetDeviceName);
      setReport.statusEvents.push(statusEvent);
      console.log(`  [set ${setNumber}] status command sent at ${String(statusEvent.observedAt)}`);
      nextStatusAtMilliseconds = currentMilliseconds + statusIntervalMilliseconds;
    }
    if (currentMilliseconds >= nextSecurePingAtMilliseconds) {
      const securePingEvent = await executeSecurePing(baseUrl, token, targetDeviceName, setNumber);
      setReport.securePingEvents.push(securePingEvent);
      console.log(`  [set ${setNumber}] secure-ping ok requestId=${String(securePingEvent.requestId)}`);
      nextSecurePingAtMilliseconds = currentMilliseconds + securePingIntervalMilliseconds;
    }
    if (currentMilliseconds >= nextSnapshotAtMilliseconds) {
      const deviceSnapshot = await collectDeviceSnapshotOrFail(
        baseUrl,
        token,
        targetDeviceName,
        options.offlineRecoveryTimeoutMilliseconds
      );
      setReport.deviceSnapshots.push(deviceSnapshot);
      console.log(
        `  [set ${setNumber}] snapshot online=${String(deviceSnapshot.onlineState)} version=${String(deviceSnapshot.firmwareVersion)} temp=${String(deviceSnapshot.temperatureC)}`
      );
      nextSnapshotAtMilliseconds = currentMilliseconds + snapshotIntervalMilliseconds;
    }
    await sleep(1000);
  }

  setReport.completedAt = new Date().toISOString();
  setReport.detail = `statusEvents=${setReport.statusEvents.length} securePingEvents=${setReport.securePingEvents.length} deviceSnapshots=${setReport.deviceSnapshots.length}`;
  return setReport;
}

/**
 * @param {ReturnType<typeof parseArguments>} options
 * @returns {Promise<void>}
 */
async function main(options) {
  const loginResult = await loginAsAdmin(options.baseUrl);
  const initialDevices = await getAdminDevices(options.baseUrl, loginResult.token);
  const targetDeviceName =
    options.targetDeviceName ??
    String(initialDevices.find((deviceItem) => deviceItem.onlineState === "online")?.targetName ?? initialDevices.find((deviceItem) => deviceItem.onlineState === "online")?.deviceName ?? "");
  if (!targetDeviceName) {
    throw new Error(`main failed. online target device not found. targetDeviceName=${String(options.targetDeviceName ?? "")}`);
  }

  console.log("[7084] 1時間過負荷試験を開始します。");
  console.log(`  baseUrl=${options.baseUrl}`);
  console.log(`  targetDeviceName=${targetDeviceName}`);
  console.log(`  sets=${options.sets} setDurationMinutes=${options.setDurationMinutes}`);

  const report = {
    testId: "7084",
    startedAt: new Date().toISOString(),
    options,
    targetDeviceName,
    setResults: []
  };

  for (let setNumber = 1; setNumber <= options.sets; setNumber += 1) {
    console.log(`\n[7084] set ${setNumber}/${options.sets} を開始します。`);
    try {
      const setReport = await runSingleSet(options.baseUrl, loginResult.token, targetDeviceName, setNumber, options);
      report.setResults.push(setReport);
      console.log(`  [OK] set ${setNumber} 完了 ${String(setReport.detail)}`);
    } catch (setError) {
      const failedSetReport = {
        setNumber,
        startedAt: new Date().toISOString(),
        result: "NG",
        detail: String(setError?.message ?? setError)
      };
      report.setResults.push(failedSetReport);
      report.completedAt = new Date().toISOString();
      report.result = "NG";
      report.detail = `setNumber=${setNumber} ${String(setError?.message ?? setError)}`;
      const reportFilePath = writeJsonReport("test7084", report);
      console.error(`  [NG] set ${setNumber} で停止します: ${String(setError?.message ?? setError)}`);
      console.error(`  [7084] レポート保存先: ${reportFilePath}`);
      process.exit(1);
    }
  }

  report.completedAt = new Date().toISOString();
  report.result = "OK";
  report.detail = `all ${options.sets} sets completed successfully.`;
  const reportFilePath = writeJsonReport("test7084", report);
  console.log(`\n[7084] 1時間過負荷試験 完了: OK`);
  console.log(`  reportFilePath=${reportFilePath}`);
}

main(parseArguments(process.argv.slice(2))).catch((mainError) => {
  console.error(`[7084] 試験失敗: ${String(mainError?.message ?? mainError)}`);
  process.exit(1);
});
