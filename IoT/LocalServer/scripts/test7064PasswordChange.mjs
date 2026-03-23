/**
 * @file test7064PasswordChange.mjs
 * @description 7064: AP/管理者パスワード変更推奨運用試験（LocalServer 管理者パスワード部分）
 *
 * 主仕様（試験仕様書.md 7064 準拠）:
 * - LocalServer 管理者パスワード変更 API の動作確認
 * - 変更後の旧値拒否・新値受諾を確認
 * - 監査ログ（logs/security-audit.log）への記録確認（平文パスワード非出力）
 *
 * [厳守] 試験後はデフォルトへ復元する。理由: 試験用一時値が残ると運用障害となるため。
 *
 * 制限事項:
 * - APロールパスワード変更（POST /api/admin/ap/password/change）は AP モード機体接続が必要なため本スクリプトでは対象外
 * - credentials/rotation は別途手動実施
 *
 * 実行手順:
 * 1. LocalServer を起動済みにすること（npm run build && npm run start）
 * 2. node scripts/test7064PasswordChange.mjs [--baseUrl http://127.0.0.1:3100] [--currentPassword <現パス>]
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { loadConfig } from "../dist/config.js";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(scriptDir, "..");

/**
 * @param {string[]} argv
 * @returns {{ baseUrl: string; currentPassword: string | null }}
 */
function parseArgs(argv) {
  const baseUrl = (argv.find((a, i) => a === "--baseUrl" && argv[i + 1]) && argv[argv.indexOf("--baseUrl") + 1]) || "http://127.0.0.1:3100";
  const currentPassword =
    argv.find((a, i) => a === "--currentPassword" && argv[i + 1]) && argv[argv.indexOf("--currentPassword") + 1];
  return { baseUrl, currentPassword: currentPassword || null };
}

/**
 * @param {string} baseUrl
 * @param {string} username
 * @param {string} password
 * @returns {Promise<string>}
 */
async function login(baseUrl, username, password) {
  const res = await fetch(`${baseUrl}/api/admin/auth/login`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, password })
  });
  const json = await res.json();
  if (!res.ok || json.result !== "OK") {
    throw new Error(`login failed. status=${res.status} detail=${json.detail ?? "unknown"}`);
  }
  const token = String(json.token ?? "");
  if (!token) throw new Error("login failed. token is empty.");
  return token;
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} currentPassword
 * @param {string} newPassword
 * @param {string} [reason]
 * @returns {Promise<{ result: string; detail?: string }>}
 */
async function changePassword(baseUrl, token, currentPassword, newPassword, reason = "7064 test") {
  const res = await fetch(`${baseUrl}/api/admin/auth/password/change`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${token}`
    },
    body: JSON.stringify({
      currentPassword,
      newPassword,
      reason,
      scope: "7064-test",
      expiresAt: ""
    })
  });
  return res.json();
}

/**
 * @param {string} logPath
 * @returns {boolean}
 */
function auditLogContainsLocalAdminPasswordChanged(logPath) {
  if (!fs.existsSync(logPath)) return false;
  const content = fs.readFileSync(logPath, "utf-8");
  return content.includes("localAdminPasswordChanged") && content.includes("result");
}

/**
 * @param {string} logPath
 * @returns {boolean}
 */
function auditLogHasNoPlaintextPassword(logPath) {
  if (!fs.existsSync(logPath)) return true;
  const content = fs.readFileSync(logPath, "utf-8");
  // 7064テストで使用したパスワードが平文で含まれていないことを簡易確認
  // 実際の試験では change-me-7064-test 等がログに出ないこと
  const lastLines = content.split("\n").slice(-20).join("\n");
  return !lastLines.includes("change-me-7064-test");
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const config = loadConfig();
  const currentPassword = args.currentPassword ?? config.adminPassword;
  const username = config.adminUsername;
  const newPassword = "change-me-7064-test";
  const auditLogPath = path.join(projectRoot, "logs", "security-audit.log");

  console.log("[7064] LocalServer 管理者パスワード変更試験 開始");
  console.log(`  baseUrl=${args.baseUrl} username=${username}`);

  let passwordWasChanged = false;
  try {
    // 1) 初期ログイン確認
    const token = await login(args.baseUrl, username, currentPassword);
    console.log("  [OK] 1) 初期ログイン成功");

    // 2) パスワード変更
    const changeRes = await changePassword(args.baseUrl, token, currentPassword, newPassword);
    if (changeRes.result !== "OK") {
      throw new Error(`password change failed. result=${changeRes.result} detail=${changeRes.detail ?? ""}`);
    }
    passwordWasChanged = true;
    console.log("  [OK] 2) パスワード変更 API 成功");

    // 3) 旧パスワードでログイン失敗を確認
    try {
      await login(args.baseUrl, username, currentPassword);
      throw new Error("old password should have been rejected");
    } catch (e) {
      if (e.message?.includes("login failed") || e.message?.includes("401")) {
        console.log("  [OK] 3) 旧パスワード拒否確認");
      } else {
        throw e;
      }
    }

    // 4) 新パスワードでログイン成功を確認
    const newToken = await login(args.baseUrl, username, newPassword);
    if (!newToken) throw new Error("new password login failed");
    console.log("  [OK] 4) 新パスワードでログイン成功");

    // 5) 元のパスワードへ復元（試験後クリーンアップ）[厳守]
    const restoreRes = await changePassword(args.baseUrl, newToken, newPassword, currentPassword, "7064 test restore");
    if (restoreRes.result !== "OK") {
      console.warn("  [WARN] 5) パスワード復元失敗（手動で data/securityState.json を確認してください）");
    } else {
      passwordWasChanged = false;
      console.log("  [OK] 5) パスワード復元成功");
    }

    // 6) 監査ログ確認
    const auditOk = auditLogContainsLocalAdminPasswordChanged(auditLogPath);
    const noPlain = auditLogHasNoPlaintextPassword(auditLogPath);
    if (auditOk) {
      console.log("  [OK] 6) 監査ログに localAdminPasswordChanged 記録あり");
    } else {
      console.warn("  [WARN] 6) 監査ログの localAdminPasswordChanged 確認に失敗");
    }
    if (noPlain) {
      console.log("  [OK] 6) 監査ログに平文パスワードなし");
    } else {
      console.warn("  [WARN] 6) 監査ログに平文パスワードが含まれる可能性あり");
    }

    const allPass = auditOk && noPlain;
    console.log(allPass ? "\n[7064] LocalServer 管理者パスワード変更試験: OK" : "\n[7064] 一部 NG（上記参照）");
    process.exit(allPass ? 0 : 1);
  } catch (err) {
    console.error("[7064] 試験失敗:", err.message);
    throw err;
  } finally {
    if (passwordWasChanged) {
      try {
        const restoreToken = await login(args.baseUrl, username, newPassword);
        const r = await changePassword(args.baseUrl, restoreToken, newPassword, currentPassword, "7064 test restore on error");
        if (r.result === "OK") {
          console.warn("  [復元] 試験失敗時のデフォルト復元を実施しました。");
        }
      } catch (e) {
        console.warn("  [WARN] 試験失敗時の復元に失敗。手動で data/securityState.json を確認してください。");
      }
    }
  }
}

main();
