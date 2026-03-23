/**
 * @file test7040KeyRotationE2E.mjs
 * @description 7040: `runKeyRotationSession()` の end-to-end 完了判定試験を半自動実行するスクリプト。
 *
 * 主仕様:
 * - `POST /api/workflows/key-rotation/start` を実行し、workflow が `completed/OK` へ到達することを確認する。
 * - 実行後に AP `GET /api/pairing/state` を参照し、`savedCurrentKeyVersion` と `previousKeyState=grace|none` を確認する。
 * - 応答に raw `k-device`、ECDH 共有秘密、`k-pairing-session` が含まれないことを確認する。
 *
 * [重要] 本試験は実機の `k-user` / `k-device` 系統を更新する。実行前に対象機と復旧手順を確認すること。
 * [厳守] `wifiSsid` / `wifiPass` は必須指定とし、勝手な既定値で実行しない。
 * [制限事項] AP 接続済みかつ `LocalServer` / `SecretCore` / ESP32 の最新版反映が前提。
 */

import {
  apiPostJson,
  apiGetJson,
  getMaintenanceApPairingState,
  loginAsAdmin,
  loginAsMaintenanceApAdmin,
  pollWorkflowUntilTerminal,
  writeJsonReport
} from "./testCommon.mjs";

const defaultBaseUrl = "http://127.0.0.1:3100";

