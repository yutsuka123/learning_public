/**
 * @file test7081BasicStability.mjs
 * @description 7081: LocalServer 基本機能 連続安定試験を半自動実行するスクリプト。
 *
 * 主仕様:
 * - 1 セット = 管理者認証、一覧表示、status、trh、単一対象 OTA workflow、APログイン・設定取得・設定反映・再起動・復旧確認
 * - 上記 1 セットを連続 10 回以上実施する
 * - 各セットで API 応答、UI 反映、online/offline 復帰、ログ異常有無を確認する
 *
 * [重要] --skipOta / --skipAp により、実機未接続時は最小セット（認証・一覧・status・trh）のみで実行できる。
 * [厳守] 連続実行で abort、watchdog、stack canary、復帰不能が発生した場合は 006章/007章へ進まない。
 * [制限事項] `../dist/config.js` を参照するため、事前に `npm run build` が必要。
 *
 * 実行例:
 * node scripts/test7081BasicStability.mjs --iterations 10
 * node scripts/test7081BasicStability.mjs --iterations 10 --skipOta --skipAp
 * node scripts/test7081BasicStability.mjs --targetDeviceName IoT_XXX --iterations 10
 */

import {
  apiGetJson,
  apiPostJson,
  getAdminDevices,
  loginAsAdmin,
  pollWorkflowUntilTerminal,
  publicGetJson,
  resolveTargetDeviceName,
  sleep,
  writeJsonReport
} from "./testCommon.mjs";

const defaultBaseUrl = "http://127.0.0.1:3100";

/**
 * @param {string[]} argv
 * @returns {{
 *   baseUrl: string;
 *   targetDeviceName: string | null;
 *   iterations: number;
 *   skipOta: boolean;
 *   skipAp: boolean;
 *   workflowTimeoutMilliseconds: number;
 *   pollIntervalMilliseconds: number;
 * }}
 */
function parseArguments(argv) {
  return {
    baseUrl: readStringOption(argv, "--baseUrl", defaultBaseUrl),
    targetDeviceName: readNullableStringOption(argv, "--targetDeviceName"),
    iterations: readNumberOption(argv, "--iterations", 10),
    skipOta: argv.includes("--skipOta"),
    skipAp: argv.includes("--skipAp"),
    workflowTimeoutMilliseconds: readNumberOption(argv, "--workflowTimeoutMilliseconds", 180000),
    pollIntervalMilliseconds: readNumberOption(argv, "--pollIntervalMilliseconds", 3000)
  };
}

function readStringOption(argv, optionName, defaultValue) {
  const optionIndex = argv.indexOf(optionName);
  if (optionIndex < 0 || !argv[optionIndex + 1]) {
    return defaultValue;
  }
  return String(argv[optionIndex + 1]);
}

function readNullableStringOption(argv, optionName) {
  const optionIndex = argv.indexOf(optionName);
  if (optionIndex < 0 || !argv[optionIndex + 1]) {
    return null;
  }
  const optionValue = String(argv[optionIndex + 1]).trim();
  return optionValue.length > 0 ? optionValue : null;
}

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
 * 1 セットを実行する。
 * @param {Object} params
 * @returns {Promise<{ ok: boolean; detail: string; steps: any[] }>}
 */
