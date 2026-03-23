/**
 * @file test7041Pipeline.mjs
 * @description 7041 試験の実行順（preflight -> full -> summary -> gate）を固定するパイプライン。
 *
 * 主仕様:
 * - preflight 実行結果を見て、成功時のみ full を実行する。
 * - summary 実行後に gate 判定まで進み、必要に応じて strict 判定を行う。
 * - full を実行しない場合でも、理由を明示し、比較結果と gate を残して終了する。
 *
 * [重要] AP 未接続期間は preflight 失敗で full を自動停止する。
 * [厳守] preflight 失敗時に full を強行しない。理由: 環境要因と workflow 要因の切り分け順序を固定するため。
 * [推奨] AP 復旧作業中は `pipeline:wait`、復旧後の厳密判定は `pipeline:strict` を使う。理由: 実行順・比較・ゲート判定を 1 コマンドへ集約するため。
 */

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { writeJsonReport } from "./testCommon.mjs";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");
const reportsDirectoryPath = path.join(projectRootPath, "logs", "test-reports");

/**
 * @param {string[]} argv
 * @returns {{
 *   skipPreflight: boolean;
 *   skipFull: boolean;
 *   summaryCount: number;
 *   preflightRetryCount: number;
 *   preflightRetryIntervalMs: number;
 *   runGate: boolean;
 *   strictGate: boolean;
 * }}
 */
function parseArguments(argv) {
  return {
    skipPreflight: argv.includes("--skipPreflight"),
    skipFull: argv.includes("--skipFull"),
    summaryCount: readNumberOption(argv, "--summaryCount", 2),
    preflightRetryCount: readNumberOption(argv, "--preflightRetryCount", 1),
    preflightRetryIntervalMs: readNumberOption(argv, "--preflightRetryIntervalMs", 3000),
    runGate: !argv.includes("--skipGate"),
    strictGate: argv.includes("--strictGate")
  };
}

/**
 * @param {string[]} argv
 * @param {string} optionName
 * @param {number} defaultValue
 * @returns {number}
 */
function readNumberOption(argv, optionName, defaultValue) {
  const optionIndex = argv.indexOf(optionName);
  if (optionIndex < 0 || !argv[optionIndex + 1]) {
    return defaultValue;
  }
  const parsedValue = Number(argv[optionIndex + 1]);
  if (!Number.isFinite(parsedValue) || parsedValue <= 0) {
    throw new Error(`readNumberOption failed. optionName=${optionName} rawValue=${String(argv[optionIndex + 1])}`);
  }
  return Math.floor(parsedValue);
}

/**
 * @param {string} prefixText
 * @param {(fileName: string) => boolean} [fileNamePredicate]
 * @returns {{ fileName: string; filePath: string } | null}
 */
function findLatestReport(prefixText, fileNamePredicate) {
  if (!fs.existsSync(reportsDirectoryPath)) {
    return null;
  }
  const sortedList = fs
    .readdirSync(reportsDirectoryPath, "utf-8")
    .filter(
      (fileName) =>
        fileName.startsWith(prefixText) &&
        fileName.endsWith(".json") &&
        (fileNamePredicate ? fileNamePredicate(fileName) : true)
    )
    .map((fileName) => {
      const filePath = path.join(reportsDirectoryPath, fileName);
      const stats = fs.statSync(filePath);
      return {
        fileName,
        filePath,
        mtimeMs: stats.mtimeMs
      };
    })
    .sort((leftItem, rightItem) => rightItem.mtimeMs - leftItem.mtimeMs);
  if (sortedList.length === 0) {
    return null;
  }
  return {
    fileName: sortedList[0].fileName,
    filePath: sortedList[0].filePath
  };
}

/**
 * @param {string} prefixText
 * @param {string} previousLatestFileName
 * @param {(fileName: string) => boolean} [fileNamePredicate]
 * @returns {{ fileName: string; report: any } | null}
 */
function readLatestReportIfChanged(prefixText, previousLatestFileName, fileNamePredicate) {
  const latest = findLatestReport(prefixText, fileNamePredicate);
  if (latest == null || latest.fileName === previousLatestFileName) {
    return null;
  }
  try {
    const report = JSON.parse(fs.readFileSync(latest.filePath, "utf-8"));
    return {
      fileName: latest.fileName,
      report
    };
  } catch (error) {
    return {
      fileName: latest.fileName,
      report: {
        parseError: String(error?.message ?? error)
      }
    };
  }
}

/**
 * 7041 実行レポート（test7041-YYYY...）のみを許可する。
 * @param {string} fileName
 * @returns {boolean}
 */
function isExecutionReportFileName(fileName) {
  return /^test7041-\d{4}-\d{2}-\d{2}T/.test(fileName);
}

/**
 * @param {string} titleText
 * @param {string} commandText
 * @param {string[]} argumentList
 * @returns {{ ok: boolean; exitCode: number }}
 */
function runStep(titleText, commandText, argumentList) {
  console.log(`\n[7041-pipeline] ${titleText}`);
  console.log(`[7041-pipeline] command=${commandText} ${argumentList.join(" ")}`);
  const result = spawnSync(commandText, argumentList, {
    cwd: projectRootPath,
    shell: true,
    stdio: "inherit"
  });
  const exitCode = Number(result.status ?? 1);
  const ok = exitCode === 0;
  console.log(`[7041-pipeline] stepResult=${ok ? "OK" : "NG"} exitCode=${exitCode}`);
  return {
    ok,
    exitCode,
    commandText,
    argumentList
  };
}

