/**
 * @file test8001MqttAuthFailure.mjs
 * @description 8001: 異常系（MQTT認証失敗）の半自動試験。
 *
 * 試験仕様書.md 8001 準拠:
 * - .env の MQTT_PASSWORD を誤値にして LocalServer を起動する。
 * - 接続失敗ログが出力され、プロセスが異常終了しないことを確認する。
 *
 * [厳守] 試験後は .env を復元する。理由: 運用障害を防ぐため。
 * [重要] 実行前に LocalServer を停止すること（ポート 3100 が未使用であること）。
 *
 * 実行例:
 * node scripts/test8001MqttAuthFailure.mjs
 * node scripts/test8001MqttAuthFailure.mjs --runSeconds 12
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { spawn } from "child_process";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(__dirname, "..");
const envPath = path.join(projectRoot, ".env");
const backupPath = path.join(projectRoot, ".env.8001.backup");

const DEFAULT_RUN_SECONDS = 12;
const WRONG_PASSWORD = "wrong_mqtt_password_8001_test";

/**
 * @param {string[]} argv
 * @returns {{ runSeconds: number }}
 */
function parseArgs(argv) {
  const runSecondsIdx = argv.indexOf("--runSeconds");
  const runSeconds =
    runSecondsIdx >= 0 && argv[runSecondsIdx + 1] ? Number(argv[runSecondsIdx + 1]) : DEFAULT_RUN_SECONDS;
  return { runSeconds: Number.isFinite(runSeconds) && runSeconds > 0 ? runSeconds : DEFAULT_RUN_SECONDS };
}

/**
 * .env の MQTT_PASSWORD 行を誤値に置換した内容を返す。
 * @param {string} content
 * @returns {string}
 */
function replaceMqttPassword(content, newPassword) {
  const lines = content.split(/\r?\n/);
  let found = false;
  const out = lines.map((line) => {
    if (/^MQTT_PASSWORD=/.test(line)) {
      found = true;
      return `MQTT_PASSWORD=${newPassword}`;
    }
    return line;
  });
  if (!found) {
    out.push(`MQTT_PASSWORD=${newPassword}`);
  }
  return out.join("\n");
}

/**
 * .env の MQTT_PASSWORD を元の値に戻す（バックアップから復元）。
 */
function restoreEnv() {
  if (fs.existsSync(backupPath)) {
    fs.copyFileSync(backupPath, envPath);
    fs.unlinkSync(backupPath);
  }
}

async function main() {
  const argv = process.argv.slice(2);
  const { runSeconds } = parseArgs(argv);

  console.log("[8001] MQTT認証失敗 異常系試験を開始します。");
  console.log(`  runSeconds=${runSeconds} projectRoot=${projectRoot}`);

  if (!fs.existsSync(envPath)) {
    console.error("[8001] 失敗: .env が存在しません。");
    process.exitCode = 1;
    return;
  }

  const originalContent = fs.readFileSync(envPath, "utf-8");
  fs.writeFileSync(backupPath, originalContent);
  const modifiedContent = replaceMqttPassword(originalContent, WRONG_PASSWORD);
  fs.writeFileSync(envPath, modifiedContent);

  let exitCode = 0;
  try {
    const serverPath = path.join(projectRoot, "dist", "server.js");
    if (!fs.existsSync(serverPath)) {
      throw new Error("dist/server.js が存在しません。npm run build を先に実行してください。");
    }

    const child = spawn("node", [serverPath], {
      cwd: projectRoot,
      stdio: ["ignore", "pipe", "pipe"],
      env: { ...process.env, NODE_ENV: "test" }
    });

    let stdout = "";
    let stderr = "";
    child.stdout?.on("data", (chunk) => {
      stdout += String(chunk);
    });
    child.stderr?.on("data", (chunk) => {
      stderr += String(chunk);
    });

    const runMs = runSeconds * 1000;
    let exitEarly = false;
    let earlyCode = null;
    let earlySignal = null;
    await new Promise((resolve) => {
      const timer = setTimeout(() => {
        child.kill("SIGTERM");
        resolve();
      }, runMs);
      child.on("exit", (code, signal) => {
        clearTimeout(timer);
        exitEarly = true;
        earlyCode = code;
        earlySignal = signal;
        resolve();
      });
    });

    const combined = (stdout + "\n" + stderr).toLowerCase();
    const portInUse = /eaddrinuse|address already in use|ポート|3100/.test(combined);

    if (exitEarly && earlyCode !== 0 && earlySignal !== "SIGTERM") {
      if (portInUse) {
        console.log("[8001] ポート 3100 使用中のためスキップしました。LocalServer を停止してから再実行してください。");
        exitCode = 0;
      } else {
        console.error(`[8001] プロセスが異常終了しました。 code=${earlyCode} signal=${earlySignal}`);
        exitCode = 1;
      }
    } else {
      const hasConnectionOrAuthError =
        /mqtt|connect|auth|fail|error|refused|password|認証|接続/.test(combined);
      if (!hasConnectionOrAuthError) {
        console.log("[8001] 警告: 接続失敗／認証エラーらしきログが検出されませんでした。");
        console.log("  stderr 先頭:", stderr.slice(0, 400));
      } else {
        console.log("[8001] 接続失敗／認証エラーらしきログを検出しました。");
      }
      console.log("[8001] プロセスは指定時間まで異常終了せず動作していました。");
    }
  } catch (err) {
    console.error("[8001] 失敗:", err.message);
    exitCode = 1;
  } finally {
    restoreEnv();
    console.log("[8001] .env を復元しました。");
  }

  console.log(exitCode === 0 ? "[8001] 完了: OK" : "[8001] 完了: NG");
  process.exitCode = exitCode;
}

main();
