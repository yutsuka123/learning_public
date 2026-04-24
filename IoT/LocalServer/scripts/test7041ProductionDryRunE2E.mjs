/**
 * @file test7041ProductionDryRunE2E.mjs
 * @description 7041: `runProductionSecureFlow()` の dry-run end-to-end 試験を半自動実行するスクリプト。
 *
 * 主仕様:
 * - `POST /api/workflows/production/start` を実行し、workflow が `completed/OK` へ到達することを確認する。
 * - 実行後に AP `GET /api/production/state` を参照し、`runId`、`state=precheck_collected`、観測 firmware/MAC/heap/stack を確認する。
 * - workflow 開始前に AP ログイン preflight を行い、到達性・認証可否を先に切り分ける。
 * - 応答に raw key、中間秘密、`eFuse` 実値が含まれないことを確認する。
 *
 * [重要] 本試験は dry-run 専用であり、不可逆処理は実行しない。
 * [厳守] `apBaseUrl` / `apMfgUsername` / `apMfgPassword` / `targetDeviceId` / `expectedMac` は workflow 実行時に必須。
 *        CLI未指定時は LocalServer 設定とデバイス一覧から補完する。
 * [制限事項] AP 接続済みかつ `LocalServer` / `SecretCore` / ESP32 の最新版反映が前提。
 */

import {
  apiPostJson,
  getAdminDevices,
  getMaintenanceApProductionState,
  loginAsAdmin,
  loginToMaintenanceAp,
  pollWorkflowUntilTerminal,
  resolveTargetDeviceName,
  writeJsonReport
} from "./testCommon.mjs";
import os from "node:os";
import { execSync } from "node:child_process";

const defaultBaseUrl = "http://127.0.0.1:3100";

function parseArguments(argv) {
  const baseUrl = readStringOption(argv, "--baseUrl", defaultBaseUrl);
  const targetDeviceId = readNullableStringOption(argv, "--targetDeviceId");
  const apBaseUrl = readNullableStringOption(argv, "--apBaseUrl");
  const apMfgUsername = readNullableStringOption(argv, "--apMfgUsername");
  const apMfgPassword = readNullableStringOption(argv, "--apMfgPassword");
  const expectedMac = readNullableStringOption(argv, "--expectedMac");
  const expectedFirmwareVersion = readNullableStringOption(argv, "--expectedFirmwareVersion");
  const runId = readStringOption(argv, "--runId", `run-7041-${Date.now()}`);
  const minimumFreeHeapBytes = readNumberOption(argv, "--minimumFreeHeapBytes", 50000);
  const minimumStackMarginBytes = readNumberOption(argv, "--minimumStackMarginBytes", 4096);
  const pollTimeoutMs = readNumberOption(argv, "--pollTimeoutMs", 90000);
  const pollIntervalMs = readNumberOption(argv, "--pollIntervalMs", 2000);
  const preflightRetryCount = readNumberOption(argv, "--preflightRetryCount", 3);
  const preflightRetryIntervalMs = readNumberOption(argv, "--preflightRetryIntervalMs", 3000);
  const skipApPreflight = argv.includes("--skipApPreflight");
  const preflightOnly = argv.includes("--preflightOnly");
  return {
    baseUrl,
    targetDeviceId,
    apBaseUrl,
    apMfgUsername,
    apMfgPassword,
    expectedMac,
    expectedFirmwareVersion,
    runId,
    minimumFreeHeapBytes,
    minimumStackMarginBytes,
    pollTimeoutMs,
    pollIntervalMs,
    preflightRetryCount,
    preflightRetryIntervalMs,
    skipApPreflight,
    preflightOnly
  };
}

function readStringOption(argv, optionName, defaultValue) {
  const index = argv.indexOf(optionName);
  if (index < 0 || !argv[index + 1]) {
    return defaultValue;
  }
  return String(argv[index + 1]);
}

function readRequiredStringOption(argv, optionName) {
  const value = readStringOption(argv, optionName, "");
  if (value.trim().length === 0) {
    throw new Error(`readRequiredStringOption failed. optionName=${optionName} is required.`);
  }
  return value.trim();
}

