/**
 * @file testCommon.mjs
 * @description 7083/7084 系の実機試験で共通利用する helper 群。
 *
 * 主仕様:
 * - LocalServer 管理者ログイン
 * - 管理API / workflow API の JSON 呼出し
 * - workflow 状態 polling
 * - device 状態 polling
 * - AP メンテナンス API ログイン
 * - 試験レポート JSON 保存
 *
 * [重要] 試験スクリプト側で raw `k-device`、ECDH 共有秘密、`k-pairing-session` を扱わない。
 * [厳守] エラー時は関数名、引数、HTTP status、応答抜粋をできるだけ含めて例外化する。
 * [制限事項] `../dist/config.js` を参照するため、事前に `npm run build` が必要。
 */

import fs from "fs";
import path from "path";
import { fileURLToPath, pathToFileURL } from "url";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");
const reportsDirectoryPath = path.join(projectRootPath, "logs", "test-reports");

/**
 * @returns {Promise<{ loadConfig: () => any }>}
 */
export async function loadBuiltConfigModule() {
  const configModulePath = path.join(projectRootPath, "dist", "config.js");
  if (!fs.existsSync(configModulePath)) {
    throw new Error(
      `loadBuiltConfigModule failed. dist/config.js not found. projectRootPath=${projectRootPath} commandHint="npm run build"`
    );
  }
  const configModuleUrl = pathToFileURL(configModulePath).href;
  return import(configModuleUrl);
}

/**
 * @param {string} baseUrl
 * @returns {Promise<{ token: string; config: any }>}
 */
export async function loginAsAdmin(baseUrl) {
  const configModule = await loadBuiltConfigModule();
  const config = configModule.loadConfig();
  const requestBody = {
    username: config.adminUsername,
    password: config.adminPassword
  };
  const response = await fetch(`${baseUrl}/api/admin/auth/login`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(requestBody)
  });
  const responseText = await response.text();
  const responseJson = parseJsonResponseText("loginAsAdmin", responseText, {
    baseUrl,
    requestBody,
    status: response.status
  });
  if (!response.ok || responseJson.result !== "OK" || !responseJson.token) {
    throw new Error(
      `loginAsAdmin failed. baseUrl=${baseUrl} status=${response.status} result=${String(responseJson.result)} detail=${String(responseJson.detail ?? "")}`
    );
  }
  return {
    token: String(responseJson.token),
    config
  };
}

/**
 * @param {any} config
 * @returns {{ apHttpBaseUrl: string; apRoleAdminUsername: string; apRoleAdminPassword: string }}
 */
function resolveApAdminConfig(config) {
  const apHttpBaseUrl = String(config.apHttpBaseUrl ?? "").trim();
  const apRoleAdminUsername = String(config.apRoleAdminUsername ?? "").trim();
  const apRoleAdminPassword = String(config.apRoleAdminPassword ?? "");
  if (apHttpBaseUrl.length === 0 || apRoleAdminUsername.length === 0 || apRoleAdminPassword.length === 0) {
    throw new Error(
      `resolveApAdminConfig failed. ap config is incomplete. apHttpBaseUrl=${apHttpBaseUrl} apRoleAdminUsername=${apRoleAdminUsername}`
    );
  }
  return { apHttpBaseUrl, apRoleAdminUsername, apRoleAdminPassword };
}

/**
 * @returns {Promise<{ token: string; apHttpBaseUrl: string; config: any }>}
 */
export async function loginAsMaintenanceApAdmin() {
  const configModule = await loadBuiltConfigModule();
  const config = configModule.loadConfig();
  const { apHttpBaseUrl, apRoleAdminUsername, apRoleAdminPassword } = resolveApAdminConfig(config);
  const loginResult = await loginToMaintenanceAp(apHttpBaseUrl, apRoleAdminUsername, apRoleAdminPassword);
  return {
    ...loginResult,
    config
  };
}

/**
 * @param {string} apHttpBaseUrl
 * @param {string} username
 * @param {string} password
 * @returns {Promise<{ token: string; apHttpBaseUrl: string }>}
 */
export async function loginToMaintenanceAp(apHttpBaseUrl, username, password) {
  try {
    const response = await fetch(`${apHttpBaseUrl}/api/auth/login`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        username,
        password
      })
    });
    const responseText = await response.text();
    const responseJson = parseJsonResponseText("loginToMaintenanceAp", responseText, {
      apHttpBaseUrl,
      status: response.status
    });
    if (!response.ok || responseJson.result !== "OK" || !responseJson.token) {
      throw new Error(
        `loginToMaintenanceAp failed. apHttpBaseUrl=${apHttpBaseUrl} username=${String(username)} status=${response.status} result=${String(
          responseJson.result
        )} detail=${String(responseJson.detail ?? "")}`
      );
    }
    return {
      token: String(responseJson.token),
      apHttpBaseUrl
    };
  } catch (error) {
    const causeMessage = String(error?.cause?.message ?? "");
    const causeCode = String(error?.cause?.code ?? "");
    const errorName = String(error?.name ?? "");
    throw new Error(
      `loginToMaintenanceAp failed. apHttpBaseUrl=${apHttpBaseUrl} username=${String(username)} errorName=${errorName} detail=${String(
        error?.message ?? error
      )} causeCode=${causeCode} causeMessage=${causeMessage}`
    );
  }
}