async function main() {
  const startedAt = new Date().toISOString();
  const options = parseArguments(process.argv.slice(2));
  console.log("[7041-pipeline] start");
  console.log(
    `[7041-pipeline] options skipPreflight=${options.skipPreflight} skipFull=${options.skipFull} summaryCount=${options.summaryCount} preflightRetryCount=${options.preflightRetryCount} preflightRetryIntervalMs=${options.preflightRetryIntervalMs} runGate=${options.runGate} strictGate=${options.strictGate}`
  );

  let preflightOk = true;
  let preflightResult = null;
  if (!options.skipPreflight) {
    const preflightBeforeFileName = findLatestReport("test7041-", isExecutionReportFileName)?.fileName ?? "";
    preflightResult = runStep("preflight", "node", [
      "scripts/test7041ProductionDryRunE2E.mjs",
      "--preflightOnly",
      "--preflightRetryCount",
      String(options.preflightRetryCount),
      "--preflightRetryIntervalMs",
      String(options.preflightRetryIntervalMs)
    ]);
    const preflightLatestReport = readLatestReportIfChanged(
      "test7041-",
      preflightBeforeFileName,
      isExecutionReportFileName
    );
    if (preflightLatestReport != null) {
      preflightResult.latestReportFileName = preflightLatestReport.fileName;
      preflightResult.report = preflightLatestReport.report;
    }
    preflightOk = preflightResult.ok;
  } else {
    console.log("[7041-pipeline] preflight skipped by --skipPreflight");
  }

  let fullExecuted = false;
  let fullOk = false;
  let fullResult = null;
  if (!options.skipFull && preflightOk) {
    const fullBeforeFileName = findLatestReport("test7041-", isExecutionReportFileName)?.fileName ?? "";
    fullResult = runStep("full", "npm", ["run", "test:7041"]);
    const fullLatestReport = readLatestReportIfChanged("test7041-", fullBeforeFileName, isExecutionReportFileName);
    if (fullLatestReport != null) {
      fullResult.latestReportFileName = fullLatestReport.fileName;
      fullResult.report = fullLatestReport.report;
    }
    fullExecuted = true;
    fullOk = fullResult.ok;
  } else if (options.skipFull) {
    console.log("[7041-pipeline] full skipped by --skipFull");
  } else {
    console.log("[7041-pipeline] full skipped because preflight failed");
  }

  const summaryBeforeFileName = findLatestReport("test7041-summary-")?.fileName ?? "";
  const summaryResult = runStep("summary", "node", [
    "scripts/test7041ReportSummary.mjs",
    "--count",
    String(options.summaryCount)
  ]);
  const summaryLatestReport = readLatestReportIfChanged("test7041-summary-", summaryBeforeFileName);
  if (summaryLatestReport != null) {
    summaryResult.latestReportFileName = summaryLatestReport.fileName;
    summaryResult.report = summaryLatestReport.report;
  }

  let gateResult = null;
  if (options.runGate) {
    const gateBeforeFileName = findLatestReport("test7041-gate-")?.fileName ?? "";
    const gateArgumentList = ["scripts/test7041GateCheck.mjs"];
    if (options.strictGate) {
      gateArgumentList.push("--strict");
    }
    gateResult = runStep("gate", "node", gateArgumentList);
    const gateLatestReport = readLatestReportIfChanged("test7041-gate-", gateBeforeFileName);
    if (gateLatestReport != null) {
      gateResult.latestReportFileName = gateLatestReport.fileName;
      gateResult.report = gateLatestReport.report;
    }
  } else {
    console.log("[7041-pipeline] gate skipped by --skipGate");
  }

  const gateOk = gateResult == null ? true : gateResult.ok;
  const pipelineOk = preflightOk && (!fullExecuted || fullOk) && summaryResult.ok && gateOk;
  const pipelineRecommendation = preflightOk
    ? "preflight OK. keep running full test and compare summary."
    : "preflight NG. fix AP reachability/auth before full execution.";
  const pipelineReport = {
    testId: "7041-pipeline",
    startedAt,
    completedAt: new Date().toISOString(),
    result: pipelineOk ? "OK" : "NG",
    options,
    preflight: preflightResult,
    full: {
      executed: fullExecuted,
      ok: fullOk,
      detail: fullResult
    },
    summary: summaryResult,
    gate: gateResult,
    pipelineRecommendation
  };
  const pipelineReportFilePath = writeJsonReport("test7041-pipeline", pipelineReport);
  console.log(
    `[7041-pipeline] completed pipelineOk=${pipelineOk} preflightOk=${preflightOk} fullExecuted=${fullExecuted} fullOk=${fullOk} summaryOk=${summaryResult.ok} gateOk=${gateOk}`
  );
  console.log(`[7041-pipeline] pipelineReportFilePath=${pipelineReportFilePath}`);
  if (!pipelineOk) {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(`[7041-pipeline] failed: ${String(error?.message ?? error)}`);
  process.exitCode = 1;
});
