/**
 * @file test7033AdminLoginLockout.mjs
 * @description 7033: LocalServer 管理者ログインの連続失敗ロック試験。
 *
 * 試験仕様書.md 7033 準拠:
 * - 管理者ログインへ誤パスワードを 3 回連続投入する。
 * - 3 回目で 5 分ロックが開始され、`retryAfterSeconds` と `lockedUntil` が返ることを確認する。
 * - ロック中は正しい認証情報でも受け付けず、`403` を返すことを確認する。
 *
 * [重要] LocalServer が起動済みであることを前提とする。
 * [厳守] 本試験は lockout 状態を意図的に発生させるため、実施後 5 分間は同一送信元からの管理者ログインが拒否される。
 * [制限事項] 5 分経過後の自動解除までは待たず、「ロック開始」と「ロック中拒否」までを確認対象とする。
 *
 * 実行例:
 * node scripts/test7033AdminLoginLockout.mjs
 * node scripts/test7033AdminLoginLockout.mjs --baseUrl http://127.0.0.1:3100
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { loadBuiltConfigModule } from "./testCommon.mjs";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");
const securityStateFilePath = path.join(projectRootPath, "data", "securityState.json");
const defaultBaseUrl = "http://127.0.0.1:3100";
const wrongPasswordText = "wrong-password-for-7033";

/**
 * @param {string[]} argv
 * @returns {{ baseUrl: string }}
 */
function parseArgs(argv) {
  const baseUrlIndex = argv.indexOf("--baseUrl");
  const baseUrl = baseUrlIndex >= 0 && argv[baseUrlIndex + 1] ? argv[baseUrlIndex + 1] : defaultBaseUrl;
  return { baseUrl };
}

/**
 * @returns {{ adminUsername: string; adminPassword: string }}
 */
function loadSecurityState() {
  if (!fs.existsSync(securityStateFilePath)) {
    throw new Error(`loadSecurityState failed. file not found. path=${securityStateFilePath}`);
  }
  const rawText = fs.readFileSync(securityStateFilePath, "utf-8");
  const parsedJson = JSON.parse(rawText);
  const adminUsername = String(parsedJson.adminUsername ?? "").trim();
  const adminPassword = String(parsedJson.adminPassword ?? "");
  if (adminUsername.length === 0 || adminPassword.length === 0) {
    throw new Error(
      `loadSecurityState failed. adminUsername/adminPassword is empty. path=${securityStateFilePath}`
    );
  }
  return { adminUsername, adminPassword };
}

/**
 * @param {string} baseUrl
 * @param {{ username: string, password: string }} requestBody
 * @returns {Promise<{ status: number, json: any }>}
 */
async function postAdminLogin(baseUrl, requestBody) {
  const response = await fetch(`${baseUrl}/api/admin/auth/login`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(requestBody)
  });
  const responseText = await response.text();
  let responseJson = {};
  try {
    responseJson = JSON.parse(responseText);
  } catch (parseError) {
    throw new Error(
      `postAdminLogin failed. response is not JSON. baseUrl=${baseUrl} status=${response.status} body=${responseText} reason=${parseError.message}`
    );
  }
  return { status: response.status, json: responseJson };
}

async function main() {
  const { baseUrl } = parseArgs(process.argv.slice(2));
  const configModule = await loadBuiltConfigModule();
  const builtConfig = configModule.loadConfig();
  const securityState = loadSecurityState();
  const adminUsername = securityState.adminUsername || String(builtConfig.adminUsername ?? "").trim();
  const adminPassword = securityState.adminPassword || String(builtConfig.adminPassword ?? "");

  if (adminUsername.length === 0 || adminPassword.length === 0) {
    throw new Error(
      `main failed. admin credential is empty. baseUrl=${baseUrl} adminUsername=${adminUsername}`
    );
  }

  console.log("[7033] LocalServer 管理者ログインの連続失敗ロック試験を開始します。");
  console.log(`  baseUrl=${baseUrl} adminUsername=${adminUsername}`);

  for (let attemptNumber = 1; attemptNumber <= 2; attemptNumber += 1) {
    const attemptResult = await postAdminLogin(baseUrl, {
      username: adminUsername,
      password: wrongPasswordText
    });
    console.log(
      `[7033] wrong attempt ${attemptNumber}: status=${attemptResult.status} body=${JSON.stringify(attemptResult.json)}`
    );
    if (attemptResult.status !== 401) {
      throw new Error(
        `main failed. expected 401 before lockout. attemptNumber=${attemptNumber} status=${attemptResult.status}`
      );
    }
  }

  const thirdAttemptResult = await postAdminLogin(baseUrl, {
    username: adminUsername,
    password: wrongPasswordText
  });
  console.log(`[7033] wrong attempt 3: status=${thirdAttemptResult.status} body=${JSON.stringify(thirdAttemptResult.json)}`);
  if (thirdAttemptResult.status !== 403) {
    throw new Error(`main failed. expected 403 on third failure. status=${thirdAttemptResult.status}`);
  }
  if (Number(thirdAttemptResult.json.retryAfterSeconds ?? 0) < 299) {
    throw new Error(
      `main failed. retryAfterSeconds is too small. retryAfterSeconds=${String(thirdAttemptResult.json.retryAfterSeconds)}`
    );
  }
  if (String(thirdAttemptResult.json.lockedUntil ?? "").trim().length === 0) {
    throw new Error("main failed. lockedUntil is empty on third failure.");
  }

  const lockedCorrectAttemptResult = await postAdminLogin(baseUrl, {
    username: adminUsername,
    password: adminPassword
  });
  console.log(
    `[7033] correct credential during lock: status=${lockedCorrectAttemptResult.status} body=${JSON.stringify(lockedCorrectAttemptResult.json)}`
  );
  if (lockedCorrectAttemptResult.status !== 403) {
    throw new Error(
      `main failed. expected 403 while lock is active. status=${lockedCorrectAttemptResult.status}`
    );
  }

  console.log("[7033] 完了: OK");
}

main().catch((error) => {
  console.error("[7033] 完了: NG");
  console.error(`[7033] 失敗詳細: ${error.message}`);
  process.exitCode = 1;
});