function readNullableStringOption(argv, optionName) {
  const value = readStringOption(argv, optionName, "");
  const normalizedValue = value.trim();
  return normalizedValue.length > 0 ? normalizedValue : null;
}

function readNumberOption(argv, optionName, defaultValue) {
  const rawValue = readStringOption(argv, optionName, "");
  if (rawValue.length === 0) {
    return defaultValue;
  }
  const numericValue = Number(rawValue);
  if (!Number.isFinite(numericValue) || numericValue <= 0) {
    throw new Error(`readNumberOption failed. optionName=${optionName} rawValue=${rawValue}`);
  }
  return numericValue;
}

/**
 * AP の到達性と認証可否を workflow 開始前に確認する。
 * @param {{
 *   apBaseUrl: string;
 *   apMfgUsername: string;
 *   apMfgPassword: string;
 * }} params
 * @returns {Promise<void>}
 */
async function verifyApPreflight(params) {
  try {
    const loginResult = await loginToMaintenanceAp(params.apBaseUrl, params.apMfgUsername, params.apMfgPassword);
    if (String(loginResult.token ?? "").trim().length === 0) {
      throw new Error("verifyApPreflight failed. ap token is empty.");
    }
  } catch (error) {
    const networkDiagnosis = collectPreflightNetworkDiagnosis(params.apBaseUrl);
    throw new Error(
      `verifyApPreflight failed. apBaseUrl=${params.apBaseUrl} apMfgUsername=${params.apMfgUsername} detail=${String(error?.message ?? error)} networkDiagnosis=${networkDiagnosis}`
    );
  }
}

function sleepMilliseconds(waitMilliseconds) {
  return new Promise((resolve) => {
    setTimeout(resolve, waitMilliseconds);
  });
}

/**
 * AP preflight を複数回リトライする。
 * [重要][修正理由] 起動直後や Wi-Fi 再接続直後は AP が一瞬だけ不安定になることがあるため、
 *                単発失敗で即 NG にせず、短い間隔で再試行して環境揺らぎを吸収する。
 * @param {{
 *   apBaseUrl: string;
 *   apMfgUsername: string;
 *   apMfgPassword: string;
 *   preflightRetryCount: number;
 *   preflightRetryIntervalMs: number;
 * }} params
 * @returns {Promise<void>}
 */
async function verifyApPreflightWithRetry(params) {
  const retryCount = Math.max(1, Number(params.preflightRetryCount ?? 1));
  const retryIntervalMilliseconds = Math.max(1, Number(params.preflightRetryIntervalMs ?? 3000));
  let lastError = null;
  for (let attemptNumber = 1; attemptNumber <= retryCount; attemptNumber += 1) {
    try {
      await verifyApPreflight(params);
      if (attemptNumber > 1) {
        console.log(`  [7041] AP preflight retry succeeded. attempt=${attemptNumber}/${retryCount}`);
      }
      return;
    } catch (error) {
      lastError = error;
      if (attemptNumber < retryCount) {
        console.log(
          `  [7041] AP preflight retry scheduled. attempt=${attemptNumber}/${retryCount} waitMs=${retryIntervalMilliseconds}`
        );
        await sleepMilliseconds(retryIntervalMilliseconds);
      }
    }
  }
  throw new Error(
    `verifyApPreflightWithRetry failed. retryCount=${retryCount} retryIntervalMs=${retryIntervalMilliseconds} detail=${String(
      lastError?.message ?? lastError
    )}`
  );
}

/**
 * preflight失敗時の切り分け情報を文字列化する。
 * @param {string} apBaseUrl
 * @returns {string}
 */
function collectPreflightNetworkDiagnosis(apBaseUrl) {
  const routeHint = collectRouteHint();
  const arpHint = collectArpHint();
  const localIpv4List = collectLocalIpv4List();
  const diagnosisPartList = [];
  diagnosisPartList.push(`targetHost=${extractHostFromUrl(apBaseUrl)}`);
  diagnosisPartList.push(`localIpv4=${localIpv4List.join(",") || "(none)"}`);
  diagnosisPartList.push(`routeHint=${routeHint}`);
  diagnosisPartList.push(`arpHint=${arpHint}`);
  diagnosisPartList.push(`nextAction=${buildPreflightNextAction(routeHint, arpHint, localIpv4List)}`);
  return diagnosisPartList.join(" | ");
}

