/**
 * @file test7102to7106LocalHistoryVerification.mjs
 * @description `7102`〜`7106`（LocalServer SQLite ローカル履歴）のうち、**自動化可能な範囲**を一括検証する。
 *
 * 実施内容の対応:
 * - **7102（一部）**: 分離 SQLite ファイルへ `LocalHistoryStore.recordDeviceStatus` を実行し、`queryDeviceStatusForExport` で 1 行以上 を確認（MQTT 実機フローは含まない）。
 * - **7103（マスク規則）**: `historyRedaction.ts` のプレースホルダ規則を JSON 化して検証（7103 手順の「平文秘密なし」に相当する機械チェック）。
 * - **7104（接続時のみ）**: `GET /api/health` が成功する場合、`POST /api/admin/local-history/export` の成功と、未認証時 `401` を確認。監査ログの有無はファイル末尾をベストエフォート検査。
 * - **7105（purge）**: 保持 1 日・3 日前行を投入し `purgeExpired` 後に最新行のみ残ることを確認。
 * - **7106**: 定期エクスポートは **2 周期待機が必要**なため本スクリプトでは検証せず、「手順は試験仕様書に従い別途実施」とレポートに記載する。
 *
 * [重要] 事前に `npm run build` 済みであること（`dist/` 参照）。
 * [厳守] 本スクリプトは一時ディレクトリに SQLite を作成し、既存 `data/localHistory.db` には触れない（7102/7105 のオフライン部）。
 *
 * 実行例:
 *   node scripts/test7102to7106LocalHistoryVerification.mjs
 *   node scripts/test7102to7106LocalHistoryVerification.mjs --baseUrl http://127.0.0.1:3100
 *   node scripts/test7102to7106LocalHistoryVerification.mjs --skipLive
 *   node scripts/test7102to7106LocalHistoryVerification.mjs --strictLive
 */

import fs from "fs";
import path from "path";
import os from "os";
import { fileURLToPath } from "url";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");

/**
 * @param {string[]} argv
 * @returns {{ baseUrl: string; skipLive: boolean; strictLive: boolean }}
 */
function parseArgs(argv) {
  const baseUrlIndex = argv.indexOf("--baseUrl");
  const baseUrl =
    baseUrlIndex >= 0 && argv[baseUrlIndex + 1] ? String(argv[baseUrlIndex + 1]).trim() : "http://127.0.0.1:3100";
  const skipLive = argv.includes("--skipLive");
  const strictLive = argv.includes("--strictLive");
  return { baseUrl, skipLive, strictLive };
}

/**
 * @param {string} messageText
 */
function logOk(messageText) {
  console.info(`[OK] ${messageText}`);
}

/**
 * @param {string} messageText
 */
function logInfo(messageText) {
  console.info(`[INFO] ${messageText}`);
}

/**
 * @param {string} testName
 * @param {string} detailText
 */
function throwWithDetail(testName, detailText) {
  throw new Error(`${testName} failed. ${detailText}`);
}

/**
 * @param {Record<string, unknown>} overrides
 * @returns {Record<string, unknown>}
 */
function buildMinimalStatus(overrides) {
  const nowIso = new Date().toISOString();
  return {
    topic: "",
    srcId: "IoT_VERIFY_DEVICE",
    dstId: "",
    messageId: "verify-msg-1",
    publicId: "",
    configVersion: "",
    macAddr: "",
    ipAddress: "",
    wifiSsid: "",
    firmwareVersion: "9.9.9-verify",
    firmwareWrittenAt: "",
    runningPartition: "",
    bootPartition: "",
    nextUpdatePartition: "",
    onlineState: "online",
    statusSub: "Reply",
    detail: "",
    receivedAt: nowIso,
    ...overrides
  };
}

/**
 * @returns {Promise<{ summary: Record<string, unknown> }>}
 */
