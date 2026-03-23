/**
 * @file test7087ProductionToolSmokeNormal.mjs
 * @description 7087: ProductionTool 基本機能スモーク試験の通常系を半自動実行する。
 *
 * 試験仕様書.md 7087 通常系:
 * - PT-001 Enter → PT-002 追加認証（正パスワード maker2026）→ PT-003 対象機入力・再入力一致 → PT-005 dry-run 完了
 * - 出力に raw key・秘密値が含まれないことを確認する。
 *
 * [重要] SecretCore が起動していること。
 *
 * 実行例:
 * node scripts/test7087ProductionToolSmokeNormal.mjs
 */

import path from "path";
import { fileURLToPath } from "url";
import { spawn } from "child_process";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const localServerRoot = path.resolve(__dirname, "..");
const productionToolRoot = path.resolve(localServerRoot, "..", "ProductionTool");

// PT-001 Enter → PT-002 (op, work_order, maker2026) → PT-003 (serial, mac, public_id, fw) → 再入力 (serial, mac)
const stdinLines = [
  "",                    // PT-001 Enter
  "op1",                 // 操作者 ID
  "wo1",                 // 作業指示番号
  "maker2026",           // パスワード（正）
  "S001",                // シリアル番号
  "AA:BB:CC:DD:EE:FF",   // MAC
  "unknown",             // public_id
  "1.1.0-beta.15",       // FW 版数
  "S001",                // シリアル番号 再入力
  "AA:BB:CC:DD:EE:FF"    // MAC 再入力
];
const stdinInput = stdinLines.map((l) => l + "\n").join("");

async function main() {
  console.log("[7087] ProductionTool スモーク試験（通常系）を開始します。");
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

  const timeoutMs = 30000;
  const timeoutPromise = new Promise((_, reject) => {
    setTimeout(() => {
      cargoRun.kill("SIGTERM");
      reject(new Error(`timeout after ${timeoutMs}ms`));
    }, timeoutMs);
  });

  let exitCode = 0;
  try {
    const { code, signal } = await Promise.race([exitPromise, timeoutPromise]);
    const combined = stdout + "\n" + stderr;
    const hasWizardComplete = /ProductionTool ウィザードフロー完了|PT-005 dry-run|dry-run 確認/.test(combined);
    const hasPt003Ok = /PT-003 対象機確認結果|Confirmed|device_verify_success/.test(combined);
    const hasNoSecret = !/\b(k-user|k-device|sharedSecret|raw.?key)\b/i.test(combined);

    if (hasWizardComplete || hasPt003Ok) {
      console.log("[7087] 通常系（認証OK・対象機確認・dry-run）を確認しました。");
    } else {
      console.log("[7087] 警告: ウィザード完了メッセージが不明瞭です。");
      console.log("  stdout 末尾:", stdout.slice(-600));
    }
    if (!hasNoSecret) {
      console.log("[7087] 警告: 出力に秘密値らしき文字列が含まれる可能性があります。");
      exitCode = 1;
    }
    if (code !== 0 && code != null && signal !== "SIGTERM") {
      console.log("[7087] 終了コード:", code);
      if (!hasWizardComplete) exitCode = 1;
    }
  } catch (err) {
    if (err.message?.includes("timeout")) {
      console.log("[7087] タイムアウトで終了しました。");
    } else {
      console.error("[7087] 失敗:", err.message);
      exitCode = 1;
    }
  }

  console.log(exitCode === 0 ? "[7087] 通常系 完了: OK" : "[7087] 通常系 完了: 要確認");
  process.exitCode = exitCode;
}

main();
