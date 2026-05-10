/**
 * @file test7042SignedOtaHmacVerification.mjs
 * @description `otaStart` の不正署名拒否を管理者 API 経由で確認する実機試験スクリプト。
 * @details
 * - [重要] 正常系の OTA 実書換えは `7083` を正とし、本スクリプトは「署名改ざん時に OTA が開始されないこと」の確認に専念する。
 * - [厳守] 実鍵はスクリプト外へ取り出さず、`LocalServer` 内で正規署名生成後に 1 文字だけ改ざんした payload を publish する。
 * - [厳守] 成功条件は「publish API が受理される」「対象機の `firmwareVersion` が不変」「`otaUpdatedAt` が不変」の 3 点とする。
 * - [禁止] 署名不一致確認のために暗号化モードを緩めたり、平文 payload を送ったりしない。
 * - [制限事項] `../dist/config.js` を参照するため、事前に `npm run build` が必要。
 */

import {
  apiGetJson,
  apiPostJson,
  getAdminDevices,
  loginAsAdmin,
  writeJsonReport
} from "./testCommon.mjs";

const defaultBaseUrl = "http://127.0.0.1:3100";

/**
 * @typedef {Object} commandLineOptions
 * @property {string} baseUrl LocalServer baseUrl。
 * @property {string | null} targetDeviceName 対象デバイス名。
 * @property {number} observeMilliseconds 改ざん publish 後の待機時間(ms)。
 * @property {number} pollIntervalMilliseconds ポーリング間隔(ms)。
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
    observeMilliseconds: readNumberOption(argv, "--observeMilliseconds", 15000),
    pollIntervalMilliseconds: readNumberOption(argv, "--pollIntervalMilliseconds", 3000)
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
 * @description 待機する。
 * @param {number} milliseconds 待機時間(ms)。
 * @returns {Promise<void>} 完了 Promise。
 */
function sleep(milliseconds) {
  return new Promise((resolvePromise) => {
    setTimeout(resolvePromise, milliseconds);
  });
}

/**
 * @description 対象デバイスを解決する。
 * @param {Array<Record<string, unknown>>} devices デバイス一覧。
 * @param {string | null} requestedTargetDeviceName 指定対象名。
 * @returns {Record<string, unknown>} 対象デバイス状態。
 */
function resolveTargetDevice(devices, requestedTargetDeviceName) {
  if (requestedTargetDeviceName) {
    const matchedDevice = devices.find(
      (deviceItem) => String(deviceItem.targetName ?? deviceItem.deviceName ?? "") === requestedTargetDeviceName
    );
    if (!matchedDevice) {
      throw new Error(`resolveTargetDevice failed. targetDeviceName=${requestedTargetDeviceName} was not found.`);
    }
    return matchedDevice;
  }
  const onlineDevice = devices.find((deviceItem) => String(deviceItem.onlineState ?? "") === "online");
  if (!onlineDevice) {
    throw new Error("resolveTargetDevice failed. online target device was not found.");
  }
  return onlineDevice;
}

/**
 * @description 指定デバイスの最新状態を取得する。
 * @param {string} baseUrl LocalServer baseUrl。
 * @param {string} token 管理者トークン。
 * @param {string} targetDeviceName 対象デバイス名。
 * @returns {Promise<Record<string, unknown>>} 最新状態。
 */
async function getDeviceStateByName(baseUrl, token, targetDeviceName) {
  const responseJson = await apiGetJson(baseUrl, token, "/api/admin/devices");
  const foundDevice = (responseJson.devices ?? []).find(
    (deviceItem) => String(deviceItem.targetName ?? deviceItem.deviceName ?? "") === targetDeviceName
  );
  if (!foundDevice) {
    throw new Error(`getDeviceStateByName failed. targetDeviceName=${targetDeviceName} was not found.`);
  }
  return foundDevice;
}

/**
 * @description 改ざん署名 OTA publish API を呼ぶ。
 * @param {string} baseUrl LocalServer baseUrl。
 * @param {string} token 管理者トークン。
 * @param {string} targetDeviceName 対象デバイス名。
 * @returns {Promise<Record<string, unknown>>} API 応答。
 */
async function publishTamperedOtaStart(baseUrl, token, targetDeviceName) {
  return apiPostJson(baseUrl, token, "/api/admin/tests/ota/tampered-signature", {
    targetDeviceName
  });
}

