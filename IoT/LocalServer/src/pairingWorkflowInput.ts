/**
 * @file pairingWorkflowInput.ts
 * @description Pairing workflow の入力組み立てと事前検証を担当する。
 * @remarks
 * - [重要] `runPairingSession()` 呼び出し前に TS 側で必須項目を固定し、`SecretCore` への不完全入力流入を防ぐ。
 * - [厳守] raw `k-device` や ECDH 共有秘密を返さず、workflow start body の整形と検証だけを扱う。
 * - [将来対応] `caCertRef` は現状 `mqttTlsCertIssueNo` 中心で引き継ぐ。証明書実体との解決規則は Rust 側 workflow 実装時に最終確定する。
 */

import {
  apConfigureRequestBody,
  pairingRequestedCredentials,
  pairingRequestedEndpointSettings,
  pairingRequestedServerSettings,
  pairingRequestedSettings,
  pairingWorkflowStartRequestBody
} from "./types";

/**
 * @description AP configure 系入力から Pairing workflow 開始要求を組み立てる。
 * @param params workflow 固有の識別情報。
 * @param requestBody 既存 AP configure 系入力。
 * @param keyDeviceBase64 Pairing workflow へ渡す `k-device` の Base64 文字列。
 * @returns `runPairingSession()` へ渡せる開始要求。
 */
export function buildPairingWorkflowStartRequestBodyFromApConfigure(
  params: {
    targetDeviceId: string;
    sessionId: string;
    keyVersion: string;
  },
  requestBody: apConfigureRequestBody,
  keyDeviceBase64: string
): pairingWorkflowStartRequestBody {
  const pairingWorkflowStartRequestBody = {
    targetDeviceId: String(params.targetDeviceId ?? "").trim(),
    sessionId: String(params.sessionId ?? "").trim(),
    keyVersion: String(params.keyVersion ?? "").trim(),
    requestedSettings: buildPairingRequestedSettingsFromApConfigure(requestBody, keyDeviceBase64)
  } satisfies pairingWorkflowStartRequestBody;

  validatePairingWorkflowStartRequestBody("buildPairingWorkflowStartRequestBodyFromApConfigure", pairingWorkflowStartRequestBody);
  return pairingWorkflowStartRequestBody;
}

/**
 * @description AP configure 系入力から Pairing workflow 用 `requestedSettings` を組み立てる。
 * @param requestBody 既存 AP configure 系入力。
 * @param keyDeviceBase64 Pairing workflow へ渡す `k-device` の Base64 文字列。
 * @returns Pairing workflow 用の設定。
 */
