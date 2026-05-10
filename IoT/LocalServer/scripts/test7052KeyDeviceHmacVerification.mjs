/**
 * @file test7052KeyDeviceHmacVerification.mjs
 * @description `keyDeviceSet` の署名付き設定変更と不正署名拒否を確認する実機試験スクリプト。
 * @details
 * - [重要] まず正規の `k-device` を対象機へ再投入し、`securePing` が成功する基準状態を確認する。
 * - [厳守] 不正署名試験では `LocalServer` 内で正規署名を生成してから 1 文字だけ改ざんし、実鍵をスクリプト外へ出さない。
 * - [厳守] 成功条件は「正規 push 後の `securePing` 成功」「改ざん `keyDeviceSet` publish 後も `securePing` 成功」の 2 点とする。
 * - [禁止] 平文 payload の直接送信や暗号化モード緩和で試験を成立させない。
 * - [制限事項] `../dist/config.js` を参照するため、事前に `npm run build` が必要。
 */

import {
  apiPostJson,
  getAdminDevices,
  loginAsAdmin,
  resolveTargetDeviceName,
  sleep,
  writeJsonReport
} from "./testCommon.mjs";

const defaultBaseUrl = "http://127.0.0.1:3100";

/**
 * @typedef {Object} commandLineOptions
 * @property {string} baseUrl LocalServer baseUrl。
 * @property {string | null} targetDeviceName 対象デバイス名。
 * @property {number} observeMilliseconds 改ざん publish 後の待機時間(ms)。
 */

/**
 * @description 引数を解析する。
 * @param {string[]} argv コマンドライン引数。
 * @returns {commandLineOptions} 解析結果。
 */
function parseArguments(argv) {
  return {
    baseUrl: readStringOption(argv, "--baseUrl", defaultBaseUrl),
    targetDeviceName: readNullableStringOption(argv, "--targetDeviceName"),
    observeMilliseconds: readNumberOption(argv, "--observeMilliseconds", 3000)
  };
}

/**
 * @description 文字列オプションを読む。
 * @param {string[]} argv 引数配列。
 * @param {string} optionName オプション名。
 * @param {string} defaultValue 既定値。
 * @returns {string} 読み取った値。
 */
function readStringOption(argv, optionName, defaultValue) {
  const optionIndex = argv.indexOf(optionName);
  if (optionIndex < 0 || !argv[optionIndex + 1]) {
    return defaultValue;
  }
  return String(argv[optionIndex + 1]);
}

/**
 * @description 任意文字列オプションを読む。
 * @param {string[]} argv 引数配列。
 * @param {string} optionName オプション名。
 * @returns {string | null} 読み取った値。
 */
function readNullableStringOption(argv, optionName) {
  const optionIndex = argv.indexOf(optionName);
  if (optionIndex < 0 || !argv[optionIndex + 1]) {
    return null;
  }
  const optionValue = String(argv[optionIndex + 1]).trim();
  return optionValue.length > 0 ? optionValue : null;
}

/**
 * @description 数値オプションを読む。
 * @param {string[]} argv 引数配列。
 * @param {string} optionName オプション名。
 * @param {number} defaultValue 既定値。
 * @returns {number} 数値化後の値。
 */
function readNumberOption(argv, optionName, defaultValue) {
  const optionValue = readNullableStringOption(argv, optionName);
  if (!optionValue) {
    return defaultValue;
  }
  const parsedValue = Number(optionValue);
  if (!Number.isFinite(parsedValue) || parsedValue <= 0) {
    throw new Error(`readNumberOption failed. optionName=${optionName} optionValue=${optionValue}`);
  }
  return parsedValue;
}

/**
 * @description 正規の k-device を対象機へ再投入する。
 * @param {string} baseUrl LocalServer baseUrl。
 * @param {string} token 管理者トークン。
 * @param {string} targetDeviceName 対象デバイス名。
 * @returns {Promise<Record<string, unknown>>} API 応答。
 */
