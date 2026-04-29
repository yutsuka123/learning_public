# ProductionTool

[重要] 本フォルダは `ProductionTool` の実装格納先です。  
理由: `LocalServer` と分離した工場端末向けソフトウェアとして、実装・設定・試験資材の入口を固定するため。

[厳守] `ProductionTool` は `LocalServer` と別ソフト、別実行物、別インストーラとして独立動作させます。  
理由: 通常運用系と工場内の高リスク処理系を混在させないため。

[厳守] Rust 共通化は `LocalServer` 側で整備した `SecretCore` 共通部や関連ライブラリを再利用する意味であり、ソフト自体を統合する意味ではありません。  
理由: 共通部を活用しつつ、責務境界と安全停止条件を保つため。

[厳守] `ProductionTool` は、製造・初回セキュア化・eFuse 最終有効化の **責任主体** とする。現段階の実装対象は、基本機能（起動、追加認証、対象機確認、dry-run、安全停止）、`ProductionTool` 所有の不可逆段階計画表示（`irreversible_stage_plan.rs`）、任意 **PT-005a（`SecretCore` が受理する不可逆監査の HTTP 経路を終端まで追跡）** である。  
理由: **王道の設計**として `ProductionTool` が工場ツールとして焼込み責任を持つ。ただし現時点では安全のため `espefuse` 等の実コマンド起動ランナーは未実装とし、`本番セキュア化出荷準備試験計画書.md` §6.4／`007_本番1台目_実行順チェックリスト.md` の手順資産を `ProductionTool` 管理下へ段階的に取り込む。

[方針変更][2026-04-29 12:27][厳守] `007-B` の不可逆本体は、`ProductionTool` の最終形実装後に進める。最終形とは、`precheck -> stage_execute -> readback -> evidence -> stability` を `ProductionTool` 管理下で閉じ、`espefuse` / `espsecure` / `esptool` の実行、段階別 readback、証跡保存、段階間安定性確認を一貫して扱える状態を指す。  
理由: `ProductionTool` を不可逆工程の責任主体とする設計思想を、画面・監査だけでなく実行責任まで一致させるため。既存実績手順を手動でなぞって `007-B` 本体へ進む運用は、今回の `DEV-002` では採用しない。

### 上位設計とこのフォルダの現在地（読み順）

| 論点 | 正（親） |
| :--- | :--- |
| 不可逆の段階・順序・証跡 | `本番セキュア化出荷準備試験計画書.md` §6.4、`007_本番1台目_実行順チェックリスト.md` |
| 将来のウィザード一気通貫 | `ProductionTool画面仕様書.md` 第9節 |
| Rust 実行物が担う処理 | PT-001〜PT-005、不可逆段階計画表示（`src/irreversible_stage_plan.rs`）、不可逆コマンドランナー準備表示（`src/irreversible_command_runner.rs`）、任意 PT-005a（`src/production_workflow_handoff.rs`） |
| SecretCore が焼くか | 焼かない（`runProductionSecureFlow` は受理／監査遷移。`generic_workflow.rs` 参照） |

## 1. 現在の想定フォルダ構成
- `Cargo.toml`
  [重要][2026-03-17] `ProductionTool` 最小実行骨格の Rust クレート定義。
- `src/`
  実装本体。起動処理、画面遷移、`SecretCore` 共通部接続、dry-run 導線を配置する。
- `config/`
  配布設定、既定値、ログ保存先などの設定ひな形を配置する。
- `scripts/`
  開発補助スクリプト、ビルド補助、試験補助を配置する。
- `installer/`
  [重要][2026-03-22] Inno Setup 用の EXE インストーラスクリプトを配置する。
- `tests/`
  `7086`〜`7088` を含む基本機能試験用の自動化・補助資材を配置する。
- `assets/`
  アイコン、画面用静的素材、配布に必要な固定資材を配置する。
- `logs/`
  開発時のログ出力先。正式な製造監査ログ保存先は別途インストール設計で確定する。

