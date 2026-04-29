# ProductionTool `src`

[重要] 本フォルダは `ProductionTool` の実装本体格納先です。  
理由: 起動処理、画面遷移、`SecretCore` 共通部接続、dry-run、安全停止、不可逆段階計画をコード上で分離して管理するため。

## 想定責務
- 起動画面と状態表示
- 追加認証
- 対象機確認
- 実行内容確認
- dry-run 導線
- 不可逆段階計画（`FE -> NVS暗号化なし確認（非焼込み） -> SB -> 封鎖`）の所有
- 不可逆コマンドランナー準備（必須環境変数とテンプレート表示）
- `LocalServer` / `SecretCore` への不可逆監査 handoff と workflow 終端追跡
- 監査ログ参照導線
- エラー / 安全停止

## 実装時の注意
- [厳守] `LocalServer` と同一プロセス化しない。
- [厳守] 実コマンドランナーを追加するまでは、不可逆実コマンドを起動しない。
- [厳守] 実コマンドランナー追加時は、`irreversible_stage_plan.rs` の順序と `本番セキュア化出荷準備試験計画書.md` §6.4 の順序を同時に満たす。
- [厳守] 秘密値を UI 表示用 DTO へ混ぜない。
- [推奨] 画面状態、認証状態、対象機状態、監査ログ状態を別モジュールに分ける。

## 変更履歴
- 2026-04-30（続）: `irreversible_command_runner.rs` の責務を追記。理由: 実コマンド起動前に、`ProductionTool` が段階別コマンドと必須環境変数を所有するため。
- 2026-04-30: `irreversible_stage_plan.rs` と `production_workflow_handoff.rs` の責務を追記。理由: `ProductionTool` を製造ツールとして不可逆工程の責任主体へ収束させるため。
- 2026-04-29: NVS(HMAC) を本番不可逆段階から外し、NVS暗号化なし確認へ修正。理由: 現行案Aでは NVS Encryption / HMAC eFuse 投入を実施しないため。
- 2026-03-17: 新規作成。理由: 実装本体の格納責務を先に固定するため。