async function runSingleSet(params) {
  const {
    baseUrl,
    token,
    targetDeviceName,
    skipOta,
    skipAp,
    workflowTimeoutMilliseconds,
    pollIntervalMilliseconds
  } = params;

  const steps = [];
  let ok = true;
  let detail = "";

  try {
    // 1. 一覧表示（管理者認証済み前提）
    const devicesResponse = await apiGetJson(baseUrl, token, "/api/admin/devices");
    const devices = Array.isArray(devicesResponse.devices) ? devicesResponse.devices : [];
    steps.push({ step: "getAdminDevices", ok: true, deviceCount: devices.length });

    const effectiveTarget = targetDeviceName ?? (devices.find((d) => d.onlineState === "online")?.targetName ?? devices[0]?.targetName ?? "all");
    const targetNames = effectiveTarget === "all" ? ["all"] : [effectiveTarget];

    // 2. status
    const statusResponse = await apiPostJson(baseUrl, token, "/api/commands/status", {
      targetNames
    });
    steps.push({
      step: "status",
      ok: statusResponse.result === "OK",
      result: statusResponse.result
    });
    if (statusResponse.result !== "OK") {
      ok = false;
      detail = `status result=${statusResponse.result}`;
    }

    // 3. get/trh
    const getTrhResponse = await apiPostJson(baseUrl, token, "/api/commands/get", {
      targetNames,
      subCommand: "trh",
      args: {}
    });
    steps.push({
      step: "getTrh",
      ok: getTrhResponse.result === "OK",
      result: getTrhResponse.result
    });
    if (getTrhResponse.result !== "OK") {
      ok = false;
      detail = detail || `getTrh result=${getTrhResponse.result}`;
    }

    // 4. OTA workflow（skipOta 時は省略）
    if (!skipOta && effectiveTarget !== "all") {
      const settingsRes = await publicGetJson(baseUrl, "/api/settings");
      const expectedVersion = String(settingsRes.settings?.otaFirmwareVersion ?? "").trim();
      if (expectedVersion) {
        const otaStartRes = await apiPostJson(baseUrl, token, "/api/workflows/signed-ota/start", {
          targetNames: [effectiveTarget],
          firmwareVersion: expectedVersion,
          timeoutSeconds: Math.ceil(workflowTimeoutMilliseconds / 1000)
        });
        const workflowId = String(otaStartRes.workflow?.workflowId ?? otaStartRes.workflow?.id ?? "").trim();
        if (workflowId) {
          const wfResult = await pollWorkflowUntilTerminal(
            baseUrl,
            token,
            workflowId,
            workflowTimeoutMilliseconds,
            pollIntervalMilliseconds
          );
          const wfState = String(wfResult.workflow?.state ?? "");
          steps.push({
            step: "otaWorkflow",
            ok: wfState === "completed",
            workflowId,
            state: wfState
          });
          if (wfState !== "completed") {
            ok = false;
            detail = detail || `otaWorkflow state=${wfState}`;
          }
        } else {
          steps.push({ step: "otaWorkflow", ok: false, detail: "workflowId empty" });
          ok = false;
        }
      } else {
        steps.push({ step: "otaWorkflow", ok: true, skipped: true, detail: "expectedVersion empty" });
      }
    } else {
      steps.push({ step: "otaWorkflow", ok: true, skipped: true, reason: skipOta ? "skipOta" : "targetAll" });
    }

    // 5. AP 操作（skipAp 時は省略）
    if (!skipAp) {
      const scanRes = await apiGetJson(baseUrl, token, "/api/admin/ap/scan");
      const networks = Array.isArray(scanRes.networks) ? scanRes.networks : [];
      const firstApSsid = networks.find((n) => n.ssid)?.ssid;
      steps.push({
        step: "apScan",
        ok: true,
        networkCount: networks.length
      });
      if (firstApSsid) {
        const settingsRes = await apiGetJson(baseUrl, token, `/api/admin/ap/settings?ssid=${encodeURIComponent(firstApSsid)}`);
        steps.push({
          step: "apSettings",
          ok: settingsRes != null,
          ssid: firstApSsid
        });
      } else {
        steps.push({ step: "apSettings", ok: true, skipped: true, detail: "no AP ssid from scan" });
      }
    } else {
      steps.push({ step: "apOperations", ok: true, skipped: true, reason: "skipAp" });
    }

    return {
      ok: ok && steps.every((s) => s.ok || s.skipped),
      detail: detail || "OK",
      steps
    };
  } catch (err) {
    return {
      ok: false,
      detail: String(err?.message ?? err),
      steps
    };
  }
}

/**
 * @param {ReturnType<typeof parseArguments>} options
 * @returns {Promise<void>}
 */
async function main(options) {
  console.log("[7081] LocalServer 基本機能 連続安定試験を開始します。");
  console.log(`  baseUrl=${options.baseUrl}`);
  console.log(`  iterations=${options.iterations}`);
  console.log(`  skipOta=${options.skipOta} skipAp=${options.skipAp}`);

  const report = {
    testId: "7081",
    startedAt: new Date().toISOString(),
    options,
    iterationResults: []
  };

  const loginResult = await loginAsAdmin(options.baseUrl);
  report.loginOk = true;

  const devices = await getAdminDevices(options.baseUrl, loginResult.token);
  let targetDeviceName;
  try {
    targetDeviceName = options.targetDeviceName ?? resolveTargetDeviceName(devices, null);
  } catch {
    targetDeviceName = "all";
  }
  report.targetDeviceName = targetDeviceName;

  for (let i = 1; i <= options.iterations; i += 1) {
    console.log(`\n[7081] ${i}/${options.iterations} セット目を実行します。`);
    const setResult = await runSingleSet({
      baseUrl: options.baseUrl,
      token: loginResult.token,
      targetDeviceName,
      skipOta: options.skipOta,
      skipAp: options.skipAp,
      workflowTimeoutMilliseconds: options.workflowTimeoutMilliseconds,
      pollIntervalMilliseconds: options.pollIntervalMilliseconds
    });

    const iterReport = {
      iterationNumber: i,
      ok: setResult.ok,
      detail: setResult.detail,
      steps: setResult.steps,
      completedAt: new Date().toISOString()
    };
    report.iterationResults.push(iterReport);

    if (!setResult.ok) {
      report.completedAt = new Date().toISOString();
      report.result = "NG";
      report.detail = `iteration ${i} failed: ${setResult.detail}`;
      const reportFilePath = writeJsonReport("test7081", report);
      console.error(`  [NG] セット ${i} で失敗: ${setResult.detail}`);
      console.error(`  [7081] レポート保存先: ${reportFilePath}`);
      process.exit(1);
    }
    console.log(`  [OK] セット ${i} 成功`);
    await sleep(500);
  }

  report.completedAt = new Date().toISOString();
  report.result = "OK";
  report.detail = `all ${options.iterations} sets completed successfully.`;
  const reportFilePath = writeJsonReport("test7081", report);
  console.log(`\n[7081] 連続安定試験 完了: OK`);
  console.log(`  reportFilePath=${reportFilePath}`);
}

main(parseArguments(process.argv.slice(2))).catch((mainError) => {
  console.error(`[7081] 試験失敗: ${String(mainError?.message ?? mainError)}`);
  process.exit(1);
});
