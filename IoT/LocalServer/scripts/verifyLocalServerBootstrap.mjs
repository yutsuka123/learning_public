/**
 * @file verifyLocalServerBootstrap.mjs
 * @description LocalServer + SecretCore の **stdin ブートストラップ**〜**IPC health OK** までを短時間で検証する（開発・CI 手前のスモーク）。
 *
 * @remarks
 * - [重要] 成功条件はログに `SecretCore ready.` が出ること、および `GET /api/health` が HTTP 200 であること。
 * - [厳守] 既定では **SecretCore を `cargo build`（debug）**、LocalServer を **`npm run build`** してから子プロセスで `node dist/server.js` を起動する。
 * - [推奨] ビルド済みのみ試す場合は `--skipCargo` / `--skipTsc` を付与する。
 * - [制限] MQTT ブローカ未起動でも HTTP は立ち上がるため本スクリプトは成立するが、`connectByRustBridge` 以降は環境依存のログのみとなる。
 *
 * 実行例:
 *   cd IoT/LocalServer && node scripts/verifyLocalServerBootstrap.mjs
 *   node scripts/verifyLocalServerBootstrap.mjs --skipCargo --skipTsc
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { spawn, spawnSync } from "child_process";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const localServerRootPath = path.resolve(scriptDirectoryPath, "..");
const secretCoreRootPath = path.resolve(localServerRootPath, "..", "SecretCore");
const distServerPath = path.join(localServerRootPath, "dist", "server.js");

/**
 * @param {string[]} argv
 * @returns {{ skipCargo: boolean; skipTsc: boolean; maxWaitMs: number; httpPort: number }}
 */
function parseArgs(argv) {
  return {
    skipCargo: argv.includes("--skipCargo"),
    skipTsc: argv.includes("--skipTsc"),
    maxWaitMs: (() => {
      const i = argv.indexOf("--maxWaitMs");
      const raw = i >= 0 && argv[i + 1] ? Number.parseInt(String(argv[i + 1]), 10) : Number.NaN;
      return Number.isFinite(raw) && raw >= 5000 ? raw : 45000;
    })(),
    httpPort: (() => {
      const fromEnv = process.env.LOCAL_SERVER_HTTP_PORT;
      const parsed = fromEnv ? Number.parseInt(fromEnv, 10) : Number.NaN;
      return Number.isFinite(parsed) && parsed > 0 ? parsed : 3100;
    })()
  };
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
 * @returns {Promise<void>}
 */
async function main() {
  const { skipCargo, skipTsc, maxWaitMs, httpPort } = parseArgs(process.argv.slice(2));

  if (!skipCargo) {
    if (!fs.existsSync(path.join(secretCoreRootPath, "Cargo.toml"))) {
      throw new Error(`verifyLocalServerBootstrap: SecretCore not found. path=${secretCoreRootPath}`);
    }
    console.info("verifyLocalServerBootstrap: cargo build (SecretCore debug) ...");
    const cargoResult = spawnSync("cargo", ["build"], {
      cwd: secretCoreRootPath,
      stdio: "inherit",
      shell: false,
      env: process.env
    });
    if (cargoResult.status !== 0) {
      throw new Error("verifyLocalServerBootstrap: cargo build failed.");
    }
  }

  if (!skipTsc) {
    console.info("verifyLocalServerBootstrap: npm run build ...");
    const npmResult = spawnSync("npm", ["run", "build"], {
      cwd: localServerRootPath,
      stdio: "inherit",
      shell: true,
      env: process.env
    });
    if (npmResult.status !== 0) {
      throw new Error("verifyLocalServerBootstrap: npm run build failed.");
    }
  }

  if (!fs.existsSync(distServerPath)) {
    throw new Error(`verifyLocalServerBootstrap: dist/server.js missing. path=${distServerPath}`);
  }

  const child = spawn(process.execPath, [distServerPath], {
    cwd: localServerRootPath,
    env: { ...process.env },
    windowsHide: true
  });

  let combinedLog = "";
  const markerReady = "SecretCore ready.";
  const markerBootstrapFail = "SecretCore bootstrap failed.";

  child.stdout?.on("data", (chunk) => {
    combinedLog += chunk.toString();
  });
  child.stderr?.on("data", (chunk) => {
    combinedLog += chunk.toString();
  });

  const startMs = Date.now();
  /** @type {{ ok: boolean; detail: string }} */
  const outcome = { ok: false, detail: "" };

  while (Date.now() - startMs < maxWaitMs) {
    if (combinedLog.includes(markerBootstrapFail)) {
      outcome.detail = "bootstrap failed (see LocalServer stderr in console)";
      break;
    }
    if (combinedLog.includes(markerReady)) {
      try {
        const healthUrl = `http://127.0.0.1:${httpPort}/api/health`;
        const res = await fetch(healthUrl, { signal: AbortSignal.timeout(8000) });
        if (res.ok) {
          outcome.ok = true;
          outcome.detail = `health OK. url=${healthUrl}`;
        } else {
          outcome.detail = `health not OK. status=${res.status} url=${healthUrl}`;
        }
      } catch (fetchError) {
        const msg = fetchError instanceof Error ? fetchError.message : String(fetchError);
        outcome.detail = `fetch /api/health failed. error=${msg}`;
      }
      break;
    }
    if (child.exitCode !== null) {
      outcome.detail = `LocalServer exited early. exitCode=${child.exitCode} logTail=${combinedLog.slice(-2000)}`;
      break;
    }
    await sleep(400);
  }

  if (typeof child.pid === "number") {
    killProcessTreeWindows(child.pid);
  }
  await sleep(1500);

  if (!outcome.ok) {
    const tail = combinedLog.slice(-3500);
    throw new Error(
      `verifyLocalServerBootstrap failed. detail=${outcome.detail} waitMs=${maxWaitMs}\n--- log tail ---\n${tail}`
    );
  }

  console.info(`verifyLocalServerBootstrap: ${outcome.detail}`);
  console.info("verifyLocalServerBootstrap: OK");
}

await main();
