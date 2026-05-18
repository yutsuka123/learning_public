/**
 * @file localHistoryRetention.ts
 * @description SQLite ローカル履歴の保持日数ポリシーを検証する（設定ストア・環境変数・ランタイム更新の共通ルール）。
 * @remarks
 * - [重要] `0` は「期日パージしない（無期限で蓄積）」を意味する。
 * - [重要] `1`〜`99999` は「その日数より古い行をパージ対象」とする（LocalServer 時計基準）。
 * - [厳守] 負数や `99999` を超える値は拒否する。理由: 誤設定やオーバーフロー相当の入力を排除するため。
 */

/**
 * @description 保持日数が仕様範囲か検証する。
 * @param days 保持日数。`0` または `1`〜`99999` の整数。
 * @returns なし。
 */
export function validateLocalHistoryRetentionDays(days: number): void {
  if (!Number.isInteger(days)) {
    throw new Error(`validateLocalHistoryRetentionDays failed. value must be integer. value=${String(days)}`);
  }
  if (days === 0) {
    return;
  }
  if (days < 1 || days > 99999) {
    throw new Error(
      `validateLocalHistoryRetentionDays failed. value must be 0 (keep forever) or 1..99999. value=${String(days)}`
    );
  }
}

/**
 * @description 環境変数から種々の入力を正規化して保持日数へ変換する（起動時のみ）。
 * @param raw 環境変数から得た数値。
 * @returns 検証済みの保持日数。
 */
export function parseLocalHistoryRetentionDaysFromEnv(raw: number): number {
  validateLocalHistoryRetentionDays(raw);
  return raw;
}
