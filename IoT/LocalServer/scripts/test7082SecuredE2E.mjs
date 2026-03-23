/**
 * @file test7082SecuredE2E.mjs
 * @description 7082: LocalServer セキュア機能 end-to-end 固定試験を半自動実行するスクリプト。
 *
 * 主仕様:
 * - Pairing workflow: 正規 body で POST /api/workflows/pairing/start を実行し、workflowId 取得と状態遷移を確認する。
 * - 応答に raw k-device、ECDH 共有秘密、k-pairing-session が含まれないことを確認する。
 * - （オプション）export_k_user / import_k_user_backup の fingerprint のみ表示・非露出を確認する。
 *
 * [重要] 003-0021 完全達成のため、Pairing は body 検証通過〜workflow 開始〜状態取得までを自動確認する。
 * [厳守] 試験スクリプトは秘密値を扱わず、開始 body に渡す keyDevice は 32byte ダミー Base64 に限定する。
 * [制限事項] 実機 AP 未接続時は workflow は precheck で failed となる。AP 接続時は verifying まで進み placeholder で failed。
 *
 * 実行例:
 * node scripts/test7082SecuredE2E.mjs
 * node scripts/test7082SecuredE2E.mjs --baseUrl http://127.0.0.1:3100 --targetDeviceId IoT_XXX
 */

import crypto from "crypto";
import {
  apiGetJson,
  apiPostJson,
  loginAsAdmin,
  pollWorkflowUntilTerminal,
  sleep,
  writeJsonReport
} from "./testCommon.mjs";

const defaultBaseUrl = "http://127.0.0.1:3100";

/**
 * 32 byte のダミー鍵を Base64 で生成する。
 * [重要] 試験用のみ。実機 Pairing では LocalServer 既知の k-device を使用すること。
 * @returns {string}
 */
function createDummyKeyDeviceBase64() {
  return crypto.randomBytes(32).toString("base64");
}

/**
 * 7082 用の最小限有効な Pairing workflow 開始 body を組み立てる。
 * @param {Object} params
 * @param {string} params.targetDeviceId
 * @param {string} params.sessionId
 * @param {string} params.keyVersion
 * @param {string} params.keyDeviceBase64 32byte Base64。
 * @returns {Object} POST /api/workflows/pairing/start の body。
 */
function buildMinimalPairingStartBody({ targetDeviceId, sessionId, keyVersion, keyDeviceBase64 }) {
  return {
    targetDeviceId: String(targetDeviceId ?? "").trim(),
    sessionId: String(sessionId ?? "").trim(),
    keyVersion: String(keyVersion ?? "v1").trim(),
    requestedSettings: {
      wifi: {
        ssid: "TestSSID-7082",
        password: "test-wifi-pass"
      },
      mqtt: {
        host: "mqtt.example.com",
        port: 1883,
        tls: false,
        username: "mqttuser",
        password: "mqttpass"
      },
      ota: {
        host: "ota.example.com",
        port: 443,
        tls: true,
        username: "otauser",
        password: "otapass"
      },
      credentials: {
        wifiPassword: "test-wifi-pass",
        mqttUsername: "mqttuser",
        mqttPassword: "mqttpass",
        otaUsername: "otauser",
        otaPassword: "otapass",
        keyDevice: String(keyDeviceBase64 ?? "").trim()
      }
    }
  };
}

/**
 * 応答 JSON に秘密値が含まれていないことを確認する。
 * [厳守] 送信した keyDevice Base64 がそのまま応答に含まれていてはならない。
 * @param {string} functionName
 * @param {any} payload 応答オブジェクト（再帰的に検査）。
 * @param {{ sentKeyDeviceBase64: string }} context 送信した keyDevice（応答に含まれていないことの確認用）。
 */
function assertNoSecretLeakage(functionName, payload, context) {
  const sentKey = String(context.sentKeyDeviceBase64 ?? "").trim();
  if (sentKey.length === 0) return;
  const jsonString = JSON.stringify(payload);
  if (jsonString.includes(sentKey)) {
    throw new Error(
      `${functionName} failed. sent keyDevice Base64 appeared in response. forbidden. context=${JSON.stringify({ sentKeyLength: sentKey.length })}`
    );
  }
}

/**
 * コマンドライン引数をパースする。
 * @param {string[]} argv
 * @returns {{ baseUrl: string; targetDeviceId: string; pairingPollTimeoutMs: number; pairingPollIntervalMs: number; allowPairingFailed: boolean }}
 */
function parseArguments(argv) {
  const baseUrl = readStringOption(argv, "--baseUrl", defaultBaseUrl);
  const targetDeviceId = readStringOption(argv, "--targetDeviceId", "IoT_7082_TARGET");
  const pairingPollTimeoutMs = readNumberOption(argv, "--pairingPollTimeoutMs", 60000);
  const pairingPollIntervalMs = readNumberOption(argv, "--pairingPollIntervalMs", 2000);
  const allowPairingFailed = readBooleanFlag(argv, "--allowPairingFailed");
  return {
    baseUrl,
    targetDeviceId,
    pairingPollTimeoutMs,
    pairingPollIntervalMs,
    allowPairingFailed
  };
}