/**
 * URLから host を取り出す。失敗時は文字列を返す。
 * @param {string} apBaseUrl
 * @returns {string}
 */
function extractHostFromUrl(apBaseUrl) {
  try {
    return new URL(apBaseUrl).host;
  } catch {
    return `(invalid-url:${String(apBaseUrl ?? "")})`;
  }
}

/**
 * ローカルIPv4一覧を列挙する。
 * @returns {string[]}
 */
function collectLocalIpv4List() {
  const networkInterfaces = os.networkInterfaces();
  const ipv4List = [];
  for (const [interfaceName, interfaceItemList] of Object.entries(networkInterfaces)) {
    for (const interfaceItem of interfaceItemList ?? []) {
      if (interfaceItem == null) {
        continue;
      }
      if (interfaceItem.family !== "IPv4" || interfaceItem.internal) {
        continue;
      }
      ipv4List.push(`${interfaceName}:${interfaceItem.address}`);
    }
  }
  return ipv4List;
}

/**
 * Windows route print から 192.168.4.1 の経路ヒントを抽出する。
 * @returns {string}
 */
function collectRouteHint() {
  try {
    const routeOutput = execSync("route print -4", {
      stdio: ["ignore", "pipe", "pipe"],
      encoding: "utf8",
      timeout: 3000
    });
    const routeLine = routeOutput
      .split(/\r?\n/)
      .map((line) => line.trim())
      .find((line) => line.includes("192.168.4.1") || line.includes("192.168.4.0"));
    return routeLine ?? "route-to-192.168.4.x-not-found";
  } catch (error) {
    return `route-check-failed:${String(error?.message ?? error)}`;
  }
}

/**
 * Windows ARP から 192.168.4.x の観測有無を抽出する。
 * @returns {string}
 */
function collectArpHint() {
  try {
    const arpOutput = execSync("arp -a", {
      stdio: ["ignore", "pipe", "pipe"],
      encoding: "utf8",
      timeout: 3000
    });
    const arpLine = arpOutput
      .split(/\r?\n/)
      .map((line) => line.trim())
      .find((line) => line.includes("192.168.4."));
    return arpLine ?? "arp-192.168.4.x-not-found";
  } catch (error) {
    return `arp-check-failed:${String(error?.message ?? error)}`;
  }
}

/**
 * 診断情報から次アクションを短文で組み立てる。
 * @param {string} routeHint
 * @param {string} arpHint
 * @param {string[]} localIpv4List
 * @returns {string}
 */
function buildPreflightNextAction(routeHint, arpHint, localIpv4List) {
  const has192SegmentAddress = localIpv4List.some((item) => item.includes(":192.168.4."));
  const hasRouteTo192Segment = !routeHint.startsWith("route-to-192.168.4.x-not-found") && !routeHint.startsWith("route-check-failed:");
  const hasArpTo192Segment = !arpHint.startsWith("arp-192.168.4.x-not-found") && !arpHint.startsWith("arp-check-failed:");
  if (!has192SegmentAddress && !hasRouteTo192Segment && !hasArpTo192Segment) {
    return "connect PC to ESP32 AP and confirm NIC gets 192.168.4.x";
  }
  if (has192SegmentAddress && !hasRouteTo192Segment) {
    return "verify routing table and disable conflicting VPN/virtual adapters temporarily";
  }
  if (hasRouteTo192Segment && !hasArpTo192Segment) {
    return "check AP power/SSID and retry login after reconnecting Wi-Fi";
  }
  return "verify AP credentials and re-run test:7041:preflight";
}

/**
 * 失敗メッセージを運用カテゴリへ分類する。
 * @param {string} errorMessage
 * @returns {"ap_unreachable" | "ap_auth_or_permission" | "workflow_failed" | "unknown"}
 */