async function pushCurrentKeyDevice(baseUrl, token, targetDeviceName) {
  return apiPostJson(baseUrl, token, "/api/admin/keys/k-device/issue", {
    targetDeviceName,
    pushToDevice: true
  });
}

/**
 * @description securePing で現在の `k-device` 整合を確認する。
 * @param {string} baseUrl LocalServer baseUrl。
 * @param {string} token 管理者トークン。
 * @param {string} targetDeviceName 対象デバイス名。
 * @param {string} plainText 試験文言。
 * @returns {Promise<Record<string, unknown>>} API 応答。
 */
async function runSecurePing(baseUrl, token, targetDeviceName, plainText) {
  return apiPostJson(baseUrl, token, "/api/admin/commands/secure-ping", {
    targetDeviceName,
    plainText,
    timeoutMs: 15000
  });
}

/**
 * @description 改ざん署名付き `keyDeviceSet` を publish する。
 * @param {string} baseUrl LocalServer baseUrl。
 * @param {string} token 管理者トークン。
 * @param {string} targetDeviceName 対象デバイス名。
 * @returns {Promise<Record<string, unknown>>} API 応答。
 */
async function publishTamperedKeyDeviceSet(baseUrl, token, targetDeviceName) {
  return apiPostJson(baseUrl, token, "/api/admin/tests/settings/key-device/tampered-signature", {
    targetDeviceName
  });
}

/**
 * @description メイン処理。
 * @param {commandLineOptions} options 実行オプション。
 * @returns {Promise<void>} 完了 Promise。
 */
async function main(options) {
  const loginResult = await loginAsAdmin(options.baseUrl);
  const devices = await getAdminDevices(options.baseUrl, loginResult.token);
  const targetDeviceName = resolveTargetDeviceName(devices, options.targetDeviceName);

  console.log("[7052] keyDeviceSet 署名検証試験を開始します。");
  console.log(`  baseUrl=${options.baseUrl}`);
  console.log(`  targetDeviceName=${targetDeviceName}`);

  const pushResult = await pushCurrentKeyDevice(options.baseUrl, loginResult.token, targetDeviceName);
  console.log(`[7052] 正規 keyDeviceSet publish 完了。keyFingerprint=${pushResult.keyFingerprint ?? "(unknown)"}`);
  await sleep(2000);

  const baselineSecurePingResult = await runSecurePing(
    options.baseUrl,
    loginResult.token,
    targetDeviceName,
    "baseline secure ping before tampered keyDeviceSet"
  );
  console.log("[7052] baseline securePing 成功。");

  const tamperedPublishResult = await publishTamperedKeyDeviceSet(options.baseUrl, loginResult.token, targetDeviceName);
  console.log(
    `[7052] 改ざん keyDeviceSet publish 完了。tamperedKeyDeviceFingerprint=${tamperedPublishResult.tamperedKeyDeviceFingerprint ?? "(unknown)"}`
  );
  await sleep(options.observeMilliseconds);

  const finalSecurePingResult = await runSecurePing(
    options.baseUrl,
    loginResult.token,
    targetDeviceName,
    "secure ping after tampered keyDeviceSet"
  );
  console.log("[7052] 改ざん publish 後 securePing 成功。");

  const reportObject = {
    testId: "7052",
    startedAt: new Date().toISOString(),
    options,
    targetDeviceName,
    pushResult,
    baselineSecurePingResult,
    tamperedPublishResult,
    finalSecurePingResult,
    result: "OK",
    detail:
      "Valid keyDeviceSet push succeeded, tampered keyDeviceSet was published, and securePing remained successful after the tampered publish.",
    completedAt: new Date().toISOString()
  };
  const reportFilePath = writeJsonReport("test7052", reportObject);
  console.log("[7052] 試験完了: OK");
  console.log(`  reportFilePath=${reportFilePath}`);
}

main(parseArguments(process.argv.slice(2))).catch((mainError) => {
  console.error(`[7052] 試験失敗: ${String(mainError?.message ?? mainError)}`);
  process.exit(1);
});
