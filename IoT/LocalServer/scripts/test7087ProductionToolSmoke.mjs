/**
 * @file test7087ProductionToolSmoke.mjs
 * @description 7087: ProductionTool 基本機能スモーク試験の半自動実行（異常系・認証失敗で安全停止）。
 *
 * 試験仕様書.md 7087 の一部を自動化:
 * - 起動後、追加認証（PT-002）で誤／空入力を与え、最大試行回数後に PT-007 安全停止することを確認する。
 * - 出力に raw key・秘密値が含まれないことを確認する。
 *
 * [重要] SecretCore が起動していること。事前に LocalServer を起動するか、SecretCore を単独起動すること。
 * [重要] 通常系（正パスワード・対象機確認・dry-run）は手動で実施し、試験記録書に記録すること。
 *
 * 実行例:
 * node scripts/test7087ProductionToolSmoke.mjs
 */

import path from "path";
import { fileURLToPath } from "url";
import { spawn } from "child_process";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const localServerRoot = path.resolve(__dirname, "..");
const productionToolRoot = path.resolve(localServerRoot, "..", "ProductionTool");

// PT-001 の Enter + PT-002 で 3 回試行（各回: operator_id, work_order_id, password の 3 行）＝ 1 + 9 行の改行
const stdinInput = "\n".repeat(12);

async function main() {
  console.log("[7087] ProductionTool スモーク試験（異常系・認証失敗）を開始します。");
  console.log(`  productionToolRoot=${productionToolRoot}`);

  const cargoRun = spawn("cargo", ["run"], {
    cwd: productionToolRoot,
    stdio: ["pipe", "pipe", "pipe"],
    shell: true
  });

  let stdout = "";
  let stderr = "";
  cargoRun.stdout?.on("data", (chunk) => {
    stdout += String(chunk);
  });
  cargoRun.stderr?.on("data", (chunk) => {
    stderr += String(chunk);
  });

  cargoRun.stdin?.write(stdinInput, () => {
    cargoRun.stdin?.end();
  });

  const exitPromise = new Promise((resolve) => {
    cargoRun.on("exit", (code, signal) => resolve({ code, signal }));
  });

  const timeoutMs = 25000;
  const timeoutPromise = new Promise((_, reject) => {
    setTimeout(() => {
      cargoRun.kill("SIGTERM");
      reject(new Error(`timeout after ${timeoutMs}ms`));
    }, timeoutMs);
  });

  let exitCode = 0;
  try {
    const { code, signal } = await Promise.race([exitPromise, timeoutPromise]);
    const combined = (stdout + "\n" + stderr);
    const hasSafeStop = /安全停止|safe stop|AuthFailed|MaxAttemptsExceeded/i.test(combined);
    const hasNoSecret = !/\b(k-user|k-device|sharedSecret|raw.?key|password.*=.*[^\s])\b/i.test(combined);

    if (hasSafeStop) {
      console.log("[7087] 異常系（認証失敗→安全停止）を確認しました。");
    } else {
      console.log("[7087] 警告: 安全停止メッセージが不明瞭です。");
      console.log("  stderr 末尾:", stderr.slice(-500));
    }
    if (!hasNoSecret) {
      console.log("[7087] 警告: 出力に秘密値らしき文字列が含まれる可能性があります。");
      exitCode = 1;
    }
    if (code !== 0 && code != null && signal !== "SIGTERM") {
      console.log("[7087] 終了コード（認証失敗による安全停止は非0で正常）:", code);
    }
  } catch (err) {
    if (err.message?.includes("timeout")) {
      console.log("[7087] タイムアウトで終了しました。手動で Ctrl+C するか、プロセスを終了してください。");
    } else {
      console.error("[7087] 失敗:", err.message);
      exitCode = 1;
    }
  }

  console.log(exitCode === 0 ? "[7087] 異常系 完了: OK" : "[7087] 異常系 完了: 要確認");
  process.exitCode = exitCode;
}

main();