export function buildPairingRequestedSettingsFromApConfigure(
  requestBody: apConfigureRequestBody,
  keyDeviceBase64: string
): pairingRequestedSettings {
  if (requestBody === undefined || requestBody === null) {
    throw new Error("buildPairingRequestedSettingsFromApConfigure failed. requestBody is null.");
  }

  const mqttSettings: pairingRequestedEndpointSettings = {
    host: normalizeHostInputForPairing(requestBody.mqttUrl),
    hostName: normalizeOptionalText(requestBody.mqttUrlName),
    port: requestBody.mqttPort,
    tls: requestBody.mqttTls,
    username: String(requestBody.mqttUser ?? ""),
    password: String(requestBody.mqttPass ?? ""),
    caCertRef: normalizeOptionalText(requestBody.mqttTlsCertIssueNo)
  };

  // [重要] 現行の AP configure 運用と同様に、OTA URL 未指定時は serverUrl 系を補助候補として扱う。
  // 理由: 既存 UI / 試験スクリプトが serverUrl を OTA 配布元と兼用するケースを含むため。
  const otaHostSource = String(requestBody.otaUrl ?? requestBody.serverUrl ?? "");
  const otaHostNameSource = String(requestBody.otaUrlName ?? requestBody.serverUrlName ?? "");
  const otaSettings: pairingRequestedEndpointSettings = {
    host: normalizeHostInputForPairing(otaHostSource),
    hostName: normalizeOptionalText(otaHostNameSource),
    port: requestBody.otaPort ?? requestBody.serverPort ?? 443,
    tls: requestBody.otaTls ?? requestBody.serverTls ?? true,
    username: String(requestBody.otaUser ?? ""),
    password: String(requestBody.otaPass ?? ""),
    caCertRef: ""
  };

  const requestedSettings: pairingRequestedSettings = {
    wifi: {
      ssid: String(requestBody.wifiSsid ?? ""),
      password: String(requestBody.wifiPass ?? "")
    },
    mqtt: mqttSettings,
    ota: otaSettings,
    credentials: buildPairingRequestedCredentials(requestBody, keyDeviceBase64)
  };

  const normalizedServerHost = normalizeHostInputForPairing(requestBody.serverUrl);
  const normalizedServerHostName = normalizeOptionalText(requestBody.serverUrlName);
  if (normalizedServerHost.length > 0 || normalizedServerHostName.length > 0) {
    requestedSettings.server = {
      host: normalizedServerHost,
      hostName: normalizedServerHostName,
      port: requestBody.serverPort ?? 443,
      tls: requestBody.serverTls ?? true
    };
  }

  const normalizedNtpHost = normalizeHostInputForPairing(requestBody.timeServerUrl);
  const normalizedNtpHostName = normalizeOptionalText(requestBody.timeServerUrlName);
  if (normalizedNtpHost.length > 0 || normalizedNtpHostName.length > 0) {
    requestedSettings.ntp = {
      host: normalizedNtpHost,
      hostName: normalizedNtpHostName,
      port: requestBody.timeServerPort ?? 123,
      tls: requestBody.timeServerTls ?? false
    };
  }

  validatePairingRequestedSettings("buildPairingRequestedSettingsFromApConfigure", requestedSettings);
  return requestedSettings;
}

/**
 * @description Pairing workflow 開始要求の必須項目を検証する。
 * @param functionName 呼び出し元関数名。
 * @param requestBody Pairing workflow 開始要求。
 */
export function validatePairingWorkflowStartRequestBody(
  functionName: string,
  requestBody: pairingWorkflowStartRequestBody
): void {
  if (requestBody === undefined || requestBody === null) {
    throw new Error(`validatePairingWorkflowStartRequestBody failed. functionName=${functionName} requestBody is null.`);
  }
  validateRequiredTextField(functionName, "targetDeviceId", requestBody.targetDeviceId);
  validateRequiredTextField(functionName, "sessionId", requestBody.sessionId);
  validateRequiredTextField(functionName, "keyVersion", requestBody.keyVersion);
  validatePairingRequestedSettings(functionName, requestBody.requestedSettings);
}

/**
 * @description Pairing workflow 用 `requestedSettings` の必須項目を検証する。
 * @param functionName 呼び出し元関数名。
 * @param requestedSettings Pairing workflow 用設定。
 */
export function validatePairingRequestedSettings(
  functionName: string,
  requestedSettings: pairingRequestedSettings
): void {
  if (requestedSettings === undefined || requestedSettings === null) {
    throw new Error(`validatePairingRequestedSettings failed. functionName=${functionName} requestedSettings is null.`);
  }

  validateRequiredTextField(functionName, "requestedSettings.wifi.ssid", requestedSettings.wifi?.ssid);
  validateStringFieldPresence(functionName, "requestedSettings.wifi.password", requestedSettings.wifi?.password);
  validateEndpointSettings(functionName, "requestedSettings.mqtt", requestedSettings.mqtt);
  validateEndpointSettings(functionName, "requestedSettings.ota", requestedSettings.ota);
  validateCredentialsSettings(functionName, requestedSettings.credentials);

  if (requestedSettings.server !== undefined) {
    validateServerSettings(functionName, "requestedSettings.server", requestedSettings.server);
  }
  if (requestedSettings.ntp !== undefined) {
    validateServerSettings(functionName, "requestedSettings.ntp", requestedSettings.ntp);
  }
}