function classifyFailure(errorMessage) {
  const normalizedMessage = String(errorMessage ?? "").toLowerCase();
  if (
    normalizedMessage.includes("verifyappreflight failed") ||
    normalizedMessage.includes("fetch failed") ||
    normalizedMessage.includes("route-to-192.168.4.x-not-found") ||
    normalizedMessage.includes("arp-192.168.4.x-not-found")
  ) {
    return "ap_unreachable";
  }
  if (
    normalizedMessage.includes("401") ||
    normalizedMessage.includes("unauthorized") ||
    normalizedMessage.includes("forbidden") ||
    normalizedMessage.includes("mfg authorization required")
  ) {
    return "ap_auth_or_permission";
  }
  if (normalizedMessage.includes("runproductionworkflowtest failed") || normalizedMessage.includes("terminalstate=failed")) {
    return "workflow_failed";
  }
  return "unknown";
}

/**
 * 失敗カテゴリに応じた復旧ヒントを返す。
 * @param {"ap_unreachable" | "ap_auth_or_permission" | "workflow_failed" | "unknown"} failureCategory
 * @returns {string}
 */
function getFailureRecommendation(failureCategory) {
  if (failureCategory === "ap_unreachable") {
    return "connect PC to ESP32 AP, confirm 192.168.4.x route, then run npm run test:7041:preflight";
  }
  if (failureCategory === "ap_auth_or_permission") {
    return "verify mfg credentials/role and retry npm run test:7041:preflight";
  }
  if (failureCategory === "workflow_failed") {
    return "after preflight OK, inspect workflow errorSummary/detail and SecretCore logs";
  }
  return "collect logs and re-run preflight/full test to isolate environment vs workflow";
}

/**
 * 失敗メッセージ内の networkDiagnosis を構造化して取り出す。
 * @param {string} errorMessage
 * @returns {{
 *   targetHost: string;
 *   localIpv4: string;
 *   routeHint: string;
 *   arpHint: string;
 *   nextAction: string;
 * } | null}
 */
function extractDiagnosisFromErrorMessage(errorMessage) {
  const normalizedMessage = String(errorMessage ?? "");
  const diagnosisPrefix = "networkDiagnosis=";
  const prefixIndex = normalizedMessage.indexOf(diagnosisPrefix);
  if (prefixIndex < 0) {
    return null;
  }
  const diagnosisText = normalizedMessage.slice(prefixIndex + diagnosisPrefix.length);
  const partList = diagnosisText.split(" | ").map((part) => part.trim());
  const diagnosisMap = {};
  for (const partItem of partList) {
    const separatorIndex = partItem.indexOf("=");
    if (separatorIndex < 0) {
      continue;
    }
    const keyText = partItem.slice(0, separatorIndex).trim();
    const valueText = partItem.slice(separatorIndex + 1).trim();
    if (keyText.length === 0) {
      continue;
    }
    diagnosisMap[keyText] = valueText;
  }
  return {
    targetHost: String(diagnosisMap.targetHost ?? ""),
    localIpv4: String(diagnosisMap.localIpv4 ?? ""),
    routeHint: String(diagnosisMap.routeHint ?? ""),
    arpHint: String(diagnosisMap.arpHint ?? ""),
    nextAction: String(diagnosisMap.nextAction ?? "")
  };
}

function assertNoSecretLeakage(functionName, payload) {
  const payloadJson = JSON.stringify(payload ?? {});
  const forbiddenHints = ["keyDevice\":\"", "sharedSecret", "transportSessionKey", "efuseValue", "rawKey"];
  const foundHint = forbiddenHints.find((hint) => payloadJson.includes(hint));
  if (foundHint) {
    throw new Error(`assertNoSecretLeakage failed. functionName=${functionName} foundHint=${foundHint}`);
  }
}

function resolveTargetDeviceIdFromDevice(device) {
  const candidateValueList = [
    String(device.publicId ?? "").trim(),
    String(device.targetName ?? "").trim(),
    String(device.deviceName ?? "").trim(),
    String(device.macAddr ?? "").trim()
  ];
  const resolvedValue = candidateValueList.find((value) => value.length > 0) ?? "";
  if (resolvedValue.length === 0) {
    throw new Error("resolveTargetDeviceIdFromDevice failed. all identifiers are empty.");
  }
  return resolvedValue;
}

