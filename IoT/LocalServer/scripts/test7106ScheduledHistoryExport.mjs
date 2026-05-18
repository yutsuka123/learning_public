/**
 * @file test7106ScheduledHistoryExport.mjs
 * @description 試験仕様書 **7106** に相当する、LocalServer の **プロセス内定期 SQLite 履歴エクスポート**を実機プロセスで検証する。
 *
 * @remarks
 * - [重要] `LOCAL_HISTORY_SCHEDULED_EXPORT_ENABLED=true` と `LOCAL_HISTORY_SCHEDULED_EXPORT_INTERVAL_MS`（>=60000）で `dist/server.js` を子プロセス起動し、`data/history-exports-7106-autotest` に **`scheduled-history-export`** 名の `.jsonl` が **2 件以上**生成されるまで待機する。
 * - [厳守] 事前に `npm run build` 済みであること。`.env` / SecretCore / MQTT は通常起動と同要件（不成立時は子プロセスが終了し本スクリプトは失敗する）。
 * - [制限事項] 既定の最大待機は **180 秒**（`--maxWaitMs` で変更可）。Task Scheduler 本体の検証は `009-0003` 側。
 * - [禁止] 本スクリプトは **検証専用の出力ディレクトリ**を使い、既存の `data/history-exports` のファイル名パターンと混ぜない。
 *
 * 実行例:
 *   cd IoT/LocalServer && npm run test:7106
 *   node scripts/test7106ScheduledHistoryExport.mjs --maxWaitMs 240000
 */

import fs from "fs";
import path from "path";
import os from "os";
import { fileURLToPath } from "url";
import { spawn } from "child_process";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");
const distServerPath = path.join(projectRootPath, "dist", "server.js");
const autotestExportDir = path.join(projectRootPath, "data", "history-exports-7106-autotest");

/**
 * @param {number} ms
 * @returns {Promise<void>}
 */
function sleep(ms) {
  return new Promise((resolvePromise) => setTimeout(resolvePromise, ms));
}

/**
 * @param {string[]} argv
 * @returns {{ maxWaitMs: number }}
 */
function parseArgs(argv) {
  const idx = argv.indexOf("--maxWaitMs");
  const raw =
    idx >= 0 && argv[idx + 1] ? Number.parseInt(String(argv[idx + 1]), 10) : Number.NaN;
  const maxWaitMs = Number.isFinite(raw) && raw >= 90000 ? raw : 180000;
  return { maxWaitMs };
}

/**
 * @returns {number}
 */
function countScheduledExportFiles() {
  if (!fs.existsSync(autotestExportDir)) {
    return 0;
  }
  return fs
    .readdirSync(autotestExportDir)
    .filter(
      (fileName) =>
        fileName.endsWith(".jsonl") &&
        fileName.toLowerCase().includes("scheduled-history-export")
    ).length;
}

/**
 * @param {number} pid
 */
function killProcessTreeWindows(pid) {
  try {
    const killer = spawn("taskkill", ["/PID", String(pid), "/T", "/F"], {
      stdio: "ignore",
      windowsHide: true
    });
    killer.on("error", () => {
      /* 既に終了 */
    });
  } catch {
    /* 続行 */
  }
}

/**
 * @returns {Promise<void>}
 */
async function main() {
  const { maxWaitMs } = parseArgs(process.argv.slice(2));

  if (!fs.existsSync(distServerPath)) {
    throw new Error(
      `test7106 failed. dist/server.js not found. path=${distServerPath} hint="npm run build"`
    );
  }

  if (fs.existsSync(autotestExportDir)) {
    fs.rmSync(autotestExportDir, { recursive: true, force: true });
  }
  fs.mkdirSync(autotestExportDir, { recursive: true });

  const childEnv = {
    ...process.env,
    LOCAL_HISTORY_SCHEDULED_EXPORT_ENABLED: "true",
    LOCAL_HISTORY_SCHEDULED_EXPORT_INTERVAL_MS: "60000",
    LOCAL_HISTORY_EXPORT_DIR: autotestExportDir
  };

  console.info(
    `[7106] spawning LocalServer with scheduled export. exportDir=${autotestExportDir} intervalMs=60000 maxWaitMs=${maxWaitMs}`
  );

  const child = spawn(process.execPath, [distServerPath], {
    cwd: projectRootPath,
    env: childEnv,
    stdio: ["ignore", "pipe", "pipe"]
  });

  let combinedLog = "";
  const appendLog = (chunk) => {
    const text = chunk.toString();
    combinedLog += text;
    if (combinedLog.length > 120000) {
      combinedLog = combinedLog.slice(-120000);
    }
  };
  child.stdout.on("data", appendLog);
  child.stderr.on("data", appendLog);

  /** @type {{ code: number | null; signal: NodeJS.Signals | null } | null} */
  let childExitInfo = null;
  child.on("exit", (code, signal) => {
    childExitInfo = { code: code ?? null, signal: signal ?? null };
  });

  const startAt = Date.now();
  let fileCount = 0;

  try {
    while (Date.now() - startAt < maxWaitMs) {
      if (childExitInfo !== null && fileCount < 2) {
        throw new Error(
          `test7106 failed. child exited before 2 exports. code=${childExitInfo.code} signal=${childExitInfo.signal} logTail=${combinedLog.slice(-1200)}`
        );
      }
      fileCount = countScheduledExportFiles();
      if (fileCount >= 2) {
        break;
      }
      await sleep(2000);
    }
    if (fileCount < 2) {
      throw new Error(
        `test7106 failed. timeout after ${maxWaitMs}ms scheduledFiles=${fileCount} logTail=${combinedLog.slice(-1200)}`
      );
    }
  } finally {
    if (child.pid && !child.killed) {
      killProcessTreeWindows(child.pid);
    }
    await sleep(1500);
  }

  console.info(`[7106] OK scheduled export files found: ${fileCount} (expected>=2)`);
  const reportPath = path.join(
    projectRootPath,
    "logs",
    "test-reports",
    `7106-scheduled-export-${new Date().toISOString().replace(/[:.]/g, "-")}.json`
  );
  const reportDir = path.dirname(reportPath);
  if (!fs.existsSync(reportDir)) {
    fs.mkdirSync(reportDir, { recursive: true });
  }
  fs.writeFileSync(
    reportPath,
    `${JSON.stringify(
      {
        executedAt: new Date().toISOString(),
        maxWaitMs,
        elapsedMs: Date.now() - startAt,
        scheduledExportFileCount: fileCount,
        exportDir: autotestExportDir,
        platform: os.platform()
      },
      null,
      2
    )}\n`,
    "utf-8"
  );
  console.info(`[7106] report: ${reportPath}`);
}

main().catch((error) => {
  console.error(`[7106] NG ${error instanceof Error ? error.stack ?? error.message : String(error)}`);
  process.exitCode = 1;
});