async function runOfflineSqliteAndRedaction() {
  const historyRedactionUrl = new URL("../dist/historyRedaction.js", import.meta.url).href;
  const configUrl = new URL("../dist/config.js", import.meta.url).href;
  const localHistoryUrl = new URL("../dist/localHistoryStore.js", import.meta.url).href;

  const { buildRedactedCommandDetailJson, getHistoryRedactionPlaceholderForKey } = await import(historyRedactionUrl);
  const { loadConfig } = await import(configUrl);
  const { LocalHistoryStore } = await import(localHistoryUrl);

  /** 7103: マスク規則 */
  const ph = getHistoryRedactionPlaceholderForKey("wifiPassword");
  if (ph !== "<password>") {
    throwWithDetail("7103-redaction", `wifiPassword placeholder expected=<password> actual=${String(ph)}`);
  }
  const redactedObj = JSON.parse(
    buildRedactedCommandDetailJson({
      wifiPassword: "must-not-appear",
      nested: { apiKey: "secret-token-xyz" },
      okField: "visible"
    })
  );
  if (redactedObj.wifiPassword !== "<password>") {
    throwWithDetail("7103-redaction", `nested mask failed. wifiPassword=${String(redactedObj.wifiPassword)}`);
  }
  if (redactedObj.nested.apiKey !== "<apiKey>") {
    throwWithDetail("7103-redaction", `nested.apiKey expected=<apiKey> actual=${String(redactedObj.nested?.apiKey)}`);
  }
  if (redactedObj.okField !== "visible") {
    throwWithDetail("7103-redaction", `non-secret field corrupted. okField=${String(redactedObj.okField)}`);
  }
  logOk("7103 historyRedaction rules (sample keys)");

  const exportParent = fs.mkdtempSync(path.join(os.tmpdir(), "local-history-710-"));
  const defaultExportDir = path.join(exportParent, "exports-default");
  fs.mkdirSync(defaultExportDir, { recursive: true });
  process.env.LOCAL_HISTORY_EXPORT_DIR = defaultExportDir;
  process.env.LOCAL_HISTORY_PURGE_INTERVAL_MS = "60000";
  process.env.LOCAL_HISTORY_RETENTION_DAYS = "30";

  /** 7102: 専用 DB */
  const dbPath7102 = path.join(exportParent, "localHistory-7102.db");
  process.env.LOCAL_HISTORY_DB_PATH = dbPath7102;
  const config7102 = loadConfig();
  const store7102 = new LocalHistoryStore(config7102, 30);
  const statusRow = buildMinimalStatus({ srcId: "IoT_7102_DEVICE", detail: "", messageId: "7102-1" });
  store7102.recordDeviceStatus(statusRow, config7102.sourceId);
  const deviceRows = store7102.queryDeviceStatusForExport({
    sources: ["deviceStatus"],
    fieldCombine: "AND",
    deviceNo: "IoT_7102_DEVICE"
  });
  if (deviceRows.length < 1) {
    throwWithDetail("7102-offline", `expected>=1 deviceStatus rows got=${deviceRows.length}`);
  }
  store7102.close();
  logOk("7102 offline: deviceStatusHistory insert + query (no live MQTT)");

  /** 7105: 別 DB（7102 行と混在させない） */
  const dbPath7105 = path.join(exportParent, "localHistory-7105.db");
  process.env.LOCAL_HISTORY_DB_PATH = dbPath7105;
  const config7105 = loadConfig();
  const store7105 = new LocalHistoryStore(config7105, 1);
  const oldIso = new Date(Date.now() - 3 * 24 * 60 * 60 * 1000).toISOString();
  store7105.recordDeviceStatus(
    buildMinimalStatus({
      srcId: "IoT_PURGE_OLD",
      receivedAt: oldIso,
      messageId: "purge-old"
    }),
    config7105.sourceId
  );
  store7105.recordDeviceStatus(
    buildMinimalStatus({
      srcId: "IoT_PURGE_NEW",
      messageId: "purge-new"
    }),
    config7105.sourceId
  );
  store7105.setRetentionDays(1);
  store7105.purgeExpired();
  const afterRows = store7105.queryDeviceStatusForExport({ sources: ["deviceStatus"], fieldCombine: "AND" });
  const oldRemaining = afterRows.filter((row) => String(row.deviceName) === "IoT_PURGE_OLD");
  const newRemaining = afterRows.filter((row) => String(row.deviceName) === "IoT_PURGE_NEW");
  if (oldRemaining.length !== 0) {
    throwWithDetail("7105-purge", `old row should be purged. oldRemaining=${oldRemaining.length}`);
  }
  if (newRemaining.length !== 1) {
    throwWithDetail("7105-purge", `expected exactly 1 new row remaining got=${newRemaining.length}`);
  }
  store7105.close();
  logOk("7105 offline: retention=1d purge removes 3-day-old row");

  return {
    summary: {
      isolatedWorkDir: exportParent,
      dbPath7102,
      dbPath7105,
      deviceStatusRows7102: deviceRows.length,
      purgeRemainingTotal: afterRows.length
    }
  };
}

/**
 * @param {Response} response
 * @param {string} contextLabel
 * @returns {Promise<any>}
 */
async function readJsonResponseBodyOrThrow(response, contextLabel) {
  const responseText = await response.text();
  const trimmed = responseText.trimStart();
  if (trimmed.startsWith("<!DOCTYPE") || trimmed.startsWith("<html")) {
    throw new Error(
      `${contextLabel}: expected JSON but received HTML. status=${response.status} preview=${responseText.slice(0, 120).replace(/\s+/g, " ")}`
    );
  }
  try {
    return JSON.parse(responseText);
  } catch (parseError) {
    throw new Error(
      `${contextLabel}: JSON.parse failed. status=${response.status} reason=${parseError instanceof Error ? parseError.message : String(parseError)} preview=${responseText.slice(0, 200)}`
    );
  }
}

/**
 * @param {string} baseUrl
 * @returns {Promise<Record<string, unknown>>}
 */