function resolveExpectedMacFromDevice(device) {
  const candidateValueList = [
    String(device.macAddr ?? "").trim(),
    String(device.sourceMac ?? "").trim()
  ];
  const resolvedValue = candidateValueList.find((value) => value.length > 0) ?? "";
  if (resolvedValue.length === 0) {
    throw new Error("resolveExpectedMacFromDevice failed. mac address is empty.");
  }
  return resolvedValue;
}

function resolveExpectedFirmwareVersionFromDevice(device) {
  const resolvedValue = String(device.firmwareVersion ?? "").trim();
  if (resolvedValue.length === 0) {
    throw new Error("resolveExpectedFirmwareVersionFromDevice failed. firmwareVersion is empty.");
  }
  return resolvedValue;
}

function resolveApMfgCredentialsFromConfig(config) {
  const apBaseUrl = String(config.apHttpBaseUrl ?? "").trim();
  const preferredMfgUsername = String(config.apRoleMfgUsername ?? "").trim();
  const preferredMfgPassword = String(config.apRoleMfgPassword ?? "");
  const fallbackAdminUsername = String(config.apRoleAdminUsername ?? "").trim();
  const fallbackAdminPassword = String(config.apRoleAdminPassword ?? "");
  const apMfgUsername = preferredMfgUsername.length > 0 ? preferredMfgUsername : fallbackAdminUsername;
  const apMfgPassword = preferredMfgPassword.length > 0 ? preferredMfgPassword : fallbackAdminPassword;
  if (apBaseUrl.length === 0 || apMfgUsername.length === 0 || apMfgPassword.length === 0) {
    throw new Error(
      `resolveApMfgCredentialsFromConfig failed. apBaseUrl=${apBaseUrl} apMfgUsername=${apMfgUsername} hasPassword=${apMfgPassword.length > 0}. hint=specify --apMfgUsername/--apMfgPassword`
    );
  }
  return { apBaseUrl, apMfgUsername, apMfgPassword };
}

function buildProductionStartBody(params) {
  return {
    targetDeviceId: params.targetDeviceId,
    runId: params.runId,
    productionSettings: {
      dryRun: true,
      expectedMac: params.expectedMac,
      expectedFirmwareVersion: params.expectedFirmwareVersion,
      minimumFreeHeapBytes: params.minimumFreeHeapBytes,
      minimumStackMarginBytes: params.minimumStackMarginBytes,
      apBaseUrl: params.apBaseUrl,
      apUsername: params.apMfgUsername,
      apPassword: params.apMfgPassword,
      operatorComment: "7041 dry-run verification"
    }
  };
}