/**
 * @description 観測期間後のデバイス状態を取得し、不変条件を確認する。
 * @param {string} baseUrl LocalServer baseUrl。
 * @param {string} token 管理者トークン。
 * @param {string} targetDeviceName 対象デバイス名。
 * @param {number} observeMilliseconds 待機時間(ms)。
 * @param {number} pollIntervalMilliseconds ポーリング間隔(ms)。
 * @returns {Promise<Record<string, unknown>>} 観測後状態。
 */
async function observeFinalState(baseUrl, token, targetDeviceName, observeMilliseconds, pollIntervalMilliseconds) {
  const deadlineAt = Date.now() + observeMilliseconds;
  let latestState = await getDeviceStateByName(baseUrl, token, targetDeviceName);
  while (Date.now() < deadlineAt) {
    await apiPostJson(baseUrl, token, "/api/commands/status", {
      targetNames: [targetDeviceName]
    });
    await sleep(pollIntervalMilliseconds);
    latestState = await getDeviceStateByName(baseUrl, token, targetDeviceName);
  }
  return latestState;
}

/**
 * @description メイン処理。
 * @param {commandLineOptions} options 実行オプション。
 * @returns {Promise<void>} 完了 Promise。
 */
async function main(options) {
  const loginResult = await loginAsAdmin(options.baseUrl);
  const devices = await getAdminDevices(options.baseUrl, loginResult.token);
  const targetDevice = resolveTargetDevice(devices, options.targetDeviceName);
  const targetDeviceName = String(targetDevice.targetName ?? targetDevice.deviceName ?? "");
  const initialFirmwareVersion = String(targetDevice.firmwareVersion ?? "").trim();
  const initialOtaUpdatedAt = String(targetDevice.otaUpdatedAt ?? "").trim();
  if (initialFirmwareVersion.length === 0) {
    throw new Error(`main failed. initial firmwareVersion is empty. targetDeviceName=${targetDeviceName}`);
  }

  console.log("[7042] OTA 不正署名拒否試験を開始します。");
  console.log(`  baseUrl=${options.baseUrl}`);
  console.log(`  targetDeviceName=${targetDeviceName}`);
  console.log(`  initialFirmwareVersion=${initialFirmwareVersion}`);
  console.log(`  initialOtaUpdatedAt=${initialOtaUpdatedAt || "(empty)"}`);

  const publishResult = await publishTamperedOtaStart(options.baseUrl, loginResult.token, targetDeviceName);
  const finalDeviceState = await observeFinalState(
    options.baseUrl,
    loginResult.token,
    targetDeviceName,
    options.observeMilliseconds,
    options.pollIntervalMilliseconds
  );
  const finalFirmwareVersion = String(finalDeviceState.firmwareVersion ?? "").trim();
  const finalOtaUpdatedAt = String(finalDeviceState.otaUpdatedAt ?? "").trim();

  if (finalFirmwareVersion !== initialFirmwareVersion) {
    throw new Error(
      `main failed. firmwareVersion changed unexpectedly. targetDeviceName=${targetDeviceName} initial=${initialFirmwareVersion} final=${finalFirmwareVersion}`
    );
  }
  if (finalOtaUpdatedAt !== initialOtaUpdatedAt) {
    throw new Error(
      `main failed. otaUpdatedAt changed unexpectedly. targetDeviceName=${targetDeviceName} initial=${initialOtaUpdatedAt} final=${finalOtaUpdatedAt}`
    );
  }

  const reportObject = {
    testId: "7042",
    startedAt: new Date().toISOString(),
    options,
    targetDeviceName,
    initialFirmwareVersion,
    initialOtaUpdatedAt,
    publishResult,
    finalDeviceState,
    result: "OK",
    detail: "tampered otaStart was published but firmwareVersion and otaUpdatedAt remained unchanged.",
    completedAt: new Date().toISOString()
  };
  const reportFilePath = writeJsonReport("test7042", reportObject);
  console.log("[7042] 試験完了: OK");
  console.log(`  reportFilePath=${reportFilePath}`);
}

main(parseArguments(process.argv.slice(2))).catch((mainError) => {
  console.error(`[7042] 試験失敗: ${String(mainError?.message ?? mainError)}`);
  process.exit(1);
});
