/**
 * @file test7106ScheduledExportDisable.mjs
 * @description 試験仕様書 **7106** 手順 **3**（`LOCAL_HISTORY_SCHEDULED_EXPORT_ENABLED=false` で再起動し新規定期出力が止まる）を自動検証する。
 *
 * @remarks
 * - [重要] **専用ディレクトリ** `data/history-exports-7106-disable-autotest/` のみを参照する。本番 `data/history-exports/` には触れない。
 * - [厳守] フェーズ1で子プロセスを **ENABLED=true**・`INTERVAL_MS=65000` で起動し、**1 件以上**の `scheduled-history-export` `.jsonl` を確認してから **`taskkill`**。フェーズ2は **ENABLED=false** で **90s** 待機し、**新規**の定期ファイルが増えないことを確認する。
 * - [制限] 総実行時間は約 **3 分**（フェーズ1 起動＋最大 ~130s 待機 + フェーズ2 90s）。
 *
 * 実行例:
 *   cd IoT/LocalServer && npm run build && npm run test:7106:disable
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { spawn } from "child_process";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");
const distServerPath = path.join(projectRootPath, "dist", "server.js");
const autotestExportDir = path.join(projectRootPath, "data", "history-exports-7106-disable-autotest");
const isolateDbPath = path.join(projectRootPath, "data", "localHistory-7106-disable-temp.db");

/**
 * @returns {string[]}
 */
function listScheduledExportFiles() {
  if (!fs.existsSync(autotestExportDir)) {
    return [];
  }
  return fs
    .readdirSync(autotestExportDir)
    .filter(
      (fileName) =>
        fileName.endsWith(".jsonl") && fileName.toLowerCase().includes("scheduled-history-export")
    );
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
    killer.on("error", () => {});
  } catch {
    /* 続行 */
  }
}

/**
 * @param {number} ms
 * @returns {Promise<void>}
 */
function sleep(ms) {
  return new Promise((resolvePromise) => setTimeout(resolvePromise, ms));
}

/**
 * @param {Record<string, string>} childEnv
 * @param {number} maxWaitMs
 * @returns {Promise<import('child_process').ChildProcess>}
 */
async function spawnServerUntilScheduledFiles(childEnv, maxWaitMs) {
  if (!fs.existsSync(distServerPath)) {
    throw new Error(`test7106ScheduledExportDisable: dist/server.js missing. path=${distServerPath}`);
  }
  const child = spawn(process.execPath, [distServerPath], {
    cwd: projectRootPath,
    env: { ...process.env, ...childEnv },
    windowsHide: true
  });
  let combinedLog = "";
  const onChunk = (chunk) => {
    combinedLog += chunk.toString();
  };
  child.stdout?.on("data", onChunk);
  child.stderr?.on("data", onChunk);

  const startMs = Date.now();
  while (Date.now() - startMs < maxWaitMs) {
    if (listScheduledExportFiles().length >= 1) {
      return child;
    }
    if (child.exitCode !== null) {
      throw new Error(
        `test7106ScheduledExportDisable: LocalServer exited early in phase1. exitCode=${child.exitCode} logTail=${combinedLog.slice(-2500)}`
      );
    }
    await sleep(2000);
  }
  killProcessTreeWindows(child.pid);
  throw new Error(
    `test7106ScheduledExportDisable: phase1 timeout waiting for first scheduled file. maxWaitMs=${maxWaitMs} logTail=${combinedLog.slice(-2500)}`
  );
}

/**
 * @returns {Promise<void>}
 */
async function main() {
  if (fs.existsSync(isolateDbPath)) {
    fs.rmSync(isolateDbPath, { force: true });
  }
  for (const suffix of ["-wal", "-shm"]) {
    const sidePath = `${isolateDbPath}${suffix}`;
    if (fs.existsSync(sidePath)) {
      fs.rmSync(sidePath, { force: true });
    }
  }
  if (fs.existsSync(autotestExportDir)) {
    fs.rmSync(autotestExportDir, { recursive: true, force: true });
  }
  fs.mkdirSync(autotestExportDir, { recursive: true });

  const phase1Env = {
    LOCAL_SERVER_HTTP_PORT: "3198",
    OTA_HTTPS_PORT: "4458",
    LOCAL_HISTORY_DB_PATH: isolateDbPath,
    LOCAL_HISTORY_SCHEDULED_EXPORT_ENABLED: "true",
    LOCAL_HISTORY_SCHEDULED_EXPORT_INTERVAL_MS: "65000",
    LOCAL_HISTORY_EXPORT_DIR: autotestExportDir
  };

  console.info("[INFO] 7106-disable phase1: ENABLED=true, waiting for first scheduled export file...");
  const child1 = await spawnServerUntilScheduledFiles(phase1Env, 130000);
  const filesAfterPhase1 = new Set(listScheduledExportFiles());
  console.info(
    `[INFO] 7106-disable phase1: OK. files=${Array.from(filesAfterPhase1).join(", ") || "(none)"}`
  );
  killProcessTreeWindows(child1.pid);
  await sleep(2000);

  if (filesAfterPhase1.size < 1) {
    throw new Error("test7106ScheduledExportDisable: phase1 expected at least 1 scheduled file.");
  }

  const phase2Env = {
    LOCAL_SERVER_HTTP_PORT: "3198",
    OTA_HTTPS_PORT: "4458",
    LOCAL_HISTORY_DB_PATH: isolateDbPath,
    LOCAL_HISTORY_SCHEDULED_EXPORT_ENABLED: "false",
    LOCAL_HISTORY_SCHEDULED_EXPORT_INTERVAL_MS: "65000",
    LOCAL_HISTORY_EXPORT_DIR: autotestExportDir
  };

  console.info("[INFO] 7106-disable phase2: ENABLED=false, waiting 90s (no new scheduled files expected)...");
  const child2 = spawn(process.execPath, [distServerPath], {
    cwd: projectRootPath,
    env: { ...process.env, ...phase2Env },
    windowsHide: true
  });
  await sleep(90000);
  killProcessTreeWindows(child2.pid);
  await sleep(1500);

  const filesAfterPhase2 = listScheduledExportFiles();
  const newFiles = filesAfterPhase2.filter((fileName) => !filesAfterPhase1.has(fileName));

  const reportPath = path.join(
    projectRootPath,
    "logs",
    "test-reports",
    `7106-disable-verify-${new Date().toISOString().replace(/[:.]/g, "-")}.json`
  );
  const reportDir = path.dirname(reportPath);
  if (!fs.existsSync(reportDir)) {
    fs.mkdirSync(reportDir, { recursive: true });
  }
  const report = {
    executedAt: new Date().toISOString(),
    exportDir: autotestExportDir,
    phase1Files: Array.from(filesAfterPhase1),
    phase2AllFiles: filesAfterPhase2,
    newFilesAfterPhase2: newFiles
  };
  fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`, "utf-8");

  if (newFiles.length > 0) {
    throw new Error(
      `test7106ScheduledExportDisable: NG. new scheduled files appeared while ENABLED=false: ${newFiles.join(", ")} report=${reportPath}`
    );
  }

  console.info(`[OK] 7106-disable: no new scheduled exports while disabled. report=${reportPath}`);
}

await main();