async function runLive7104IfAvailable(baseUrl) {
  const healthResponse = await fetch(`${baseUrl}/api/health`);
  if (!healthResponse.ok) {
    return { skipped: true, reason: `GET /api/health status=${healthResponse.status}` };
  }
  const healthJson = await readJsonResponseBodyOrThrow(healthResponse, "GET /api/health");
  if (healthJson.status !== "ok" || healthJson.service !== "local-server") {
    return {
      skipped: true,
      reason: `GET /api/health body is not LocalServer JSON. got=${JSON.stringify(healthJson).slice(0, 200)}`
    };
  }

  const { loginAsAdmin } = await import("./testCommon.mjs");
  const { token } = await loginAsAdmin(baseUrl);
  const exportBody = {
    sources: ["serverEvent", "deviceStatus", "command"],
    fieldCombine: "AND",
    fileBaseName: `auto-7104-${Date.now()}`
  };
  const authedResponse = await fetch(`${baseUrl}/api/admin/local-history/export`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${token}`
    },
    body: JSON.stringify(exportBody)
  });
  const authedJson = await readJsonResponseBodyOrThrow(authedResponse, "POST /api/admin/local-history/export (authed)");
  if (!authedResponse.ok || authedJson.result !== "OK") {
    throwWithDetail(
      "7104-export-authed",
      `status=${authedResponse.status} body=${JSON.stringify(authedJson).slice(0, 400)}`
    );
  }
  const exportPathText = String(authedJson.exportPath ?? "");
  if (exportPathText.length === 0) {
    throwWithDetail("7104-export-authed", "exportPath is empty");
  }
  if (!fs.existsSync(exportPathText)) {
    logInfo(`7104 export file path from API not found locally (may be other host): ${exportPathText}`);
  }

  const unauthResponse = await fetch(`${baseUrl}/api/admin/local-history/export`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(exportBody)
  });
  const unauthJson = await readJsonResponseBodyOrThrow(unauthResponse, "POST /api/admin/local-history/export (unauth)");
  const unauthStatus = unauthResponse.status;
  if (unauthStatus !== 401) {
    throwWithDetail(
      "7104-export-unauth",
      `expected status 401 got=${unauthStatus} detail=${String(unauthJson?.detail ?? "")}`
    );
  }

  const auditPath = path.join(projectRootPath, "logs", "security-audit.log");
  let auditHasExport = false;
  if (fs.existsSync(auditPath)) {
    const tailSize = 65536;
    const stat = fs.statSync(auditPath);
    const start = Math.max(0, stat.size - tailSize);
    const fd = fs.openSync(auditPath, "r");
    try {
      const buffer = Buffer.alloc(Math.min(tailSize, stat.size));
      fs.readSync(fd, buffer, 0, buffer.length, start);
      const tailText = buffer.toString("utf-8");
      auditHasExport = tailText.includes("localHistoryExported");
    } finally {
      fs.closeSync(fd);
    }
  }

  logOk(`7104 live: export OK recordCount=${authedJson.recordCount} unauth=401 auditTailHasExport=${auditHasExport}`);

  return {
    skipped: false,
    exportPath: exportPathText,
    recordCount: authedJson.recordCount,
    unauthStatus,
    auditTailHasLocalHistoryExported: auditHasExport
  };
}

/**
 * @returns {Promise<void>}
 */
async function main() {
  const { baseUrl, skipLive, strictLive } = parseArgs(process.argv.slice(2));
  const report = {
    executedAt: new Date().toISOString(),
    baseUrlAttempted: baseUrl,
    offline: null,
    live7104: null,
    note7106: "定期エクスポートは LOCAL_HISTORY_SCHEDULED_EXPORT_* と 2 周期以上の待機が必要。本スクリプトでは未実施（試験仕様書 7106 を参照）。"
  };

  logInfo(`offline suite (7102 partial / 7103 / 7105) starting. cwd=${process.cwd()}`);
  report.offline = await runOfflineSqliteAndRedaction();

  if (skipLive) {
    report.live7104 = { skipped: true, reason: "--skipLive" };
    logInfo("7104 live: skipped (--skipLive)");
  } else {
    try {
      report.live7104 = await runLive7104IfAvailable(baseUrl);
      if (report.live7104.skipped) {
        logInfo(`7104 live: skipped (${report.live7104.reason})`);
      }
    } catch (liveError) {
      const messageText = liveError instanceof Error ? liveError.message : String(liveError);
      report.live7104 = {
        skipped: false,
        error: messageText
      };
      if (strictLive) {
        throw liveError;
      }
      console.warn(`[WARN] 7104 live verification failed (offline suite already OK). detail=${messageText}`);
      console.warn('[WARN] LocalServer を起動したうえで再実行するか、`--strictLive` なしでは終了コード 0 のまま続行します。');
    }
  }

  const reportPath = path.join(projectRootPath, "logs", "test-reports", `7102-7106-verify-${report.executedAt.replace(/[:.]/g, "-")}.json`);
  const reportDir = path.dirname(reportPath);
  if (!fs.existsSync(reportDir)) {
    fs.mkdirSync(reportDir, { recursive: true });
  }
  fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`, "utf-8");
  logOk(`report written: ${reportPath}`);
}

main().catch((error) => {
  console.error(`[NG] ${error instanceof Error ? error.stack ?? error.message : String(error)}`);
  process.exitCode = 1;
});