## 2. 直近の実装対象
- [進捗][2026-03-24] `PT-001`〜`PT-005` の CLI ウィザード骨格を実装済み。起動・追加認証（開発用 `maker2026`、操作者ID任意、作業指示番号空欄時自動採番）・対象機一覧行選択・dry-run 手前停止・監査ログを 7086/7087 観点で確認できる状態。`ProductionTool画面仕様書.md` 第8節に実装状況を記載。
- `PT-001` 起動画面 … 実装済
- `PT-002` 追加認証画面 … 実装済
- `PT-003` 対象機確認画面 … 実装済（一覧行選択方式、MAC再入力不要）
- `PT-004` 実行内容確認画面 … 実装済（dry-run 導線入口）
- `PT-005` dry-run 結果画面 … 実装済（本実行手前で停止）
- `PT-005x` 不可逆段階計画表示 … 実装済（`FE -> NVS暗号化なし確認（非焼込み） -> SB -> 封鎖` を `ProductionTool` 所有計画として表示。実コマンド起動は未実装）
- `PT-005y` 不可逆コマンドランナー準備表示 … 実装済（`espefuse` / `espsecure` / `esptool` のテンプレートと必須環境変数を表示。実コマンド起動は未実装）
- `PT-005z` 不可逆実コマンドランナー … **未実装 / 007-B進入ブロック条件**（`precheck -> stage_execute -> readback -> evidence -> stability` を閉じる最終形。2026-04-29 12:27 方針変更により、本機能の実装・検証完了まで本番候補 `DEV-002` の不可逆本体へ進まない）
- `PT-005a` 不可逆監査受理 handoff … 実装済（`LocalServer` start API の POST 成功だけでなく、`GET /api/workflows/{workflowId}` で `completed/OK` まで追跡）
- `PT-006` 監査ログ確認画面 … 骨格実装済
- `PT-007` エラー / 安全停止画面 … 実装済
- [進捗][2026-03-22] `scripts/uninstall-production-tool.ps1`、`scripts/build-production-tool-installer.ps1`、`installer/ProductionTool.iss` を追加し、`003-0016` / `009-1021` 向けの安全消去付きアンインストーラ骨格と Inno Setup EXE 化骨格を実装した。混在インストール防止と `ProgramData` 配下の監査ログ/作業領域作成をスクリプト化済み。実際の EXE ビルド・インストール試験は未実施。
- [進捗][2026-03-23] `scripts/install-production-tool.ps1`、`tests/testProductionToolInstallHelperSmoke.ps1`、`tests/test7090ProductionToolUninstallSmoke.ps1` を追加し、実インストール前に配置/安全消去の両方向を擬似環境で確認できるようにした。現時点の差し止め要因は `ISCC.exe` 未導入による `ProductionToolSetup.exe` 未生成である。
- [進捗][2026-03-24] `ProductionTool/.env` を追加し、PT-002 追加認証の `PRODUCTION_TOOL_AUTH_OPERATOR_ID` / `PRODUCTION_TOOL_AUTH_PASSWORD` / `PRODUCTION_TOOL_AUTH_MAX_ATTEMPTS` を環境変数管理へ移行した。既定値は従来どおり（`productiontool` / `maker2026` / `3`）。
- [進捗][2026-03-24] 共有用サンプルとして `ProductionTool/.env.example.sample.txt` を追加した。理由: 共有時に実値を出さず、`.env` は端末ごとにローカル実値で管理するため。

## 3. 参照すべき文書
- `../ProductionTool画面仕様書.md`
- `../製造・セキュア化手順書.md`
- `../デバイス管理表.md`
- `../製品化成果概要書.md`
- `../モジュール仕様書.md`
- `../IF仕様書.md`
- `../LocalServer秘密処理別層仕様書.md`
- `../試験仕様書.md`
- `../コマンド仕様書.md`
- `../インストーラEXE化設計仕様書.md`