function parseArguments(argv) {
  const baseUrl = readStringOption(argv, "--baseUrl", defaultBaseUrl);
  const targetDeviceId = readRequiredStringOption(argv, "--targetDeviceId");
  const wifiSsid = readRequiredStringOption(argv, "--wifiSsid");
  const wifiPass = readRequiredStringOption(argv, "--wifiPass");
  const keyVersion = readStringOption(argv, "--keyVersion", `v-rot-${Date.now()}`);
  const pollTimeoutMs = readNumberOption(argv, "--pollTimeoutMs", 90000);
  const pollIntervalMs = readNumberOption(argv, "--pollIntervalMs", 2000);
  return {
    baseUrl,
    targetDeviceId,
    wifiSsid,
    wifiPass,
    keyVersion,
    pollTimeoutMs,
    pollIntervalMs
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

function assertNoSecretLeakage(functionName, payload) {
  const payloadJson = JSON.stringify(payload ?? {});
  const forbiddenHints = ["keyDevice\":\"", "sharedSecret", "k-pairing-session", "transportSessionKey"];
  const foundHint = forbiddenHints.find((hint) => payloadJson.includes(hint));
  if (foundHint) {
    throw new Error(`assertNoSecretLeakage failed. functionName=${functionName} foundHint=${foundHint}`);
  }
}

function buildKeyRotationStartBody(params, config, currentNetworkSettings) {
  const currentSettings = currentNetworkSettings ?? {};
  return {
    targetDeviceId: params.targetDeviceId,
    sessionId: `session-7040-${Date.now()}`,
    keyVersion: params.keyVersion,
    ssid: `test-7040-${params.targetDeviceId}`,
    wifiSsid: params.wifiSsid,
    wifiPass: params.wifiPass,
    // [重要] AP 側の secure-bundle apply は OTA/MQTT 認証情報を空文字で受け付けない。
    // そのため既存保存値を読み出し、7040 では Wi-Fi / keyVersion だけを差し替えて他設定を保持する。
    mqttUrl: String(currentSettings.mqttUrl ?? config.mqttHostName ?? ""),
    mqttUrlName: String(currentSettings.mqttUrlName ?? config.mqttHostName ?? ""),
    mqttUser: String(currentSettings.mqttUser ?? config.mqttUsername ?? ""),
    mqttPass: String(currentSettings.mqttPass ?? config.mqttPassword ?? ""),
    mqttPort: Number(currentSettings.mqttPort ?? config.mqttPort ?? 8883),
    mqttTls: Boolean(currentSettings.mqttTls ?? (config.mqttProtocol === "mqtts")),
    otaUrl: String(currentSettings.otaUrl ?? config.otaPublicHostName ?? ""),
    otaUrlName: String(currentSettings.otaUrlName ?? config.otaPublicHostName ?? ""),
    otaUser: String(currentSettings.otaUser ?? ""),
    otaPass: String(currentSettings.otaPass ?? ""),
    otaPort: Number(currentSettings.otaPort ?? config.otaHttpsPort ?? 443),
    otaTls: true,
    serverUrl: config.otaPublicHostName,
    serverUrlName: config.otaPublicHostName,
    serverPort: config.otaHttpsPort,
    serverTls: true,
    timeServerUrl: String(currentSettings.timeServerUrl ?? ""),
    timeServerPort: Number(currentSettings.timeServerPort ?? 123),
    timeServerTls: Boolean(currentSettings.timeServerTls ?? false)
  };
}

async function runKeyRotationWorkflowTest(params) {
  const { token, baseUrl, config, targetDeviceId, keyVersion, pollTimeoutMs, pollIntervalMs } = params;
  const currentApLogin = await loginAsMaintenanceApAdmin();
  const currentNetworkSettings = await apiGetJson(currentApLogin.apHttpBaseUrl, currentApLogin.token, "/api/settings/network");
  const startBody = buildKeyRotationStartBody(params, config, currentNetworkSettings);
  const startResponse = await apiPostJson(baseUrl, token, "/api/workflows/key-rotation/start", startBody);
  assertNoSecretLeakage("runKeyRotationWorkflowTest startResponse", startResponse);

  const workflow = startResponse.workflow ?? {};
  const workflowId = String(workflow.workflowId ?? "").trim();
  if (startResponse.result !== "OK" || workflowId.length === 0) {
    throw new Error(
      `runKeyRotationWorkflowTest failed. start result=${String(startResponse.result)} workflowId=${workflowId}`
    );
  }
  if (String(workflow.workflowType ?? "") !== "key-rotation") {
    throw new Error(
      `runKeyRotationWorkflowTest failed. workflowType must be key-rotation. actual=${String(workflow.workflowType ?? "")}`
    );
  }

  const workflowTerminal = await pollWorkflowUntilTerminal(baseUrl, token, workflowId, pollTimeoutMs, pollIntervalMs);
  assertNoSecretLeakage("runKeyRotationWorkflowTest workflowTerminal", workflowTerminal.workflow);
  const terminalState = String(workflowTerminal.workflow.state ?? "");
  const terminalResult = String(workflowTerminal.workflow.result ?? "");
  if (terminalState !== "completed" || terminalResult !== "OK") {
    throw new Error(
      `runKeyRotationWorkflowTest failed. terminalState=${terminalState} terminalResult=${terminalResult} detail=${String(
        workflowTerminal.workflow.detail ?? ""
      )}`
    );
  }

  const finalApLogin = await loginAsMaintenanceApAdmin();
  const pairingState = await getMaintenanceApPairingState(finalApLogin.apHttpBaseUrl, finalApLogin.token);
  if (String(pairingState.state ?? "") !== "applied") {
    throw new Error(
      `runKeyRotationWorkflowTest failed. AP state is not applied. state=${String(pairingState.state ?? "")}`
    );
  }
  if (String(pairingState.savedCurrentKeyVersion ?? "") !== keyVersion) {
    throw new Error(
      `runKeyRotationWorkflowTest failed. savedCurrentKeyVersion mismatch. expected=${keyVersion} actual=${String(
        pairingState.savedCurrentKeyVersion ?? ""
      )}`
    );
  }
  if (!["grace", "none"].includes(String(pairingState.previousKeyState ?? ""))) {
    throw new Error(
      `runKeyRotationWorkflowTest failed. previousKeyState must be grace|none. actual=${String(
        pairingState.previousKeyState ?? ""
      )}`
    );
  }

  return {
    workflowId,
    workflow: workflowTerminal.workflow,
    workflowHistory: workflowTerminal.workflowHistory,
    pairingState
  };
}

async function main() {
  const argv = process.argv.slice(2);
  const params = parseArguments(argv);
  console.log("[7040] KeyRotation workflow end-to-end 試験を開始します。");
  console.log(`  baseUrl=${params.baseUrl} targetDeviceId=${params.targetDeviceId} keyVersion=${params.keyVersion}`);

  const adminLogin = await loginAsAdmin(params.baseUrl);
  console.log("  [7040] LocalServer 管理者認証 OK");

  const result = await runKeyRotationWorkflowTest({
    ...params,
    token: adminLogin.token,
    config: adminLogin.config
  });

  console.log(
    `  [7040] workflow completed workflowId=${result.workflowId} state=${String(result.workflow.state ?? "")} result=${String(
      result.workflow.result ?? ""
    )}`
  );
  console.log(
    `  [7040] AP pairing state=${String(result.pairingState.state ?? "")} savedCurrentKeyVersion=${String(
      result.pairingState.savedCurrentKeyVersion ?? ""
    )} previousKeyState=${String(result.pairingState.previousKeyState ?? "")}`
  );

  const report = {
    testId: "7040",
    startedAt: new Date().toISOString(),
    targetDeviceId: params.targetDeviceId,
    keyVersion: params.keyVersion,
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
    pairingState: {
      state: result.pairingState.state,
      savedCurrentKeyVersion: result.pairingState.savedCurrentKeyVersion,
      previousKeyState: result.pairingState.previousKeyState,
      keyDevicePresent: result.pairingState.keyDevicePresent
    },
    detail: "KeyRotation workflow completed and AP pairing/state confirmed."
  };
  const reportFilePath = writeJsonReport("test7040", report);
  console.log(`[7040] 完了: OK reportFilePath=${reportFilePath}`);
}

main().catch((error) => {
  console.error("[7040] 失敗:", error.message);
  process.exitCode = 1;
});
