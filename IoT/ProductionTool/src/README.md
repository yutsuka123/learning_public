# ProductionTool `src`

[重要] 本フォルダは `ProductionTool` の実装本体格納先です。  
理由: 起動処理、画面遷移、`SecretCore` 共通部接続、dry-run、安全停止、不可逆段階計画をコード上で分離して管理するため。

## 想定責務
- 起動画面と状態表示
- 追加認証
- 対象機確認
- `PC-004` 鍵ID確認
- 実行内容確認
- dry-run 導線
- 不可逆段階計画（`FE -> NVS暗号化なし確認（非焼込み） -> SB -> 封鎖`）の所有
- 不可逆コマンドランナー準備（必須環境変数、証跡ファイル名、テンプレート表示）
- `PT-005z precheck`（Windows 対応、証跡保存先、`python`、`*_PATH` 系の存在確認、`PC-004` 期待値設定確認）
- 最小不可逆コマンドランナー（Windows CLI 限定、既定未実行、安全ゲート付き、`run` の段階逐次実行と `run 1`〜`run 4` の段階指定実行に対応。段階4では `DIS_USB_SERIAL_JTAG` を既定保留し、最終クローズ時のみ明示許可で焼成可能）
- 段階実行要約（`runner-execution-summary.json` による段階別 readback/evidence 要約）
- stability 受け皿（`runner-stability-summary.json` による `OTA/AP/STA` と次段可否の回収）
- `LocalServer` / `SecretCore` への不可逆監査 handoff と workflow 終端追跡
- 監査ログ参照導線
- エラー / 安全停止

## 実装時の注意
- [厳守] `LocalServer` と同一プロセス化しない。
- [厳守] 最小実ランナーは `PRODUCTION_TOOL_ENABLE_IRREVERSIBLE_COMMAND_RUNNER=1` と操作者の `run` または `run 1`〜`run 4` 入力が揃った場合のみ起動する。
- [厳守] 不可逆コマンドは `PRODUCTION_TOOL_ALLOW_IRREVERSIBLE_EXECUTION=1` が無い限り実行しない。
- [厳守] `precheck` が `canProceed=false` の場合、`stage_execute` へ進まない。
- [厳守] 実行後は `runner-execution-summary.json` を確認し、段階ごとの `before/after readback` と停止理由を追えるようにする。
- [厳守] stability 実施後は `runner-stability-summary.json` を確認し、`OTA/AP/STA` と次段可否を段階単位で追えるようにする。手動確認待ち段階でも、次段可否を保存しなければ後続段階へ進めない。
- [厳守] `run 2` 以降の段階指定実行は、直前段の `runner-stability-summary.json` に `canProceedToNextStage=true` が記録されていなければ開始しない。
- [厳守] `run` は内部的に段階ごとに実行し、各段階の stability / 次段可否を回収してから次段へ進む。
- [厳守] `DIS_USB_SERIAL_JTAG` は `PRODUCTION_TOOL_BURN_DIS_USB_SERIAL_JTAG=1` のときだけ段階4へ含め、`ENABLE_SECURITY_DOWNLOAD` は常に最終コマンドとして残す。
- [重要] シリアル出力停止は、他の不可逆と証跡確認が完了した後にシリアル抑止済みの量産通常FWへ更新する最終クローズ判断として扱う。現時点の実装はシリアル書換え不能化の切替までを担当し、FW切替自体は運用レビュー対象とする。
- [厳守] 実コマンドランナー追加時は、`irreversible_stage_plan.rs` の順序と `本番セキュア化出荷準備試験計画書.md` §6.4 の順序を同時に満たす。
- [厳守] `PC-004` は `PRODUCTION_TOOL_FE_KEY_ID` / `PRODUCTION_TOOL_SB_SIGNING_MATERIAL_ID` / `PRODUCTION_TOOL_TARGET_LOT_ID` を期待値とし、鍵ID・署名素材ID・ロット・作業指示番号の照合結果を `pc004-check-summary.json` へ残す。
- [厳守] 秘密値を UI 表示用 DTO へ混ぜない。
- [推奨] 画面状態、認証状態、対象機状態、監査ログ状態を別モジュールに分ける。

## 変更履歴
- 2026-04-30（続）: `irreversible_command_runner.rs` の責務を追記。理由: 実コマンド起動前に、`ProductionTool` が段階別コマンドと必須環境変数を所有するため。
- 2026-05-02: 最小実ランナーの責務を追記。理由: `ProductionTool` が証跡ディレクトリ作成、コマンド展開、stdout/stderr 保存、安全ゲート停止まで所有するようになったため。
- 2026-05-02: 最小実ランナーの段階指定実行を追記。理由: 全段一括ではなく、各段階で readback/evidence/stability を閉じてから次段へ進めるため。
- 2026-05-02: `PT-005z precheck` の責務を追記。理由: 実行前に不足条件と停止理由を `ProductionTool` 自身が証跡化するようになったため。
- 2026-05-02: 段階実行要約の責務を追記。理由: 実行後に段階ごとの readback/evidence と停止理由を `ProductionTool` 自身が要約するようになったため。
- 2026-05-02: stability 受け皿の責務を追記。理由: 実行後に `OTA/AP/STA` と次段可否を `ProductionTool` 自身が回収・保存するようになったため。
- 2026-05-02: 前段 stability ゲートを追記。理由: 段階指定実行でも次段進行条件を `ProductionTool` が強制するため。
- 2026-05-02: `run` の段階逐次実行を追記。理由: 全段実行でも段階境界で stability を閉じるため。
- 2026-05-02: 段階4の最終クローズ切替を追記。理由: 試験ではシリアルログを残しつつ、最終個体では `DIS_USB_SERIAL_JTAG` も焼けるようにするため。
- 2026-05-02: `PC-004` 鍵ID確認の責務を追記。理由: 鍵ID・署名素材ID・ロット・作業指示番号の照合責任も `ProductionTool` 側で一貫して持つため。
- 2026-04-30: `irreversible_stage_plan.rs` と `production_workflow_handoff.rs` の責務を追記。理由: `ProductionTool` を製造ツールとして不可逆工程の責任主体へ収束させるため。
- 2026-04-29: NVS(HMAC) を本番不可逆段階から外し、NVS暗号化なし確認へ修正。理由: 現行案Aでは NVS Encryption / HMAC eFuse 投入を実施しないため。
- 2026-03-17: 新規作成。理由: 実装本体の格納責務を先に固定するため。
