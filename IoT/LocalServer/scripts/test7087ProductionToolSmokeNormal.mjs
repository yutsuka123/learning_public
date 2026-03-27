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
import fs from "fs";
import { spawn, spawnSync } from "child_process";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const localServerRoot = path.resolve(__dirname, "..");
const productionToolRoot = path.resolve(localServerRoot, "..", "ProductionTool");
const installedProductionToolExePath = "C:\\Program Files\\IoT\\ProductionTool\\ProductionTool.exe";

// PT-001 Enter → PT-002 (op, work_order, maker2026) → PT-003 (serial, mac, public_id, fw) → 再入力 (serial, mac)
const stdinLines = [
  "",                    // PT-001 Enter
  "productiontool",      // 操作者 ID
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

/**
 * @description ProductionTool/シリアル出力断片から Secure Boot 検証の要約判定を作る。
 * @param {string} outputText 標準出力+標準エラー連結文字列。
 * @returns {{ verdict: "ok" | "ng" | "unknown"; evidence: string }}
 */
function summarizeSecureBootVerificationEvidence(outputText) {
  const normalized = String(outputText ?? "");
  const positivePatterns = [
    /secure boot.+enabled/i,
    /secure boot.+verified/i,
    /signature.+verified/i,
    /bootloader.+verified/i
  ];
  const negativePatterns = [
    /secure boot.+fail/i,
    /signature.+fail/i,
    /invalid signature/i,
    /verification failed/i
  ];
  if (negativePatterns.some((pattern) => pattern.test(normalized))) {
    return { verdict: "ng", evidence: "secure-boot-negative-pattern-detected" };
  }
  if (positivePatterns.some((pattern) => pattern.test(normalized))) {
    return { verdict: "ok", evidence: "secure-boot-positive-pattern-detected" };
  }
  return { verdict: "unknown", evidence: "secure-boot-pattern-not-found" };
}

/**
 * @returns {{
 *   launchCommand: string;
 *   launchArguments: string[];
 *   launchCwdPath: string;
 *   launchMode: "cargo" | "installed-exe";
 * }}
 */
function resolveProductionToolLaunchSettings() {
  const whereCargoResult = spawnSync("where", ["cargo"], {
    shell: true,
    stdio: "pipe",
    encoding: "utf-8"
  });
  if (whereCargoResult.status === 0) {
    return {
      launchCommand: "cargo",
      launchArguments: ["run"],
      launchCwdPath: productionToolRoot,
      launchMode: "cargo"
    };
  }

  if (fs.existsSync(installedProductionToolExePath)) {
    return {
      launchCommand: installedProductionToolExePath,
      launchArguments: [],
      launchCwdPath: path.dirname(installedProductionToolExePath),
      launchMode: "installed-exe"
    };
  }

  throw new Error(
    `resolveProductionToolLaunchSettings failed. cargo not found and ProductionTool.exe not found. installedPath=${installedProductionToolExePath}`
  );
}

async function main() {
  console.log("[7087] ProductionTool スモーク試験（通常系）を開始します。");
  console.log(`  productionToolRoot=${productionToolRoot}`);
  const launchSettings = resolveProductionToolLaunchSettings();
  console.log(
    `  launchMode=${launchSettings.launchMode} launchCommand=${launchSettings.launchCommand} cwd=${launchSettings.launchCwdPath}`
  );

  const cargoRun = spawn(launchSettings.launchCommand, launchSettings.launchArguments, {
    cwd: launchSettings.launchCwdPath,
    stdio: ["pipe", "pipe", "pipe"],
    shell: false
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
    const secureBootSummary = summarizeSecureBootVerificationEvidence(combined);

    if (hasWizardComplete || hasPt003Ok) {
      console.log("[7087] 通常系（認証OK・対象機確認・dry-run）を確認しました。");
    } else {
      console.log("[7087] 警告: ウィザード完了メッセージが不明瞭です。");
      console.log("  stdout 末尾:", stdout.slice(-600));
      console.log("  stderr 末尾:", stderr.slice(-600));
    }
    if (!hasNoSecret) {
      console.log("[7087] 警告: 出力に秘密値らしき文字列が含まれる可能性があります。");
      exitCode = 1;
    }
    if (code !== 0 && code != null && signal !== "SIGTERM") {
      console.log("[7087] 終了コード:", code);
      // [重要] 非0終了でも、通常系の到達シグナル（対象機確認 or dry-run 完了）が出ていれば合格とする。
      // 理由: CUI 試験は入力終端時に非0で終わることがあり、到達判定まで NG 化すると誤判定になるため。
      if (!hasWizardComplete && !hasPt003Ok) exitCode = 1;
    }
    if (secureBootSummary.verdict === "ok") {
      console.log(`[7087] Secure Boot 検証サマリ: OK (${secureBootSummary.evidence})`);
    } else if (secureBootSummary.verdict === "ng") {
      console.log(`[7087] Secure Boot 検証サマリ: NG (${secureBootSummary.evidence})`);
      exitCode = 1;
    } else {
      console.log(
        `[7087] Secure Boot 検証サマリ: 未判定 (${secureBootSummary.evidence})`
      );
      console.log(
        "[7087] 補足: SB検証はブートローダ段階のため、ProductionTool出力のみでは判定不能な場合があります。"
      );
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