function resolveExecutionParameters(params, loginResult, devices) {
  const normalizedTargetDeviceId = String(params.targetDeviceId ?? "").trim();
  const normalizedExpectedMac = String(params.expectedMac ?? "").trim();
  const normalizedExpectedFirmwareVersion = String(params.expectedFirmwareVersion ?? "").trim();
  let resolvedDevice = null;
  if (
    normalizedTargetDeviceId.length === 0 ||
    normalizedExpectedMac.length === 0 ||
    normalizedExpectedFirmwareVersion.length === 0
  ) {
    if (!Array.isArray(devices) || devices.length === 0) {
      throw new Error(
        "resolveExecutionParameters failed. admin devices list is empty. hint=connect target device once or specify --targetDeviceId/--expectedMac/--expectedFirmwareVersion manually."
      );
    }
    let targetDeviceName = "";
    try {
      targetDeviceName = resolveTargetDeviceName(devices, null);
    } catch {
      targetDeviceName = String(devices[0]?.targetName ?? devices[0]?.deviceName ?? "").trim();
    }
    resolvedDevice =
      devices.find((deviceItem) => String(deviceItem.targetName ?? "").trim() === targetDeviceName) ??
      devices.find((deviceItem) => String(deviceItem.deviceName ?? "").trim() === targetDeviceName) ??
      null;
    if (resolvedDevice == null) {
      throw new Error(`resolveExecutionParameters failed. targetDeviceName=${targetDeviceName} not found in devices.`);
    }
  }
  const resolvedTargetDeviceId =
    normalizedTargetDeviceId.length > 0 ? normalizedTargetDeviceId : resolveTargetDeviceIdFromDevice(resolvedDevice);
  const resolvedExpectedMac =
    normalizedExpectedMac.length > 0 ? normalizedExpectedMac : resolveExpectedMacFromDevice(resolvedDevice);
  const resolvedExpectedFirmwareVersion =
    normalizedExpectedFirmwareVersion.length > 0
      ? normalizedExpectedFirmwareVersion
      : resolveExpectedFirmwareVersionFromDevice(resolvedDevice);

  const normalizedApBaseUrl = String(params.apBaseUrl ?? "").trim();
  const normalizedApMfgUsername = String(params.apMfgUsername ?? "").trim();
  const normalizedApMfgPassword = String(params.apMfgPassword ?? "");
  const apFieldCount = [normalizedApBaseUrl, normalizedApMfgUsername, normalizedApMfgPassword].filter((value) => value.length > 0).length;
  if (apFieldCount !== 0 && apFieldCount !== 3) {
    throw new Error(
      "resolveExecutionParameters failed. apBaseUrl/apMfgUsername/apMfgPassword must be provided together."
    );
  }
  const resolvedApCredentials =
    apFieldCount === 3
      ? {
          apBaseUrl: normalizedApBaseUrl,
          apMfgUsername: normalizedApMfgUsername,
          apMfgPassword: normalizedApMfgPassword
        }
      : resolveApMfgCredentialsFromConfig(loginResult.config);

  return {
    ...params,
    targetDeviceId: resolvedTargetDeviceId,
    expectedMac: resolvedExpectedMac,
    expectedFirmwareVersion: resolvedExpectedFirmwareVersion,
    apBaseUrl: resolvedApCredentials.apBaseUrl,
    apMfgUsername: resolvedApCredentials.apMfgUsername,
    apMfgPassword: resolvedApCredentials.apMfgPassword
  };
}

async function runProductionWorkflowTest(params) {
  const { token, baseUrl, runId, targetDeviceId, pollTimeoutMs, pollIntervalMs } = params;
  const startBody = buildProductionStartBody(params);
  const startResponse = await apiPostJson(baseUrl, token, "/api/workflows/production/start", startBody);
  assertNoSecretLeakage("runProductionWorkflowTest startResponse", startResponse);

  const workflow = startResponse.workflow ?? {};
  const workflowId = String(workflow.workflowId ?? "").trim();
  if (startResponse.result !== "OK" || workflowId.length === 0) {
    throw new Error(
      `runProductionWorkflowTest failed. start result=${String(startResponse.result)} workflowId=${workflowId}`
    );
  }
  if (String(workflow.workflowType ?? "") !== "production") {
    throw new Error(
      `runProductionWorkflowTest failed. workflowType must be production. actual=${String(workflow.workflowType ?? "")}`
    );
  }

  const workflowTerminal = await pollWorkflowUntilTerminal(baseUrl, token, workflowId, pollTimeoutMs, pollIntervalMs);
  assertNoSecretLeakage("runProductionWorkflowTest workflowTerminal", workflowTerminal.workflow);
  const terminalState = String(workflowTerminal.workflow.state ?? "");
  const terminalResult = String(workflowTerminal.workflow.result ?? "");
  const terminalErrorSummary = String(workflowTerminal.workflow.errorSummary ?? "");
  const terminalDetail = String(workflowTerminal.workflow.detail ?? "");
  if (terminalState !== "completed" || terminalResult !== "OK") {
    throw new Error(
      `runProductionWorkflowTest failed. terminalState=${terminalState} terminalResult=${terminalResult} errorSummary=${terminalErrorSummary} detail=${terminalDetail}`
    );
  }

  const mfgLogin = await loginToMaintenanceAp(params.apBaseUrl, params.apMfgUsername, params.apMfgPassword);
  const productionState = await getMaintenanceApProductionState(mfgLogin.apHttpBaseUrl, mfgLogin.token);
  assertNoSecretLeakage("runProductionWorkflowTest productionState", productionState);
  if (String(productionState.runId ?? "") !== runId) {
    throw new Error(
      `runProductionWorkflowTest failed. runId mismatch. expected=${runId} actual=${String(productionState.runId ?? "")}`
    );
  }
  if (String(productionState.state ?? "") !== "precheck_collected") {
    throw new Error(
      `runProductionWorkflowTest failed. production state is not precheck_collected. actual=${String(productionState.state ?? "")}`
    );
  }
  if (String(productionState.targetDeviceId ?? "") !== targetDeviceId) {
    throw new Error(
      `runProductionWorkflowTest failed. targetDeviceId mismatch. expected=${targetDeviceId} actual=${String(
        productionState.targetDeviceId ?? ""
      )}`
    );
  }
  if (String(productionState.observedFirmwareVersion ?? "").trim().length === 0) {
    throw new Error("runProductionWorkflowTest failed. observedFirmwareVersion is empty.");
  }
  if (String(productionState.observedMac ?? "").trim().length === 0) {
    throw new Error("runProductionWorkflowTest failed. observedMac is empty.");
  }

  const stateHistory = workflowTerminal.workflowHistory.map((historyItem) => String(historyItem.state ?? ""));
  // [重要][修正理由] 2026-03-24:
  // workflow が高速完了した場合、履歴が queued->completed のみになることがあり、
  // 中間状態(running/waiting_device/verifying)を必須にすると正常ケースを誤ってNG判定してしまう。
  // ここでは terminal の completed/OK を厳守条件とし、中間状態は観測情報として扱う。
  if (!stateHistory.includes("completed")) {
    throw new Error(
      `runProductionWorkflowTest failed. requiredState=completed is missing. stateHistory=${stateHistory.join("->")}`
    );
  }

  return {
    workflowId,
    workflow: workflowTerminal.workflow,
    workflowHistory: workflowTerminal.workflowHistory,
    productionState
  };
}