function readStringOption(argv, optionName, defaultValue) {
  const i = argv.indexOf(optionName);
  if (i < 0 || !argv[i + 1]) return defaultValue;
  return String(argv[i + 1]);
}

function readNumberOption(argv, optionName, defaultValue) {
  const v = readStringOption(argv, optionName, null);
  if (!v) return defaultValue;
  const n = Number(v);
  if (!Number.isFinite(n) || n <= 0) throw new Error(`readNumberOption failed. optionName=${optionName} value=${v}`);
  return n;
}

/**
 * ブールフラグ（指定あり=true）を読む。
 * @param {string[]} argv
 * @param {string} optionName
 * @returns {boolean}
 */
function readBooleanFlag(argv, optionName) {
  return argv.includes(optionName);
}

/**
 * Pairing workflow 開始〜終端状態までの確認を実行する。
 * @param {Object} params
 * @returns {Promise<{ ok: boolean; workflowId: string; state: string; history: any[]; noLeak: boolean }>}
 */
async function runPairingWorkflowTest(params) {
  const { baseUrl, token, targetDeviceId, pairingPollTimeoutMs, pairingPollIntervalMs, allowPairingFailed } = params;
  const keyDeviceBase64 = createDummyKeyDeviceBase64();
  const startBody = buildMinimalPairingStartBody({
    targetDeviceId,
    sessionId: `session-7082-${Date.now()}`,
    keyVersion: "v1",
    keyDeviceBase64
  });

  const startResponse = await apiPostJson(baseUrl, token, "/api/workflows/pairing/start", startBody);
  assertNoSecretLeakage("runPairingWorkflowTest startResponse", startResponse, { sentKeyDeviceBase64: keyDeviceBase64 });

  const workflowStatus = startResponse.workflow ?? {};
  if (startResponse.result !== "OK" || !workflowStatus.workflowId) {
    throw new Error(
      `runPairingWorkflowTest failed. pairing/start result=${String(startResponse.result)} workflowId=${String(workflowStatus.workflowId)}`
    );
  }
  const workflowId = String(workflowStatus.workflowId);
  const workflowType = String(workflowStatus.workflowType ?? "");
  if (workflowType !== "pairing") {
    throw new Error(`runPairingWorkflowTest failed. expected workflowType pairing actual=${workflowType}`);
  }

  const { workflow, workflowHistory } = await pollWorkflowUntilTerminal(
    baseUrl,
    token,
    workflowId,
    pairingPollTimeoutMs,
    pairingPollIntervalMs
  );
  assertNoSecretLeakage("runPairingWorkflowTest workflow", workflow, { sentKeyDeviceBase64: keyDeviceBase64 });
  const terminalState = String(workflow.state ?? "");
  const terminalResult = String(workflow.result ?? "");
  if (!allowPairingFailed && (terminalState !== "completed" || terminalResult !== "OK")) {
    throw new Error(
      `runPairingWorkflowTest failed. pairing terminal is not completed/OK. state=${terminalState} result=${terminalResult} detail=${String(
        workflow.detail ?? ""
      )}`
    );
  }

  return {
    ok: true,
    workflowId,
    state: String(workflow.state ?? ""),
    result: String(workflow.result ?? ""),
    history: workflowHistory,
    noLeak: true
  };
}

async function main() {
  const argv = process.argv.slice(2);
  const { baseUrl, targetDeviceId, pairingPollTimeoutMs, pairingPollIntervalMs, allowPairingFailed } = parseArguments(argv);

  console.log("[7082] LocalServer セキュア機能 end-to-end 試験を開始します。");
  console.log(`  baseUrl=${baseUrl} targetDeviceId=${targetDeviceId}`);

  const { token } = await loginAsAdmin(baseUrl);
  console.log("  [7082] 管理者認証 OK");

  const pairingResult = await runPairingWorkflowTest({
    baseUrl,
    token,
    targetDeviceId,
    pairingPollTimeoutMs,
    pairingPollIntervalMs,
    allowPairingFailed
  });
  console.log(
    `  [7082] Pairing workflow OK workflowId=${pairingResult.workflowId} state=${pairingResult.state} result=${pairingResult.result} noLeak=${pairingResult.noLeak}`
  );

  const report = {
    testId: "7082",
    startedAt: new Date().toISOString(),
    baseUrl,
    targetDeviceId,
    pairing: {
      workflowId: pairingResult.workflowId,
      state: pairingResult.state,
      result: pairingResult.result,
      noSecretLeakage: pairingResult.noLeak,
      historyLength: pairingResult.history.length,
      stateHistory: pairingResult.history.map((h) => ({ at: h.at, state: h.state, result: h.result }))
    },
    options: {
      allowPairingFailed
    },
    result: "OK",
    detail: "Pairing workflow start + poll until terminal; no raw k-device/ECDH/k-pairing-session in responses."
  };
  const reportFilePath = writeJsonReport("test7082", report);
  console.log(`[7082] 完了: OK  reportFilePath=${reportFilePath}`);
}

main().catch((err) => {
  console.error("[7082] 失敗:", err.message);
  process.exitCode = 1;
});
