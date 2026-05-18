/**
 * @file historyTimestamps.ts
 * @description ローカル履歴用の時刻表記ヘルパー。`recordedAt` は UTC の ISO8601 を正とし、人間向けに JST（`Asia/Tokyo`）を併記する。
 * @remarks
 * - [重要] 日本は令和現在もサマータイムが無いため `Asia/Tokyo` は常に UTC+9 相当として扱える。
 * - [厳守] 本モジュールはタイムゾーン表示の統一のみを担い、MQTT ペイロードの `ts` フィールドの意味（送信側時計）とは別物である。
 */

/**
 * @description UTC の ISO8601 文字列から、同一瞬間の JST を説明付きで返す。
 * @param isoUtc `Date` が解釈できる ISO8601（通常は `toISOString()`）。
 * @returns 例: `2026-05-17T21:30:45.123+09:00 (JST)`。解釈不能時は空文字。
 */
export function formatRecordedAtJstFromIsoUtc(isoUtc: string): string {
  const parsed = Date.parse(isoUtc);
  if (Number.isNaN(parsed)) {
    return "";
  }
  const dateValue = new Date(parsed);
  const dateFormatter = new Intl.DateTimeFormat("en-CA", {
    timeZone: "Asia/Tokyo",
    year: "numeric",
    month: "2-digit",
    day: "2-digit"
  });
  const timeFormatter = new Intl.DateTimeFormat("en-CA", {
    timeZone: "Asia/Tokyo",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
    fractionalSecondDigits: 3
  });
  const datePart = dateFormatter.format(dateValue);
  const timePart = timeFormatter.format(dateValue);
  return `${datePart}T${timePart}+09:00 (JST)`;
}
