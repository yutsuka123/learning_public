/**
 * @file test7083OtaDurability.mjs
 * @description 7083: OTA耐久試験（10回連続）を半自動実行するスクリプト。
 *
 * 主仕様:
 * - LocalServer 管理者ログイン後に signed OTA workflow を反復実行する
 * - 各回で workflow 完了状態、デバイス再オンライン、版数一致を確認する
 * - 各回の状態履歴と最終結果を JSON レポートへ保存する
 *
 * [重要] 版数を毎回変える運用が必要な場合は `--preIterationCommand` で事前コマンドを差し込む。
 * [厳守] 1回でも workflow failed、再オンライン失敗、版数不一致が出た場合はその時点で終了し、006章/007章へ進まない。
 * [制限事項] `../dist/config.js` を参照するため、事前に `npm run build` が必要。
 *
 * 実行例:
 * node scripts/test7083OtaDurability.mjs --iterations 10
 * node scripts/test7083OtaDurability.mjs --targetDeviceName esp32lab --iterations 10 --expectedVersion beta.12
 * node scripts/test7083OtaDurability.mjs --preIterationCommand "powershell -ExecutionPolicy Bypass -File ..\\scripts\\bump-esp32-beta-version.ps1"
 */

import {
  apiGetJson,
  apiPostJson,
  getAdminDevices,
  loginAsAdmin,
  pollDeviceUntil,
  pollWorkflowUntilTerminal,
  publicGetJson,
  runCommand,
  writeJsonReport
} from "./testCommon.mjs";

const defaultBaseUrl = "http://127.0.0.1:3100";

/**
 * @param {string[]} argv
 * @returns {{
 *   baseUrl: string;
 *   targetDeviceName: string | null;
 *   iterations: number;
 *   workflowTimeoutMilliseconds: number;
 *   onlineTimeoutMilliseconds: number;
 *   pollIntervalMilliseconds: number;
 *   expectedVersion: string | null;
 *   manifestUrl: string | null;
 *   firmwareUrl: string | null;
 *   preIterationCommand: string | null;
 * }}
 */
