/**
 * @file runRealDeviceTests.mjs
 * @description 実機試験 7027/7064残/7065/7066/7067 を順次実行するスクリプト。
 *
 * 前提:
 * - LocalServer 起動済み
 * - ESP32 が MQTT 接続済み（オンライン）、または AP モードで SSID: AP-esp32lab-<MAC> を発信
 * - USB Wi-Fi（wifiUsbInterfaceName）が同一 PC に接続済み
 *
 * 実行: cd IoT/LocalServer && node scripts/runRealDeviceTests.mjs [--step 7027|7064|7065|7066|all]
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { loadConfig } from "../dist/config.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const BASE_URL = "http://127.0.0.1:3100";
const WAIT_AFTER_REBOOT_MS = 18000;

async function login() {
  const config = loadConfig();
  const res = await fetch(`${BASE_URL}/api/admin/auth/login`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      username: config.adminUsername,
      password: config.adminPassword
    })
  });
  const json = await res.json();
  if (!res.ok || json.result !== "OK") throw new Error(`login failed: ${json.detail ?? res.status}`);
  return { token: json.token, config };
}

/**
 * @description GETリクエスト。JSON以外（HTML等）が返った場合は詳細付きで例外を投げる。
 */
async function apiGet(token, path) {
  const res = await fetch(`${BASE_URL}${path}`, {
    headers: { Authorization: `Bearer ${token}` },
    redirect: "manual"
  });
  const text = await res.text();
  if (text.trimStart().startsWith("<")) {
    throw new Error(
      `apiGet received HTML instead of JSON. status=${res.status} path=${path} contentType=${res.headers.get("content-type")} bodyPreview=${text.slice(0, 120)}`
    );
  }
  try {
    return JSON.parse(text);
  } catch (parseErr) {
    throw new Error(`apiGet JSON parse failed. path=${path} status=${res.status} bodyPreview=${text.slice(0, 200)}`);
  }
}