/**
 * @description Pairing workflow 用 credentials を組み立てる。
 * @param requestBody 既存 AP configure 系入力。
 * @param keyDeviceBase64 Pairing workflow へ渡す `k-device` の Base64 文字列。
 * @returns credentials。
 */
function buildPairingRequestedCredentials(
  requestBody: apConfigureRequestBody,
  keyDeviceBase64: string
): pairingRequestedCredentials {
  return {
    wifiUsername: "",
    wifiPassword: String(requestBody.wifiPass ?? ""),
    mqttUsername: String(requestBody.mqttUser ?? ""),
    mqttPassword: String(requestBody.mqttPass ?? ""),
    otaUsername: String(requestBody.otaUser ?? ""),
    otaPassword: String(requestBody.otaPass ?? ""),
    keyDevice: String(keyDeviceBase64 ?? "").trim()
  };
}

/**
 * @description 接続先設定の必須項目を検証する。
 * @param functionName 呼び出し元関数名。
 * @param fieldPath 検証対象のフィールドパス。
 * @param endpointSettings 検証対象の接続先設定。
 */
function validateEndpointSettings(
  functionName: string,
  fieldPath: string,
  endpointSettings: pairingRequestedEndpointSettings | undefined
): void {
  if (endpointSettings === undefined || endpointSettings === null) {
    throw new Error(`validateEndpointSettings failed. functionName=${functionName} fieldPath=${fieldPath} endpointSettings is null.`);
  }
  validateRequiredTextField(functionName, `${fieldPath}.host`, endpointSettings.host);
  validatePortField(functionName, `${fieldPath}.port`, endpointSettings.port);
  validateBooleanField(functionName, `${fieldPath}.tls`, endpointSettings.tls);
  validateStringFieldPresence(functionName, `${fieldPath}.username`, endpointSettings.username);
  validateStringFieldPresence(functionName, `${fieldPath}.password`, endpointSettings.password);
  if (endpointSettings.hostName !== undefined) {
    validateStringFieldPresence(functionName, `${fieldPath}.hostName`, endpointSettings.hostName);
  }
  if (endpointSettings.caCertRef !== undefined) {
    validateStringFieldPresence(functionName, `${fieldPath}.caCertRef`, endpointSettings.caCertRef);
  }
}

/**
 * @description server / ntp の任意設定を検証する。
 * @param functionName 呼び出し元関数名。
 * @param fieldPath 検証対象のフィールドパス。
 * @param serverSettings 検証対象の設定。
 */
function validateServerSettings(
  functionName: string,
  fieldPath: string,
  serverSettings: pairingRequestedServerSettings | undefined
): void {
  if (serverSettings === undefined || serverSettings === null) {
    throw new Error(`validateServerSettings failed. functionName=${functionName} fieldPath=${fieldPath} serverSettings is null.`);
  }
  validateRequiredTextField(functionName, `${fieldPath}.host`, serverSettings.host);
  validatePortField(functionName, `${fieldPath}.port`, serverSettings.port);
  validateBooleanField(functionName, `${fieldPath}.tls`, serverSettings.tls);
  if (serverSettings.hostName !== undefined) {
    validateStringFieldPresence(functionName, `${fieldPath}.hostName`, serverSettings.hostName);
  }
}

/**
 * @description credentials 設定の必須項目を検証する。
 * @param functionName 呼び出し元関数名。
 * @param credentials 検証対象の credentials。
 */