async function main() {
  const startedAt = new Date().toISOString();
  const argv = process.argv.slice(2);
  const rawParams = parseArguments(argv);
  const adminLogin = await loginAsAdmin(rawParams.baseUrl);
  const devices = await getAdminDevices(rawParams.baseUrl, adminLogin.token);
  const params = resolveExecutionParameters(rawParams, adminLogin, devices);
  console.log("[7041] Production workflow dry-run 試験を開始します。");
  console.log(
    `  baseUrl=${params.baseUrl} targetDeviceId=${params.targetDeviceId} expectedMac=${params.expectedMac} expectedFirmwareVersion=${params.expectedFirmwareVersion}`
  );
  console.log("  [7041] LocalServer 管理者認証 OK");
  if (!params.skipApPreflight) {
    await verifyApPreflightWithRetry({
      apBaseUrl: params.apBaseUrl,
      apMfgUsername: params.apMfgUsername,
      apMfgPassword: params.apMfgPassword,
      preflightRetryCount: params.preflightRetryCount,
      preflightRetryIntervalMs: params.preflightRetryIntervalMs
    });
    console.log(`  [7041] AP preflight OK apBaseUrl=${params.apBaseUrl}`);
  } else {
    console.log("  [7041] AP preflight は --skipApPreflight 指定により省略");
  }
  if (params.preflightOnly) {
    const preflightOnlyReport = {
      testId: "7041",
      startedAt,
      completedAt: new Date().toISOString(),
      mode: "preflight-only",
      result: "OK",
      targetDeviceId: params.targetDeviceId,
      runId: params.runId,
      apBaseUrl: params.apBaseUrl,
      preflightRetryCount: params.preflightRetryCount,
      preflightRetryIntervalMs: params.preflightRetryIntervalMs,
      detail: "AP preflight completed. workflow execution skipped by --preflightOnly."
    };
    const preflightOnlyReportFilePath = writeJsonReport("test7041", preflightOnlyReport);
    console.log("  [7041] --preflightOnly 指定のため、workflow 実行は行わず終了");
    console.log(`  [7041] preflight reportFilePath=${preflightOnlyReportFilePath}`);
    return;
  }

  const result = await runProductionWorkflowTest({
    ...params,
    token: adminLogin.token
  });

  console.log(
    `  [7041] workflow completed workflowId=${result.workflowId} state=${String(result.workflow.state ?? "")} result=${String(
      result.workflow.result ?? ""
    )}`
  );
  console.log(
    `  [7041] AP production state=${String(result.productionState.state ?? "")} observedFirmwareVersion=${String(
      result.productionState.observedFirmwareVersion ?? ""
    )} observedMac=${String(result.productionState.observedMac ?? "")}`
  );

  const report = {
    testId: "7041",
    startedAt,
    completedAt: new Date().toISOString(),
    mode: "full",
    targetDeviceId: params.targetDeviceId,
    runId: params.runId,
    expectedMac: params.expectedMac,
    expectedFirmwareVersion: params.expectedFirmwareVersion,
    minimumFreeHeapBytes: params.minimumFreeHeapBytes,
    minimumStackMarginBytes: params.minimumStackMarginBytes,
    result: "OK",
    workflow: {
      workflowId: result.workflowId,
      state: result.workflow.state,
      result: result.workflow.result,
      historyLength: result.workflowHistory.length,
      stateHistory: result.workflowHistory.map((historyItem) => ({
        at: historyItem.at,
        state: historyItem.state,
        result: historyItem.result
      }))
    },
    productionState: {
      runId: result.productionState.runId,
      state: result.productionState.state,
      resultLabel: result.productionState.resultLabel,
      observedFirmwareVersion: result.productionState.observedFirmwareVersion,
      observedMac: result.productionState.observedMac,
      measuredFreeHeapBytes: result.productionState.measuredFreeHeapBytes,
      measuredMinStackMarginBytes: result.productionState.measuredMinStackMarginBytes
    },
    detail: "Production workflow dry-run completed and AP production/state confirmed."
  };
  const reportFilePath = writeJsonReport("test7041", report);
  console.log(`[7041] 完了: OK reportFilePath=${reportFilePath}`);
}

