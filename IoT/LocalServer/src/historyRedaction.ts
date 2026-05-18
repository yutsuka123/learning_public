/**
 * @file historyRedaction.ts
 * @description SQLite 履歴・MQTT `args` / status `detail` 向けの機密キーマスク（プレースホルダ置換）を集約する。
 * @remarks
 * - [重要][2026-05-17] `mqttGateway` と同一ルールを単一モジュールで保持し、自動試験（7103）と本体実装の乖離を防ぐ。
 * - [厳守] ルール変更時は `試験仕様書.md` 7103 と本モジュールの両方を確認する。
 */

/** @description ネストが深すぎる場合の履歴プレースホルダ（機密ではなく構造上限の通知）。 */
export const HISTORY_TRUNCATED_PLACEHOLDER = "<truncatedDetail>";

/** @description ネストした args / JSON を辿る最大深さ（履歴肥大・再帰過多の抑止）。 */
export const HISTORY_DETAIL_MAX_DEPTH = 6;

/**
 * @description キー名から履歴用マスク文字列（角括弧の英語ラベル）を返す。
 * @param key 引数キー名。
 * @returns 置換文字列。マスク不要なら undefined。
 */
export function getHistoryRedactionPlaceholderForKey(key: string): string | undefined {
  const k = key.toLowerCase();
  const rules: ReadonlyArray<{ match: (low: string) => boolean; placeholder: string }> = [
    { match: (low) => low === "enc", placeholder: "<encryptedPayload>" },
    {
      match: (low) => low === "pwd" || low.includes("password") || low.includes("passwd"),
      placeholder: "<password>"
    },
    { match: (low) => low.includes("apikey") || low.includes("api_key"), placeholder: "<apiKey>" },
    { match: (low) => low.includes("clientsecret"), placeholder: "<clientSecret>" },
    {
      match: (low) => low.includes("accesstoken") || low.includes("refreshtoken"),
      placeholder: "<token>"
    },
    { match: (low) => low.includes("sessionkey") || low.includes("ipckey"), placeholder: "<sessionSecret>" },
    { match: (low) => low.includes("authorization"), placeholder: "<authorization>" },
    { match: (low) => low.includes("bearer"), placeholder: "<token>" },
    { match: (low) => low.includes("credential"), placeholder: "<credential>" },
    { match: (low) => low.includes("token"), placeholder: "<token>" },
    { match: (low) => low.includes("secret"), placeholder: "<secret>" },
    {
      match: (low) => low.includes("privatekey") || low.includes("keydevice") || low.includes("kdevice"),
      placeholder: "<keyMaterial>"
    }
  ];
  for (const rule of rules) {
    if (rule.match(k)) {
      return rule.placeholder;
    }
  }
  return undefined;
}

/**
 * @description 配列・子オブジェクトを履歴向けに再帰処理する。
 * @param value 任意の JSON 互換値。
 * @param depth ネスト深さ。
 * @returns サニタイズ後の値。
 */
function sanitizeHistoryDetailValue(value: unknown, depth: number): unknown {
  if (value === null || value === undefined) {
    return value;
  }
  if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
    return value;
  }
  if (typeof value === "bigint") {
    return value.toString();
  }
  if (Array.isArray(value)) {
    if (depth > HISTORY_DETAIL_MAX_DEPTH) {
      return HISTORY_TRUNCATED_PLACEHOLDER;
    }
    return value.map((item) => sanitizeHistoryDetailValue(item, depth + 1));
  }
  if (typeof value === "object") {
    return sanitizeRecordForHistoryDetail(value as Record<string, unknown>, depth);
  }
  return String(value);
}

/**
 * @description オブジェクトを履歴保存向けに再帰コピーし、機密キーはプレースホルダへ差し替える。
 * @param source 元のキー/値。
 * @param depth 現在のネスト深さ。
 * @returns サニタイズ済みプレーンオブジェクト。
 */
export function sanitizeRecordForHistoryDetail(source: Record<string, unknown>, depth: number): Record<string, unknown> {
  if (depth > HISTORY_DETAIL_MAX_DEPTH) {
    return { _historyDetailTruncated: HISTORY_TRUNCATED_PLACEHOLDER };
  }
  const nextRecord: Record<string, unknown> = {};
  for (const entryKey of Object.keys(source)) {
    const placeholder = getHistoryRedactionPlaceholderForKey(entryKey);
    if (placeholder !== undefined) {
      nextRecord[entryKey] = placeholder;
      continue;
    }
    nextRecord[entryKey] = sanitizeHistoryDetailValue(source[entryKey], depth + 1);
  }
  return nextRecord;
}

/**
 * @description MQTT `args` を履歴用に複製し、機密キーの値を `<password>` 等へ置換してから JSON 化する。
 * @param args MQTT payload の args。
 * @returns JSON 文字列。
 */
export function buildRedactedCommandDetailJson(args: Record<string, unknown>): string {
  const sanitizedRecord = sanitizeRecordForHistoryDetail(args, 0);
  return JSON.stringify(sanitizedRecord);
}

/**
 * @description status の `detail` が JSON オブジェクトなら同ルールでマスクした文字列を返す。それ以外は据え置き。
 * @param detailText デバイスからの自由文または JSON 文字列。
 * @returns 記録用 detail。
 */
export function buildRedactedStatusDetailText(detailText: string): string {
  if (detailText.length === 0) {
    return detailText;
  }
  try {
    const parsed = JSON.parse(detailText) as unknown;
    if (parsed !== null && typeof parsed === "object" && !Array.isArray(parsed)) {
      const sanitizedRecord = sanitizeRecordForHistoryDetail(parsed as Record<string, unknown>, 0);
      return JSON.stringify(sanitizedRecord);
    }
  } catch {
    /* 自由文のまま保存（改行や非 JSON はここに来る） */
  }
  return detailText;
}