/**
 * @param {string} functionName
 * @param {string} responseText
 * @param {Record<string, unknown>} context
 * @returns {any}
 */
export function parseJsonResponseText(functionName, responseText, context) {
  try {
    return JSON.parse(responseText);
  } catch (parseError) {
    throw new Error(
      `${functionName} failed. JSON parse error=${String(parseError)} context=${JSON.stringify(context)} bodyPreview=${responseText.slice(0, 240)}`
    );
  }
}

/**
 * @param {string} token
 * @returns {{ Authorization: string }}
 */
export function createAuthorizationHeader(token) {
  return {
    Authorization: `Bearer ${token}`
  };
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} apiPath
 * @returns {Promise<any>}
 */
export async function apiGetJson(baseUrl, token, apiPath) {
  const response = await fetch(`${baseUrl}${apiPath}`, {
    method: "GET",
    headers: {
      ...createAuthorizationHeader(token)
    }
  });
  const responseText = await response.text();
  const responseJson = parseJsonResponseText("apiGetJson", responseText, {
    baseUrl,
    apiPath,
    status: response.status
  });
  if (!response.ok) {
    throw new Error(
      `apiGetJson failed. baseUrl=${baseUrl} apiPath=${apiPath} status=${response.status} detail=${String(responseJson.detail ?? "")}`
    );
  }
  return responseJson;
}

/**
 * @param {string} baseUrl
 * @param {string} apiPath
 * @returns {Promise<any>}
 */
export async function publicGetJson(baseUrl, apiPath) {
  const response = await fetch(`${baseUrl}${apiPath}`, {
    method: "GET"
  });
  const responseText = await response.text();
  const responseJson = parseJsonResponseText("publicGetJson", responseText, {
    baseUrl,
    apiPath,
    status: response.status
  });
  if (!response.ok) {
    throw new Error(
      `publicGetJson failed. baseUrl=${baseUrl} apiPath=${apiPath} status=${response.status} detail=${String(responseJson.detail ?? "")}`
    );
  }
  return responseJson;
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} apiPath
 * @param {any} requestBody
 * @returns {Promise<any>}
 */
export async function apiPostJson(baseUrl, token, apiPath, requestBody) {
  const response = await fetch(`${baseUrl}${apiPath}`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      ...createAuthorizationHeader(token)
    },
    body: JSON.stringify(requestBody ?? {})
  });
  const responseText = await response.text();
  const responseJson = parseJsonResponseText("apiPostJson", responseText, {
    baseUrl,
    apiPath,
    status: response.status,
    requestBody
  });
  if (!response.ok) {
    throw new Error(
      `apiPostJson failed. baseUrl=${baseUrl} apiPath=${apiPath} status=${response.status} detail=${String(responseJson.detail ?? "")}`
    );
  }
  return responseJson;
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} apiPath
 * @returns {Promise<any>}
 */
export async function apiGetJsonWithBearer(baseUrl, token, apiPath) {
  const response = await fetch(`${baseUrl}${apiPath}`, {
    method: "GET",
    headers: {
      Authorization: `Bearer ${token}`
    }
  });
  const responseText = await response.text();
  const responseJson = parseJsonResponseText("apiGetJsonWithBearer", responseText, {
    baseUrl,
    apiPath,
    status: response.status
  });
  if (!response.ok) {
    throw new Error(
      `apiGetJsonWithBearer failed. baseUrl=${baseUrl} apiPath=${apiPath} status=${response.status} detail=${String(
        responseJson.detail ?? ""
      )}`
    );
  }
  return responseJson;
}

/**
 * @param {string} apHttpBaseUrl
 * @param {string} token
 * @returns {Promise<any>}
 */
export async function getMaintenanceApPairingState(apHttpBaseUrl, token) {
  return apiGetJsonWithBearer(apHttpBaseUrl, token, "/api/pairing/state");
}

/**
 * @param {string} apHttpBaseUrl
 * @param {string} token
 * @returns {Promise<any>}
 */
export async function getMaintenanceApProductionState(apHttpBaseUrl, token) {
  return apiGetJsonWithBearer(apHttpBaseUrl, token, "/api/production/state");
}

/**
 * @param {number} waitMilliseconds
 * @returns {Promise<void>}
 */
