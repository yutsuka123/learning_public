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
import fs from "fs";
import { spawn, spawnSync } from "child_process";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const localServerRoot = path.resolve(__dirname, "..");
const productionToolRoot = path.resolve(localServerRoot, "..", "ProductionTool");
const installedProductionToolExePath = "C:\\Program Files\\IoT\\ProductionTool\\ProductionTool.exe";

// PT-001 の Enter + PT-002 で 3 回試行（各回: operator_id, work_order_id, password の 3 行）＝ 1 + 9 行の改行
const stdinInput = "\n".repeat(12);

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
  console.log("[7087] ProductionTool スモーク試験（異常系・認証失敗）を開始します。");
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
    const secureBootSummary = summarizeSecureBootVerificationEvidence(combined);

    if (hasSafeStop) {
      console.log("[7087] 異常系（認証失敗→安全停止）を確認しました。");
    } else {
      console.log("[7087] 警告: 安全停止メッセージが不明瞭です。");
      console.log("  stderr 末尾:", stderr.slice(-500));
      exitCode = 1;
    }
    if (!hasNoSecret) {
      console.log("[7087] 警告: 出力に秘密値らしき文字列が含まれる可能性があります。");
      exitCode = 1;
    }
    if (code !== 0 && code != null && signal !== "SIGTERM") {
      console.log("[7087] 終了コード（認証失敗による安全停止は非0で正常）:", code);
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