## 4. 実装開始時の注意
- [厳守] raw key、中間秘密、eFuse 実値、平文パスワードを画面・ログ・標準出力へ出さないこと。
- [厳守] 認証設定は `ProductionTool/.env` で管理し、共有時は `ProductionTool/.env.example.sample.txt` を配布すること。
- [厳守] `LocalServer` の通常画面や通常 REST の延長として不可逆処理へ入らないこと。
- [厳守] 実コマンドランナーを追加する場合は、`ProductionTool` 内で `precheck -> stage_execute -> readback -> evidence -> stability` を閉じ、`SecretCore` の受理ログだけを実焼込み完了証跡として扱わないこと。
- [推奨] 追加認証、対象機確認、dry-run、安全停止を先に実装し、その後に不可逆処理の前段だけを接続すること。
- [推奨] 変更時は `IoT/ソース概要.md`、`IoT/試験仕様書.md`、`IoT/試験記録書.md` の更新要否も同時確認すること。

## 5.1 `.env` 運用（PT-002）
- 初回は `ProductionTool/.env.example.sample.txt` を `ProductionTool/.env` へコピーして利用する。
- 実運用値は `ProductionTool/.env` のみで管理し、サンプルへ逆流させない。
- `PRODUCTION_TOOL_AUTH_OPERATOR_ID` を空にすると操作者ID固定照合を無効化できる（監査用途のみ）。
- `PRODUCTION_TOOL_AUTH_PASSWORD` は空文字禁止。
- `PRODUCTION_TOOL_AUTH_MAX_ATTEMPTS` は `1` 以上を設定する。

## 5.2 `.env` 運用（PT-005a）
- `PRODUCTION_TOOL_LS_ADMIN_TOKEN`: `LocalServer` 管理者 API bearer token。
- `PRODUCTION_TOOL_AP_BASE_URL`: 対象機 AP の URL。
- `PRODUCTION_TOOL_AP_MFG_USERNAME` / `PRODUCTION_TOOL_AP_MFG_PASSWORD`: AP 側 mfg 認証。
- `PRODUCTION_TOOL_WORKFLOW_POLL_TIMEOUT_SECONDS`: workflow 終端待ち秒数（既定 60 秒）。
- `PRODUCTION_TOOL_WORKFLOW_POLL_INTERVAL_MILLIS`: workflow ポーリング間隔（既定 1000 ms）。

## 5.3 `.env` 運用（不可逆コマンドランナー準備）
- `PRODUCTION_TOOL_ENABLE_IRREVERSIBLE_COMMAND_RUNNER`: 現フェーズでは `1` でも実コマンド未実行。後続実装で二重確認ゲートに使う。
- `PRODUCTION_TOOL_SERIAL_PORT`: 対象 COM ポート。
- `PRODUCTION_TOOL_EVIDENCE_DIR`: 4点証跡のうち `ProductionTool` / `espefuse` 側を保存する作業ディレクトリ。
- `PRODUCTION_TOOL_FE_KEY_PATH` / `PRODUCTION_TOOL_SB_SIGNING_KEY_PATH`: 鍵素材のローカル実パス（サンプルや Git へ実値禁止）。現行案Aでは `PRODUCTION_TOOL_HMAC_KEY_PATH` は使わない。
- `PRODUCTION_TOOL_FLASH_ARGS_STAGE1` / `PRODUCTION_TOOL_FLASH_ARGS_STAGE3`: 段階別 flash 引数。アドレス・ファイル対応は `コマンド仕様書.md` と当日記録で固定する。

## 5. EXE 化の再開手順
- [重要] `ISCC.exe` が利用可能になったら、まず `scripts/build-production-tool-installer.ps1 -SkipCargoBuild` で解決可否だけを確認する。
- [重要] `build-production-tool-installer.ps1` は `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`、`C:\Program Files\Inno Setup 6\ISCC.exe`、`%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe` を探索する。
- [重要] その後 `scripts/build-production-tool-installer.ps1` を実行し、`installer/build/ProductionToolSetup.exe` を生成する。
- [厳守] 実インストール試験は `LocalServer` 未導入の工場専用端末でのみ実施する。
- [厳守] 本試験では `ProductionTool.exe` の不可逆処理本体を実行せず、配置確認、1回起動確認、`UninstallRun` 経由の安全消去確認までを対象とする。