function validateCredentialsSettings(
  functionName: string,
  credentials: pairingRequestedCredentials | undefined
): void {
  if (credentials === undefined || credentials === null) {
    throw new Error(`validateCredentialsSettings failed. functionName=${functionName} credentials is null.`);
  }
  if (credentials.wifiUsername !== undefined) {
    validateStringFieldPresence(functionName, "requestedSettings.credentials.wifiUsername", credentials.wifiUsername);
  }
  validateStringFieldPresence(functionName, "requestedSettings.credentials.wifiPassword", credentials.wifiPassword);
  validateStringFieldPresence(functionName, "requestedSettings.credentials.mqttUsername", credentials.mqttUsername);
  validateStringFieldPresence(functionName, "requestedSettings.credentials.mqttPassword", credentials.mqttPassword);
  validateStringFieldPresence(functionName, "requestedSettings.credentials.otaUsername", credentials.otaUsername);
  validateStringFieldPresence(functionName, "requestedSettings.credentials.otaPassword", credentials.otaPassword);
  validateRequiredTextField(functionName, "requestedSettings.credentials.keyDevice", credentials.keyDevice);
}

/**
 * @description 文字列項目が存在することを検証する。
 * @param functionName 呼び出し元関数名。
 * @param fieldPath 検証対象のフィールドパス。
 * @param fieldValue 検証対象の値。
 */
function validateStringFieldPresence(functionName: string, fieldPath: string, fieldValue: unknown): void {
  if (typeof fieldValue !== "string") {
    throw new Error(
      `validateStringFieldPresence failed. functionName=${functionName} fieldPath=${fieldPath} actualType=${typeof fieldValue}`
    );
  }
}

/**
 * @description 文字列必須項目を検証する。
 * @param functionName 呼び出し元関数名。
 * @param fieldPath 検証対象のフィールドパス。
 * @param fieldValue 検証対象の値。
 */
function validateRequiredTextField(functionName: string, fieldPath: string, fieldValue: unknown): void {
  validateStringFieldPresence(functionName, fieldPath, fieldValue);
  if (String(fieldValue).trim().length === 0) {
    throw new Error(`validateRequiredTextField failed. functionName=${functionName} fieldPath=${fieldPath} is empty.`);
  }
}

/**
 * @description 真偽値項目を検証する。
 * @param functionName 呼び出し元関数名。
 * @param fieldPath 検証対象のフィールドパス。
 * @param fieldValue 検証対象の値。
 */
function validateBooleanField(functionName: string, fieldPath: string, fieldValue: unknown): void {
  if (typeof fieldValue !== "boolean") {
    throw new Error(`validateBooleanField failed. functionName=${functionName} fieldPath=${fieldPath} actualType=${typeof fieldValue}`);
  }
}

/**
 * @description ポート番号項目を検証する。
 * @param functionName 呼び出し元関数名。
 * @param fieldPath 検証対象のフィールドパス。
 * @param fieldValue 検証対象の値。
 */
function validatePortField(functionName: string, fieldPath: string, fieldValue: unknown): void {
  if (typeof fieldValue !== "number" || !Number.isInteger(fieldValue) || fieldValue < 1 || fieldValue > 65535) {
    throw new Error(`validatePortField failed. functionName=${functionName} fieldPath=${fieldPath} actualValue=${String(fieldValue)}`);
  }
}

/**
 * @description URL またはホスト表記を Pairing workflow 用ホスト名へ正規化する。
 * @param rawHostOrUrl 生入力。
 * @returns 正規化後ホスト名。
 */
function normalizeHostInputForPairing(rawHostOrUrl: unknown): string {
  const inputText = String(rawHostOrUrl ?? "").trim();
  if (inputText.length === 0) {
    return "";
  }
  if (inputText.includes("://")) {
    try {
      const parsedUrl = new URL(inputText);
      return parsedUrl.hostname.trim();
    } catch {
      return inputText;
    }
  }
  const slashIndex = inputText.indexOf("/");
  if (slashIndex >= 0) {
    return inputText.slice(0, slashIndex).trim();
  }
  return inputText;
}

/**
 * @description 任意文字列を trim して返す。
 * @param rawValue 生入力。
 * @returns trim 後文字列。
 */
function normalizeOptionalText(rawValue: unknown): string {
  return String(rawValue ?? "").trim();
}