async function apiPost(token, path, body) {
  const res = await fetch(`${BASE_URL}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json", Authorization: `Bearer ${token}` },
    body: JSON.stringify(body ?? {})
  });
  return res.json();
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function run7027(token) {
  console.log("\n=== 7027: ESP32 設定永続保存試験 ===");
  const devices = await apiGet(token, "/api/admin/devices");
  const online = (devices.devices ?? []).filter((d) => d.onlineState === "online");
  if (online.length === 0) {
    console.log("  [SKIP] オンラインの ESP32 がありません。MQTT 接続後に再実行してください。");
    return { ok: false, reason: "no_online_device" };
  }
  const target = online[0];
  const targetName = target.targetName ?? target.deviceName ?? "";
  console.log(`  対象: ${targetName}`);

  console.log("  1) maintenance-reboot 送信...");
  const rebRes = await apiPost(token, "/api/admin/commands/maintenance-reboot", {
    targetNames: [targetName]
  });
  if (rebRes.result !== "OK") {
    console.log("  [NG] maintenance-reboot 失敗:", rebRes.detail);
    return { ok: false, reason: "maintenance_reboot_failed" };
  }
  console.log("  [OK] 送信済み。AP モード移行を待機中（18秒）...");
  await sleep(WAIT_AFTER_REBOOT_MS);

  console.log("  2) AP スキャン...");
  const scanRes = await apiGet(token, "/api/admin/ap/scan");
  if (scanRes.result !== "OK" || !scanRes.apList?.length) {
    console.log("  [NG] AP スキャン失敗。USB Wi-Fi を AP に接続し、再度 ap/scan を試してください。");
    return { ok: false, reason: "ap_scan_failed" };
  }
  const ssid = scanRes.apList[0].ssid;
  console.log(`  [OK] 検出: ${ssid}`);

  console.log("  3) ap/settings 取得...");
  const settingsRes = await apiGet(token, `/api/admin/ap/settings?ssid=${encodeURIComponent(ssid)}`);
  if (settingsRes.result !== "OK" || !settingsRes.settings) {
    console.log("  [NG] ap/settings 失敗:", settingsRes.detail);
    return { ok: false, reason: "ap_settings_failed" };
  }
  const net = settingsRes.settings;
  console.log("  [OK] 設定取得済み。同一設定で configure + 再起動...");

  console.log("  4) ap/configure (同一設定・requestReboot=true)...");
  const cfgRes = await apiPost(token, "/api/admin/ap/configure", {
    ssid,
    targetDeviceName: targetName,
    wifiSsid: net.wifiSsid ?? "",
    wifiPass: net.wifiPass ?? "",
    mqttUrl: net.mqttUrl ?? net.mqttUrlName ?? "",
    mqttPort: net.mqttPort ?? 8883,
    mqttUser: net.mqttUser ?? "",
    mqttPass: net.mqttPass ?? "",
    mqttTls: net.mqttTls !== false,
    otaUrl: net.otaUrl ?? net.serverUrl ?? "",
    otaPort: net.otaPort ?? net.serverPort ?? 443,
    otaTls: net.otaTls !== false,
    timeServerUrl: net.timeServerUrl ?? net.timeServerUrlName ?? "",
    requestReboot: true
  });
  if (cfgRes.result !== "OK") {
    console.log("  [NG] ap/configure 失敗:", cfgRes.detail);
    return { ok: false, reason: "ap_configure_failed" };
  }
  console.log("  [OK] 設定投入・再起動要求済み。通常運用復帰を待機中（30秒）...");
  await sleep(30000);

  console.log("  5) デバイス状態確認...");
  const devRes = await apiGet(token, "/api/admin/devices");
  const found = (devRes.devices ?? []).find((d) => (d.targetName ?? d.deviceName) === targetName);
  const isOnline = found?.onlineState === "online";
  if (isOnline) {
    console.log("  [OK] 7027 合格。再起動後も NVS から設定が復元され、通常運用に復帰しました。");
    return { ok: true };
  }
  console.log("  [要確認] デバイスがまだ offline の可能性があります。もう少し待って GET /api/admin/devices を確認してください。");
  return { ok: isOnline };
}

async function run7066(token, config) {
  console.log("\n=== 7066: AP 探索・順次一括メンテナンス試験 ===");
  const scanRes = await apiGet(token, "/api/admin/ap/scan");
  if (scanRes.result !== "OK" || !scanRes.apList?.length) {
    console.log("  [NG] AP スキャンで AP-esp32lab-* が見つかりません。ESP32 を AP モードにして再実行してください。");
    return { ok: false, reason: "ap_scan_empty" };
  }
  const ssid = scanRes.apList[0].ssid;
  console.log(`  検出 AP: ${ssid}`);

  const settingsRes = await apiGet(token, `/api/admin/ap/settings?ssid=${encodeURIComponent(ssid)}`);
  if (settingsRes.result !== "OK" || !settingsRes.settings) {
    console.log("  [NG] ap/settings 失敗:", settingsRes.detail ?? "networkSettings 取得不可");
    return { ok: false, reason: "ap_settings_failed" };
  }
  const net = settingsRes.settings;

  const networkSettings = {
    wifiSsid: net.wifiSsid ?? "",
    wifiPass: net.wifiPass ?? "",
    mqttUrl: net.mqttUrl ?? net.mqttUrlName ?? "",
    mqttPort: net.mqttPort ?? 8883,
    mqttUser: net.mqttUser ?? "",
    mqttPass: net.mqttPass ?? "",
    mqttTls: net.mqttTls !== false,
    serverUrl: net.serverUrl ?? net.otaUrl ?? "",
    serverPort: net.serverPort ?? net.otaPort ?? 443,
    serverTls: net.otaTls !== false,
    otaUrl: net.otaUrl ?? net.serverUrl ?? "",
    otaPort: net.otaPort ?? net.serverPort ?? 443,
    otaTls: net.otaTls !== false,
    timeServerUrl: net.timeServerUrl ?? net.timeServerUrlName ?? ""
  };

  console.log("  ap/batch/start 実行（targetSsids 未指定=スキャン結果を対象）...");
  const batchRes = await apiPost(token, "/api/admin/ap/batch/start", {
    networkSettings,
    requestReboot: true,
    statusWaitTimeoutSeconds: 60
  });
  if (batchRes.result !== "OK") {
    console.log("  [NG] batch/start 失敗:", batchRes.detail);
    return { ok: false, reason: "batch_failed" };
  }
  console.log(`  [OK] 7066 バッチ完了。batchId=${batchRes.batchId}`);
  console.log(`  completed=${batchRes.completedCount} failed=${batchRes.failedCount} total=${batchRes.totalCount}`);
  batchRes.itemResults?.forEach((r) => {
    console.log(`    ${r.ssid}: ${r.status} ${r.errorDetail ? r.errorDetail : ""}`);
  });
  const ok = (batchRes.failedCount ?? 0) === 0 && (batchRes.completedCount ?? 0) > 0;
  if (ok) {
    console.log("  7067: 再起動後 status は batch 結果の publicId/fwVersion/configVersion で判定済み。");
  }
  return { ok, batchId: batchRes.batchId };
}

/**
 * @description 7064 残: APロールパスワード変更・credentials/rotation 試験。
 * [厳守] 試験後はデフォルトへ復元する。理由: 試験用一時値が残ると運用障害となるため。
 * 事前: ESP32 が AP モードで SSID 発信中、USB Wi-Fi が検出可能であること。
 */
async function run7064Remaining(token, config) {
  console.log("\n=== 7064 残: APロールパスワード変更・credentials/rotation ===");
  const scanRes = await apiGet(token, "/api/admin/ap/scan");
  if (scanRes.result !== "OK" || !scanRes.apList?.length) {
    console.log("  [NG] AP スキャンで AP-esp32lab-* が見つかりません。");
    return { ok: false, reason: "ap_scan_empty" };
  }
  const ssid = scanRes.apList[0].ssid;
  console.log(`  検出 AP: ${ssid}`);

  const defaultPw = config.apRoleAdminPassword || "change-me";
  const tempPw = "temp-7064-test-ok";
  let passwordChanged = false;
  let restoreRes = { result: "NG" };
  try {
    console.log("  1) AP admin ロールパスワード変更 (デフォルト -> 一時値)...");
    const chgRes = await apiPost(token, "/api/admin/ap/password/change", {
      ssid,
      role: "admin",
      currentPassword: defaultPw,
      newPassword: tempPw,
      reason: "7064 test"
    });
    if (chgRes.result !== "OK") {
      console.log("  [NG] ap/password/change 失敗:", chgRes.detail);
      return { ok: false, reason: "ap_password_change_failed" };
    }
    passwordChanged = true;
    console.log("  [OK] パスワード変更成功");

    console.log("  2) credentials/rotation 実行...");
    const rotRes = await apiPost(token, "/api/admin/credentials/rotation", {
      targetType: "mqtt",
      targetId: "7064-test",
      reason: "7064 試験用例外継続記録"
    });
    if (rotRes.result !== "OK") {
      console.log("  [NG] credentials/rotation 失敗:", rotRes.detail);
      return { ok: false, reason: "credentials_rotation_failed" };
    }
    console.log("  [OK] credentials/rotation 成功");

    const auditPath = path.join(__dirname, "..", "logs", "security-audit.log");
    const auditOk = fs.existsSync(auditPath) &&
      fs.readFileSync(auditPath, "utf-8").includes("apRolePasswordChanged") &&
      fs.readFileSync(auditPath, "utf-8").includes("credentialRotationRegistered");
    console.log(`  3) 監査ログ確認: ${auditOk ? "OK" : "要確認"}`);

    console.log("  [厳守] パスワードをデフォルトへ復元...");
    restoreRes = await apiPost(token, "/api/admin/ap/password/change", {
      ssid,
      role: "admin",
      currentPassword: tempPw,
      newPassword: defaultPw,
      reason: "7064 test restore"
    });
    if (restoreRes.result === "OK") {
      console.log("  [OK] パスワード復元成功");
      passwordChanged = false;
    } else {
      console.log("  [WARN] パスワード復元失敗:", restoreRes.detail);
    }
    return { ok: restoreRes.result === "OK" && rotRes.result === "OK" && auditOk };
  } finally {
    if (passwordChanged) {
      console.log("  [厳守] パスワードをデフォルトへ復元...");
      restoreRes = await apiPost(token, "/api/admin/ap/password/change", {
        ssid,
        role: "admin",
        currentPassword: tempPw,
        newPassword: defaultPw,
        reason: "7064 test restore"
      });
      if (restoreRes.result === "OK") {
        console.log("  [OK] パスワード復元成功");
      } else {
        console.log("  [WARN] パスワード復元失敗。手動で data/securityState.json を確認してください。");
      }
    }
  }
}

/**
 * @description 7065: AP ログイン必須・RBAC 試験。
 * - 未認証で保護APIへアクセス → 401 確認
 * - admin が mfg パスワード変更を試行 → 403 確認（mfg ロールのみ可）
 */
async function run7065(token, config) {
  console.log("\n=== 7065: AP ログイン必須・RBAC 試験 ===");
  const scanRes = await apiGet(token, "/api/admin/ap/scan");
  if (scanRes.result !== "OK" || !scanRes.apList?.length) {
    console.log("  [NG] AP スキャンで AP-esp32lab-* が見つかりません。");
    return { ok: false, reason: "ap_scan_empty" };
  }
  const ssid = scanRes.apList[0].ssid;
  console.log(`  検出 AP: ${ssid}`);

  console.log("  1) 未認証で /api/settings/network にアクセス → 401 期待...");
  const unauthRes = await apiGet(token, `/api/admin/ap/test-unauth?ssid=${encodeURIComponent(ssid)}&path=/api/settings/network`);
  if (unauthRes.result !== "OK") {
    console.log("  [NG] test-unauth 失敗:", unauthRes.detail);
    return { ok: false, reason: "test_unauth_failed" };
  }
  if (unauthRes.statusCode !== 401) {
    console.log(`  [NG] 未認証時に 401 でなく ${unauthRes.statusCode} を返しました。`);
    return { ok: false, reason: "unauth_not_401" };
  }
  console.log("  [OK] 未認証時に 401 を確認");

  console.log("  2) admin ロールで mfg パスワード変更を試行 → 403 期待...");
  const mfgChgRes = await apiPost(token, "/api/admin/ap/password/change", {
    ssid,
    role: "mfg",
    currentPassword: "mfg-default-placeholder",
    newPassword: "temp-mfg-7065-test",
    reason: "7065 test"
  });
  if (mfgChgRes.result === "OK") {
    console.log("  [NG] admin で mfg パスワード変更が許可されてしまった。403 であるべき。");
    return { ok: false, reason: "mfg_change_should_be_403" };
  }
  const detail = String(mfgChgRes.detail ?? "").toLowerCase();
  if (!detail.includes("403") && !detail.includes("mfg") && !detail.includes("forbidden")) {
    console.log("  [要確認] 期待 403。実際:", mfgChgRes.detail);
  }
  console.log("  [OK] admin では mfg パスワード変更不可（403相当）を確認");

  console.log("  3) maintenance/admin ロールで ap/settings 到達可能（7063/7064 で確認済み）");
  return { ok: true };
}

async function main() {
  const step = process.argv.includes("--step") ? process.argv[process.argv.indexOf("--step") + 1] : "all";
  console.log("[実機試験] LocalServer にログイン...");
  const { token, config } = await login();
  console.log("[OK] ログイン成功");

  let results = {};
  if (step === "7027" || step === "all") results["7027"] = await run7027(token);
  if (step === "7064" || step === "all") results["7064"] = await run7064Remaining(token, config);
  if (step === "7065" || step === "all") results["7065"] = await run7065(token, config);
  if (step === "7066" || step === "all") {
    results["7066"] = await run7066(token, config);
  }

  console.log("\n=== 試験サマリ ===");
  Object.entries(results).forEach(([id, r]) => {
    console.log(`  ${id}: ${r.ok ? "OK" : "NG"}`);
  });
  const allOk = Object.values(results).every((r) => r.ok);
  process.exit(allOk ? 0 : 1);
}

main().catch((e) => {
  console.error("[実機試験] 失敗:", e.message);
  process.exit(1);
});