## 6. 変更履歴
- 2026-04-29 12:27: `007-B` は `ProductionTool` 最終形実ランナーの実装・検証後に進める方針へ変更。理由: `ProductionTool` を不可逆工程の責任主体とする設計思想を、画面・監査だけでなく実行責任まで一致させるため。
- 2026-04-29: `irreversible_command_runner.rs` の `espefuse summary` テンプレートを段階・コマンド番号別の証跡ファイル名へ修正。理由: 実ランナー実装時に before/after 証跡を上書きしないため。
- 2026-04-30（続²）: `irreversible_command_runner.rs` による不可逆コマンドランナー準備表示を追記。理由: `ProductionTool` が `espefuse` 等を管理下へ取り込む前に、必須環境変数・段階テンプレート・実行未サポート状態を安全に固定するため。
- 2026-04-30（続）: `irreversible_stage_plan.rs` による不可逆段階計画表示と、PT-005a の workflow 終端追跡を追記。理由: `ProductionTool` が製造ツールとして焼込み責任を持つ設計へ、既存 Rust フローを壊さず段階的に収束させるため。
- 2026-04-29: 現行案Aに合わせ、NVS(HMAC) / HMAC鍵投入を本番不可逆段階から外す旨を追記。理由: NVS Encryption は実施しない方針のため。
- 2026-04-30: **上位設計との関係（§6.4／007 チェックリストと本番1台目実績）** と **現コードが担う範囲（PT-005a まで）** を冒頭直下に明示。不可逆焼込の実コマンドランナーは未実装であり、`SecretCore` は受理記録のみと整理。理由: 「ProductionTool が eFuse の設計」を文書資産・実コード・本番実施の三本で食い違わせないため。
- 2026-03-27: 参照文書へ `製造・セキュア化手順書.md`、`デバイス管理表.md`、`製品化成果概要書.md` を追加。理由: GUI 化や量産展開に進む前に、工程順序、対象機状態、今回の成果範囲を README から辿れるようにするため。
- 2026-03-24: `PT-002` 入力簡素化（操作者ID任意、作業指示番号空欄時の `YYYYMMDDHHMMSS` 自動採番）と、`PT-003` 一覧行選択方式（MAC再入力不要）を追記。理由: 現場運用で過剰入力を減らし、対象機選択を短時間化するため。
- 2026-03-24: `.env.example.sample.txt` を追加し、README に `.env` 運用手順を追記。理由: 共有時に実値を出さない運用を明文化するため。
- 2026-03-23: install/uninstall 両スモークと EXE 化再開手順を追記。理由: `ISCC.exe` 導入後に 7090 本試験へ迷わず再開できるようにするため。
- 2026-03-22: `installer/` を構成一覧へ追加し、`003-0016` / `009-1021` 向けの Inno Setup スクリプトと安全消去付きアンインストーラ骨格実装を追記。理由: 配布実装の入口と現在地を README から辿れるようにするため。
- 2026-03-19: 現段階の対象フェーズを `004-0008` から `004-0009`（基本機能まで）へ更新。理由: 7087 完了後の現状と README 記載のズレを解消するため。
- 2026-03-19: 直近の実装対象を PT-001〜PT-005 骨格実装済みに更新。`ProductionTool画面仕様書.md` 第8節への参照を追加。理由: 004-0009 完了時点の証跡を README から辿れるようにするため。
- 2026-03-17: `Cargo.toml`、`src/main.rs`、`src/app_config.rs`、`src/audit_logger.rs`、`config/productionTool.settings.example.json` を追加。理由: `004-0008` の最小起動骨格として、独立起動、設定読込、監査ログ初期化、`SecretCore` 事前確認、安全停止までをコード化するため。
- 2026-03-17: 最小実行骨格の入口として新規作成。理由: `004-0008` の実装開始位置を `ProductionTool` フォルダ内で明確化するため。