main().catch((error) => {
  const errorMessage = String(error?.message ?? error);
  const failureCategory = classifyFailure(errorMessage);
  const failureRecommendation = getFailureRecommendation(failureCategory);
  let diagnosis = extractDiagnosisFromErrorMessage(errorMessage) ?? {
    targetHost: "",
    localIpv4: "",
    routeHint: "",
    arpHint: "",
    nextAction: ""
  };
  if (
    String(diagnosis.targetHost ?? "").trim().length === 0 &&
    String(diagnosis.localIpv4 ?? "").trim().length === 0 &&
    String(diagnosis.routeHint ?? "").trim().length === 0 &&
    String(diagnosis.arpHint ?? "").trim().length === 0 &&
    String(diagnosis.nextAction ?? "").trim().length === 0
  ) {
    try {
      const fallbackParams = parseArguments(process.argv.slice(2));
      const fallbackApBaseUrl = String(fallbackParams.apBaseUrl ?? "").trim().length > 0
        ? fallbackParams.apBaseUrl
        : "http://192.168.4.1";
      const fallbackDiagnosisText = collectPreflightNetworkDiagnosis(fallbackApBaseUrl);
      diagnosis = extractDiagnosisFromErrorMessage(`networkDiagnosis=${fallbackDiagnosisText}`);
    } catch {
      // fallback diagnosis 生成に失敗しても本来の失敗理由を優先して継続する
    }
  }
  const failureReport = {
    testId: "7041",
    startedAt: new Date().toISOString(),
    completedAt: new Date().toISOString(),
    mode: "failed-before-completion",
    result: "NG",
    failureCategory,
    failureRecommendation,
    diagnosis,
    detail: errorMessage
  };
  const failureReportFilePath = writeJsonReport("test7041", failureReport);
  console.error("[7041] 失敗:", errorMessage);
  console.error(`[7041] failureCategory=${failureCategory} recommendation=${failureRecommendation}`);
  console.error(`[7041] failure reportFilePath=${failureReportFilePath}`);
  process.exitCode = 1;
});
