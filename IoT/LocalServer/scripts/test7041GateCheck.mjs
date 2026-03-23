/**
 * @file test7041GateCheck.mjs
 * @description 7041 の最新レポートから実行可否ゲートを判定するスクリプト。
 *
 * 主仕様:
 * - `test7041-pipeline-*.json` と `test7041-summary-*.json` の最新ファイルを参照する。
 * - preflight / full / summary の状態を読み取り、現在の進行可否を判定する。
 * - 判定結果を標準出力し、`test7041-gate-*.json` として保存する。
 *
 * [重要] AP未接続期間でも、次に何を実施すべきかを機械的に決める。
 * [厳守] `--strict` 指定時は `READY_FOR_FULL` または `FULL_COMPLETED` 以外で終了コード 1 を返す。
 * [制限事項] 直近レポートに依存するため、レポート未生成時は先に `test:7041:pipeline` を実行する。
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createTimestampText } from "./testCommon.mjs";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");
const reportsDirectoryPath = path.join(projectRootPath, "logs", "test-reports");

/**
 * @param {string[]} argv
 * @returns {{ strict: boolean }}
 */
function parseArguments(argv) {
  return {
    strict: argv.includes("--strict")
  };
}

/**
 * @param {string} prefixText
 * @returns {{ filePath: string; fileName: string; mtimeMs: number } | null}
 */
function findLatestReportFile(prefixText) {
  if (!fs.existsSync(reportsDirectoryPath)) {
    return null;
  }
  const candidateList = fs
    .readdirSync(reportsDirectoryPath, "utf-8")
    .filter((fileName) => fileName.startsWith(prefixText) && fileName.endsWith(".json"))
    .map((fileName) => {
      const filePath = path.join(reportsDirectoryPath, fileName);
      const stats = fs.statSync(filePath);
      return {
        filePath,
        fileName,
        mtimeMs: stats.mtimeMs
      };
    })
    .sort((leftItem, rightItem) => rightItem.mtimeMs - leftItem.mtimeMs);
  return candidateList[0] ?? null;
}

/**
 * @param {string} filePath
 * @returns {any}
 */
function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf-8"));
}

/**
 * @param {any} pipelineReport
 * @returns {{ gateStatus: string; recommendation: string; reason: string }}
 */
function evaluateGateStatus(pipelineReport) {
  const preflightOk = Boolean(pipelineReport?.preflight?.ok);
  const fullExecuted = Boolean(pipelineReport?.full?.executed);
  const fullOk = Boolean(pipelineReport?.full?.ok);
  const summaryOk = Boolean(pipelineReport?.summary?.ok);
  const fullFailureCategory = String(
    pipelineReport?.full?.detail?.report?.failureCategory ?? pipelineReport?.full?.detail?.failureCategory ?? ""
  );
  const preflightFailureCategory = String(
    pipelineReport?.preflight?.report?.failureCategory ?? pipelineReport?.preflight?.failureCategory ?? ""
  );

  if (preflightOk && !fullExecuted) {
    return {
      gateStatus: "READY_FOR_FULL",
      recommendation: "run npm run test:7041",
      reason: "preflight succeeded and full has not been executed yet."
    };
  }
  if (preflightOk && fullExecuted && fullOk && summaryOk) {
    return {
      gateStatus: "FULL_COMPLETED",
      recommendation: "update test records and proceed to next planned verification step.",
      reason: "preflight/full/summary are all successful."
    };
  }
  if (!preflightOk) {
    if (preflightFailureCategory === "ap_auth_or_permission") {
      return {
        gateStatus: "BLOCKED_AP_AUTH",
        recommendation: "fix AP credentials/role and rerun npm run test:7041:pipeline",
        reason: "preflight failed due to AP authentication or permission issue."
      };
    }
    return {
      gateStatus: "BLOCKED_AP_REACHABILITY",
      recommendation: "connect PC to ESP32 AP and rerun npm run test:7041:pipeline:wait",
      reason: "preflight failed due to AP reachability issue."
    };
  }
  if (fullExecuted && !fullOk) {
    if (fullFailureCategory === "workflow_failed") {
      return {
        gateStatus: "BLOCKED_WORKFLOW",
        recommendation: "inspect workflow errorSummary/detail and SecretCore logs, then rerun pipeline.",
        reason: "full execution failed in workflow stage."
      };
    }
    return {
      gateStatus: "BLOCKED_AFTER_FULL",
      recommendation: "inspect latest test7041 report and rerun pipeline after corrective action.",
      reason: "full execution failed with non-workflow category."
    };
  }
  return {
    gateStatus: "UNKNOWN",
    recommendation: "rerun npm run test:7041:pipeline and inspect generated reports.",
    reason: "unable to classify current state from latest reports."
  };
}

/**
 * @param {any} gateReport
 * @returns {string}
 */
function writeGateReport(gateReport) {
  if (!fs.existsSync(reportsDirectoryPath)) {
    fs.mkdirSync(reportsDirectoryPath, { recursive: true });
  }
  const filePath = path.join(reportsDirectoryPath, `test7041-gate-${createTimestampText()}.json`);
  fs.writeFileSync(filePath, `${JSON.stringify(gateReport, null, 2)}\n`, "utf-8");
  return filePath;
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const latestPipelineFile = findLatestReportFile("test7041-pipeline-");
  const latestSummaryFile = findLatestReportFile("test7041-summary-");
  if (latestPipelineFile == null) {
    throw new Error("main failed. latest pipeline report is not found. commandHint=npm run test:7041:pipeline");
  }
  const pipelineReport = readJson(latestPipelineFile.filePath);
  const summaryReport = latestSummaryFile == null ? null : readJson(latestSummaryFile.filePath);
  const gate = evaluateGateStatus(pipelineReport);
  const gateReport = {
    testId: "7041-gate",
    generatedAt: new Date().toISOString(),
    strictMode: options.strict,
    source: {
      pipelineFileName: latestPipelineFile.fileName,
      summaryFileName: latestSummaryFile?.fileName ?? ""
    },
    gate
  };
  if (summaryReport != null) {
    gateReport.summarySnapshot = {
      sourceReportCount: Number(summaryReport.sourceReportCount ?? 0),
      comparisonKeys: Object.keys(summaryReport.comparison ?? {})
    };
  }
  const gateReportFilePath = writeGateReport(gateReport);
  console.log(`[7041-gate] gateStatus=${gate.gateStatus}`);
  console.log(`[7041-gate] reason=${gate.reason}`);
  console.log(`[7041-gate] recommendation=${gate.recommendation}`);
  console.log(`[7041-gate] gateReportFilePath=${gateReportFilePath}`);

  if (options.strict && gate.gateStatus !== "READY_FOR_FULL" && gate.gateStatus !== "FULL_COMPLETED") {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(`[7041-gate] failed: ${String(error?.message ?? error)}`);
  process.exitCode = 1;
});