export async function sleep(waitMilliseconds) {
  await new Promise((resolvePromise) => setTimeout(resolvePromise, waitMilliseconds));
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @returns {Promise<any[]>}
 */
export async function getAdminDevices(baseUrl, token) {
  const devicesResponse = await apiGetJson(baseUrl, token, "/api/admin/devices");
  return Array.isArray(devicesResponse.devices) ? devicesResponse.devices : [];
}

/**
 * @param {any[]} devices
 * @param {string | null} requestedTargetDeviceName
 * @returns {string}
 */
export function resolveTargetDeviceName(devices, requestedTargetDeviceName) {
  const normalizedRequestedTargetDeviceName = (requestedTargetDeviceName ?? "").trim();
  if (normalizedRequestedTargetDeviceName.length > 0) {
    return normalizedRequestedTargetDeviceName;
  }
  const onlineDevice = devices.find((deviceItem) => deviceItem.onlineState === "online");
  const resolvedTargetDeviceName = onlineDevice?.targetName ?? onlineDevice?.deviceName ?? "";
  if (!resolvedTargetDeviceName) {
    throw new Error(
      `resolveTargetDeviceName failed. requestedTargetDeviceName=${String(requestedTargetDeviceName)} onlineDevice is not found.`
    );
  }
  return String(resolvedTargetDeviceName);
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} workflowId
 * @param {number} timeoutMilliseconds
 * @param {number} pollIntervalMilliseconds
 * @returns {Promise<{ workflow: any; workflowHistory: any[] }>}
 */
export async function pollWorkflowUntilTerminal(baseUrl, token, workflowId, timeoutMilliseconds, pollIntervalMilliseconds) {
  const startedAtMilliseconds = Date.now();
  const workflowHistory = [];
  while (Date.now() - startedAtMilliseconds <= timeoutMilliseconds) {
    const workflowResponse = await apiGetJson(baseUrl, token, `/api/workflows/${encodeURIComponent(workflowId)}`);
    const workflow = workflowResponse.workflow ?? {};
    workflowHistory.push({
      at: new Date().toISOString(),
      state: workflow.state ?? "",
      result: workflow.result ?? "",
      detail: workflow.detail ?? "",
      errorSummary: workflow.errorSummary ?? ""
    });
    const currentState = String(workflow.state ?? "");
    if (currentState === "completed" || currentState === "failed") {
      return { workflow, workflowHistory };
    }
    await sleep(pollIntervalMilliseconds);
  }
  throw new Error(
    `pollWorkflowUntilTerminal failed. workflowId=${workflowId} timeoutMilliseconds=${timeoutMilliseconds} lastState=${String(workflowHistory.at(-1)?.state ?? "")}`
  );
}

/**
 * @param {string} baseUrl
 * @param {string} token
 * @param {string} targetDeviceName
 * @param {(device: any) => boolean} predicateFunction
 * @param {number} timeoutMilliseconds
 * @param {number} pollIntervalMilliseconds
 * @returns {Promise<any>}
 */
export async function pollDeviceUntil(baseUrl, token, targetDeviceName, predicateFunction, timeoutMilliseconds, pollIntervalMilliseconds) {
  const startedAtMilliseconds = Date.now();
  while (Date.now() - startedAtMilliseconds <= timeoutMilliseconds) {
    const devices = await getAdminDevices(baseUrl, token);
    const device = devices.find((deviceItem) => String(deviceItem.targetName ?? deviceItem.deviceName ?? "") === targetDeviceName);
    if (device && predicateFunction(device)) {
      return device;
    }
    await sleep(pollIntervalMilliseconds);
  }
  throw new Error(
    `pollDeviceUntil failed. targetDeviceName=${targetDeviceName} timeoutMilliseconds=${timeoutMilliseconds}`
  );
}

/**
 * @param {string} reportPrefix
 * @param {any} reportObject
 * @returns {string}
 */
export function writeJsonReport(reportPrefix, reportObject) {
  if (!fs.existsSync(reportsDirectoryPath)) {
    fs.mkdirSync(reportsDirectoryPath, { recursive: true });
  }
  const timestampText = createTimestampText();
  const reportFilePath = path.join(reportsDirectoryPath, `${reportPrefix}-${timestampText}.json`);
  fs.writeFileSync(reportFilePath, `${JSON.stringify(reportObject, null, 2)}\n`, "utf-8");
  return reportFilePath;
}

/**
 * @returns {string}
 */
export function createTimestampText() {
  return new Date().toISOString().replace(/[:.]/g, "-");
}

/**
 * @param {string} commandText
 * @returns {Promise<{ exitCode: number; stdout: string; stderr: string }>}
 */
export async function runCommand(commandText) {
  const childProcessModule = await import("child_process");
  return new Promise((resolvePromise, rejectPromise) => {
    childProcessModule.exec(commandText, { cwd: projectRootPath }, (error, stdout, stderr) => {
      if (error) {
        rejectPromise(
          new Error(
            `runCommand failed. commandText=${commandText} exitCode=${String(error.code ?? "")} stdout=${stdout.slice(0, 240)} stderr=${stderr.slice(0, 240)}`
          )
        );
        return;
      }
      resolvePromise({
        exitCode: 0,
        stdout,
        stderr
      });
    });
  });
}