function parseArguments(argv) {
  return {
    baseUrl: readStringOption(argv, "--baseUrl", defaultBaseUrl),
    targetDeviceName: readNullableStringOption(argv, "--targetDeviceName"),
    iterations: readNumberOption(argv, "--iterations", 10),
    workflowTimeoutMilliseconds: readNumberOption(argv, "--workflowTimeoutMilliseconds", 180000),
    onlineTimeoutMilliseconds: readNumberOption(argv, "--onlineTimeoutMilliseconds", 180000),
    pollIntervalMilliseconds: readNumberOption(argv, "--pollIntervalMilliseconds", 3000),
    expectedVersion: readNullableStringOption(argv, "--expectedVersion"),
    manifestUrl: readNullableStringOption(argv, "--manifestUrl"),
    firmwareUrl: readNullableStringOption(argv, "--firmwareUrl"),
    preIterationCommand: readNullableStringOption(argv, "--preIterationCommand")
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
 * @param {string} baseUrl
 * @returns {Promise<string>}
 */
async function resolveExpectedVersion(baseUrl) {
  const settingsResponse = await publicGetJson(baseUrl, "/api/settings");
  const versionText = String(settingsResponse.settings?.otaFirmwareVersion ?? "").trim();
  if (versionText.length <= 0) {
    throw new Error(`resolveExpectedVersion failed. baseUrl=${baseUrl} otaFirmwareVersion is empty.`);
  }
  return versionText;
}

/**
 * @param {string} targetDeviceName
 * @param {string} expectedVersion
 * @param {any} deviceState
 * @returns {boolean}
 */
function isDeviceRecovered(targetDeviceName, expectedVersion, deviceState) {
  const currentTargetDeviceName = String(deviceState.targetName ?? deviceState.deviceName ?? "");
  const currentVersion = String(deviceState.firmwareVersion ?? "");
  const isOnline = String(deviceState.onlineState ?? "") === "online";
  return currentTargetDeviceName === targetDeviceName && isOnline && currentVersion === expectedVersion;
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} targetDeviceName
 * @param {string} expectedVersion
 * @param {string | null} manifestUrl
 * @param {string | null} firmwareUrl
 * @param {number} workflowTimeoutMilliseconds
 * @param {number} onlineTimeoutMilliseconds
 * @param {number} pollIntervalMilliseconds
 * @returns {Promise<any>}
 */
async function runSingleIteration(
  baseUrl,
  token,
  targetDeviceName,
  expectedVersion,
  manifestUrl,
  firmwareUrl,
  workflowTimeoutMilliseconds,
  onlineTimeoutMilliseconds,
  pollIntervalMilliseconds
) {
  const workflowStartResponse = await apiPostJson(baseUrl, token, "/api/workflows/signed-ota/start", {
    targetNames: [targetDeviceName],
    firmwareVersion: expectedVersion,
    manifestUrl: manifestUrl ?? undefined,
    firmwareUrl: firmwareUrl ?? undefined,
    timeoutSeconds: Math.ceil(workflowTimeoutMilliseconds / 1000)
  });
  const workflow = workflowStartResponse.workflow ?? {};
  const workflowId = String(workflow.workflowId ?? workflow.id ?? "").trim();
  if (workflowId.length <= 0) {
    throw new Error(
      `runSingleIteration failed. workflowId is empty. targetDeviceName=${targetDeviceName} expectedVersion=${expectedVersion}`
    );
  }

  const workflowTerminalResult = await pollWorkflowUntilTerminal(
    baseUrl,
    token,
    workflowId,
    workflowTimeoutMilliseconds,
    pollIntervalMilliseconds
  );
  if (String(workflowTerminalResult.workflow.state ?? "") !== "completed") {
    throw new Error(
      `runSingleIteration failed. workflow did not complete successfully. workflowId=${workflowId} state=${String(workflowTerminalResult.workflow.state ?? "")} detail=${String(workflowTerminalResult.workflow.detail ?? "")}`
    );
  }

  const statusResponse = await apiPostJson(baseUrl, token, "/api/commands/status", {
    targetNames: [targetDeviceName]
  });
  const recoveredDeviceState = await pollDeviceUntil(
    baseUrl,
    token,
    targetDeviceName,
    (deviceState) => isDeviceRecovered(targetDeviceName, expectedVersion, deviceState),
    onlineTimeoutMilliseconds,
    pollIntervalMilliseconds
  );

  return {
    workflowId,
    workflowTerminalResult,
    statusResponse,
    recoveredDeviceState
  };
}

/**
 * @param {ReturnType<typeof parseArguments>} options
 * @returns {Promise<void>}
 */
async function main(options) {
  const loginResult = await loginAsAdmin(options.baseUrl);
  const devices = await getAdminDevices(options.baseUrl, loginResult.token);
  const targetDeviceName = options.targetDeviceName ?? String(devices.find((deviceItem) => deviceItem.onlineState === "online")?.targetName ?? devices.find((deviceItem) => deviceItem.onlineState === "online")?.deviceName ?? "");
  if (!targetDeviceName) {
    throw new Error(`main failed. online target device not found. targetDeviceName=${String(options.targetDeviceName ?? "")}`);
  }

  console.log("[7083] OTA耐久試験を開始します。");
  console.log(`  baseUrl=${options.baseUrl}`);
  console.log(`  targetDeviceName=${targetDeviceName}`);
  console.log(`  iterations=${options.iterations}`);

  const report = {
    testId: "7083",
    startedAt: new Date().toISOString(),
    options,
    targetDeviceName,
    iterationResults: []
  };

  for (let iterationNumber = 1; iterationNumber <= options.iterations; iterationNumber += 1) {
    console.log(`\n[7083] ${iterationNumber}/${options.iterations} 回目を開始します。`);
    const iterationReport = {
      iterationNumber,
      startedAt: new Date().toISOString(),
      expectedVersion: "",
      preIterationCommand: options.preIterationCommand,
      commandResult: null,
      workflowId: "",
      workflowHistory: [],
      recoveredDeviceState: null,
      result: "NG",
      detail: ""
    };
    try {
      if (options.preIterationCommand) {
        console.log(`  事前コマンドを実行します: ${options.preIterationCommand}`);
        iterationReport.commandResult = await runCommand(options.preIterationCommand);
      }

      const expectedVersion = options.expectedVersion ?? (await resolveExpectedVersion(options.baseUrl));
      iterationReport.expectedVersion = expectedVersion;
      console.log(`  期待版数: ${expectedVersion}`);

      const iterationResult = await runSingleIteration(
        options.baseUrl,
        loginResult.token,
        targetDeviceName,
        expectedVersion,
        options.manifestUrl,
        options.firmwareUrl,
        options.workflowTimeoutMilliseconds,
        options.onlineTimeoutMilliseconds,
        options.pollIntervalMilliseconds
      );

      iterationReport.workflowId = iterationResult.workflowId;
      iterationReport.workflowHistory = iterationResult.workflowTerminalResult.workflowHistory;
      iterationReport.recoveredDeviceState = iterationResult.recoveredDeviceState;
      iterationReport.result = "OK";
      iterationReport.detail = `workflowId=${iterationResult.workflowId} version=${expectedVersion}`;
      iterationReport.completedAt = new Date().toISOString();
      report.iterationResults.push(iterationReport);
      console.log(`  [OK] ${iterationNumber} 回目成功 workflowId=${iterationResult.workflowId}`);
    } catch (iterationError) {
      iterationReport.result = "NG";
      iterationReport.detail = String(iterationError?.message ?? iterationError);
      iterationReport.completedAt = new Date().toISOString();
      report.iterationResults.push(iterationReport);
      report.completedAt = new Date().toISOString();
      report.result = "NG";
      report.detail = `iterationNumber=${iterationNumber} ${iterationReport.detail}`;
      const reportFilePath = writeJsonReport("test7083", report);
      console.error(`  [NG] ${iterationNumber} 回目で停止します: ${iterationReport.detail}`);
      console.error(`  [7083] レポート保存先: ${reportFilePath}`);
      process.exit(1);
    }
  }

  report.completedAt = new Date().toISOString();
  report.result = "OK";
  report.detail = `all ${options.iterations} iterations completed successfully.`;
  const reportFilePath = writeJsonReport("test7083", report);
  console.log(`\n[7083] OTA耐久試験 完了: OK`);
  console.log(`  reportFilePath=${reportFilePath}`);
}

main(parseArguments(process.argv.slice(2))).catch((mainError) => {
  console.error(`[7083] 試験失敗: ${String(mainError?.message ?? mainError)}`);
  process.exit(1);
});
