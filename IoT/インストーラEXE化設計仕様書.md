# IoT インストーラ EXE 化設計仕様書

[重要] 本書は 009 以降の `LocalServer` / `ProductionTool` 向けインストーラ・アンインストーラの EXE 化設計を定義する。  
理由: 本番・客先環境の導入要件を最小化し、Python 不要で単体 EXE 配布可能にするため。

[厳守] 顧客・工場端末への導入時に「Node.js や Python を別途インストールする」手間を極力省く。  
理由: 導入負荷低減と環境差異による不具合を防ぐため。

[重要] 配布物はインストーラ EXE 1 本のみとする。  
理由: 顧客・工場への配布・ダウンロードを単純化し、exe 一つで済ませるため。

[厳守] アンインストーラはインストーラの実行時に自動生成・配置される。Inno Setup の標準動作により、インストール完了時にアンインストーラがインストール先へ作成され、コントロールパネル「プログラムのアンインストール」へ登録される。  
理由: ユーザーが別途アンインストーラを配布・取得する必要をなくすため。

## 1. 対象とスコープ

| 対象 | 用途 | 配布形式（ユーザーへ渡すもの） |
|------|------|------------------------------|
| LocalServer | 顧客向け通常運用（監視・OTA・AP メンテ） | インストーラ EXE 1 本のみ |
| `ProductionTool` | 工場端末向け製造・eFuse 実行 | インストーラ EXE 1 本のみ |

- アンインストーラはインストール時にインストーラが自動作成する。別途配布しない。

## 2. 技術選定

### 2.1 インストーラ作成ツール（Python 不要）

- [厳守] **Inno Setup** を採用する。  
  理由: 無料・単体 EXE 生成・Pascal スクリプトでカスタム処理可能・Python 不要。
- [禁止] インストーラ作成に Python を必須としない。理由: 客先・工場端末のビルド環境を単純化するため。
- [推奨] Inno Setup の .iss スクリプトをリポジトリで管理し、CI または手動ビルドで EXE を生成する。

### 2.2 LocalServer の実行形式

- [重要] LocalServer は Node.js アプリのため、以下いずれかで「Node.js 事前インストール不要」を目指す。
  - **方針 A**: インストーラに Node.js ポータブル版を同梱し、インストール時に展開する。
  - **方針 B**: Node.js の Single Executable Applications (SEA) で `dist/server.js` を単体 EXE にパッケージする（Node.js v25.5+ の `--build-sea` 利用）。
- [推奨] 方針 B を優先検証する。ネイティブモジュール非依存であれば配布が最もシンプルになる。

### 2.3 `ProductionTool` の実行形式

- [重要] `ProductionTool` は Windows ネイティブアプリ（Rust または .NET）を想定する。
  - **Rust**: `cargo build --release` で単体 EXE 生成。追加ランタイム不要。
  - **.NET**: `dotnet publish -r win-x64 --self-contained` で単体 EXE 生成。.NET ランタイム不要。
- [厳守] `ProductionTool` の GUI はブラウザではなく Windows アプリとする（モジュール仕様書に準拠）。
- [厳守] Rust 共通化は `LocalServer` 側 `SecretCore` 共通部の再利用を意味し、`ProductionTool` 自体を `LocalServer` と統合する意味ではない。

## 3. インストーラ EXE 仕様

### 3.0 共通動作要件
- [厳守] **管理者権限必須**: 起動時に UAC プロンプトを出し、管理者権限で実行する。
- [厳守] **対象 OS**: Windows 11 対応を基準とする（Windows 10 x64 も互換範囲）。
- [重要] **対話的ウィザード形式**: ポート開放や自動起動など、システムに変更を加える自動化処理は、裏で勝手に行わず、インストーラの画面で一つずつ「チェックボックス等で確認・同意」を得てから進める形式とする。

### 3.1 LocalServer インストーラ

| 項目 | 内容 |
|------|------|
| 作成ツール | Inno Setup |
| 入力 | 事前ビルド済み `dist/`、`package.json`、`public/`、`.env.example.sample.txt`、必要に応じて Node ポータブル or SEA |
| 自動化項目 (※要画面確認) | ・Windows ファイアウォール受信許可（TCP 3100, 4443 等）<br>・Task Scheduler への PC 起動時自動実行の登録 |
| 処理内容 | 展開、data/logs/uploads/certs 作成、上記自動化処理の実行 |
| 出力 | `IoT-LocalServer-Setup-YYYYMMDD.exe` |

### 3.2 LocalServer アンインストーラ EXE

| 項目 | 内容 |
|------|------|
| 作成方法 | Inno Setup の標準 uninstaller + カスタム処理 |
| 処理内容 | プロセス停止、Task Scheduler 解除、ファイアウォールルールの削除、機密ファイル上書き消去、監査ログ出力 |
| [厳守] | 安全消去ポリシー 4.1 に準拠し、keyStore.json / securityState.json / settings.json / logs / uploads / certs を上書き消去 |

### 3.3 `ProductionTool` インストーラ / アンインストーラ

- LocalServer と同様に Inno Setup で EXE 化する。
- [厳守] LocalServer 環境への混在インストールを禁止するチェックをインストーラに含める。
- アンインストーラは安全消去ポリシー 4.2 に準拠する。

### 3.4 `ProductionTool` 配布前提（003-0016 / 009-1021 確定値）

| 項目 | 確定値 |
|------|--------|
| 対象端末 | 工場端末（Windows 11 x64）。最小構成: 本番端末 1 台 + 予備端末 1 台 |
| インストール先 | `C:\\Program Files\\IoT\\ProductionTool\\` |
| 監査ログ保存先 | `C:\\ProgramData\\IoT\\ProductionTool\\logs\\audit\\` |
| 作業一時領域 | `C:\\ProgramData\\IoT\\ProductionTool\\work\\` |
| 鍵素材作業領域 | `C:\\ProgramData\\IoT\\ProductionTool\\keys\\`（平文鍵の恒久保存禁止） |
| アンインストール監査ログ | `C:\\ProgramData\\IoT\\InstallerAudit\\ProductionTool\\uninstall-YYYYMMDD-HHMMSS.json` |
| 権限要件 | UAC 管理者権限必須 |
| 混在インストール制約 | `LocalServer` 既存導入端末では `ProductionTool` セットアップを中断し、専用端末へ導線を案内する |

- [厳守] `ProductionTool` インストーラは、`LocalServer` の既存導入痕跡（既定インストール先、サービス/タスク、設定マーカー）を検知した場合、インストールを継続しない。
- [厳守] `ProductionTool` アンインストーラは `安全消去ポリシー.md` 4.2 の対象（鍵素材、実行履歴、一時ファイル）を上書き消去し、成否を監査ログへ残す。
- [重要] これらの確定値は `004-0010` 再判定時の「工場端末配布前提が崩れていないこと」の判定基準として扱う。
- [進捗][2026-03-22] 実装骨格として `ProductionTool/installer/ProductionTool.iss`、`ProductionTool/scripts/build-production-tool-installer.ps1`、`ProductionTool/scripts/uninstall-production-tool.ps1` を追加した。未完了項目は EXE ビルド実施、混在インストール実機確認、7090 による安全消去確認である。
- [進捗][2026-03-23] `ProductionTool/scripts/install-production-tool.ps1` と install/uninstall 両スモーク試験を追加し、実インストール前の配置/消去を擬似環境で前倒し確認できるようにした。実端末側の未了は `ISCC.exe` 導入、`ProductionToolSetup.exe` 実生成、7090 本試験である。
- [重要][2026-03-23] `ProductionTool` の 7090 本試験は「`ISCC.exe` 解決確認 -> `ProductionToolSetup.exe` 生成 -> 工場専用端末へ管理者インストール -> 配置確認 -> 1回起動確認 -> `UninstallRun` 経由アンインストール -> `ProgramData` 消去と監査ログ確認」の順で実施する。
- [厳守][2026-03-23] `ISCC.exe` 未導入、`LocalServer` 混在検知、UAC 権限不足、監査ログ未生成、`keys/work/logs\audit` 残留のいずれかが発生した場合は、7090 本試験を不合格として中断する。

## 4. 導入要件の最小化

### 4.1 顧客・工場端末で不要にしたいもの

| 項目 | 現状 | 目標 |
|------|------|------|
| Python | インストーラ作成時に不要（Inno Setup 採用で達成） | 導入時も不要 |
| Node.js 事前インストール | LocalServer 実行に必要 | 同梱 or SEA で不要化 |
| PowerShell 手動実行 | 手動でPS1を実行しポリシー変更が必要 | EXE 化で不要化 |
| 手作業での環境構築 | フォルダ作成、ファイアウォール設定、タスク登録 | インストーラウィザードで自動化 |

### 4.2 やむを得ず必要とするもの

| 項目 | 理由 |
|------|------|
| Windows 11 (Win10互換) | 対象 OS 基準 |
| TPM 2.0（SecretCore 利用時） | 鍵保護設計 |
| 管理者権限（UAC 必須） | Task Scheduler 登録、ファイアウォール設定等システム変更を伴うため |

## 5. 実装順序（009 以降）

| ID | 正式タスク | 役割 |
|----|------------|------|
| 009-1020 | LocalServer 用インストーラ/アンインストーラ EXE 化 | LocalServer 配布と安全消去を実装する |
| 009-1021 | ProductionTool 用インストーラ/アンインストーラ EXE 化 | ProductionTool 配布と混在インストール防止を実装する |
| 009-1022 | LocalServer Node.js 同梱 or SEA 化方針確定 | 顧客端末の追加ランタイム要求を最小化する |
| 009-1023 | ProductionTool 用 Inno Setup スクリプト確定 | ProductionTool 側のビルド/配布手順を固定する |

1. **009-1020**: LocalServer 用 Inno Setup スクリプト作成（.iss）と、アンインストーラ安全消去（[UninstallRun] 等）を実装し、EXE ビルド手順を `コマンド仕様書.md` へ追記。
2. **009-1021**: `ProductionTool` 用 Inno Setup スクリプト作成（.iss）と、アンインストーラ安全消去、`LocalServer` 環境との混在防止チェックを実装。
3. **009-1022**: LocalServer 向け Node.js 同梱 or SEA 化の方針を確定し、インストーラへ反映。
4. **009-1023**: ProductionTool 向け配布手順（署名、インストール先、監査ログ保存先、アンインストール検証）を確定。

## 6. 関連文書

- `安全消去ポリシー.md`: アンインストール時の消去対象・方法。
- `コマンド仕様書.md`: ビルド・配布コマンド。
- `環境仕様書.md`: 対象 OS・ツール要件。

## 7. 変更履歴
- 2026-03-22: `ProductionTool` 用の Inno Setup スクリプトとビルド/アンインストール補助を実装骨格として追加した旨を追記。理由: `003-0016` / `009-1021` の現在地を設計書から辿れるようにするため。
- 2026-03-19: 第3.4節に `ProductionTool` 配布前提の確定値（対象端末、インストール先、監査ログ保存先、作業領域、混在インストール制約、安全消去監査ログ）を追加。理由: `003-0016` / `009-1021` の方針確定を `004-0010` 再判定に使える形へ固定するため。
- 2026-03-19: `009-1021` の意味を `todo.md` に合わせて `ProductionTool` 用 EXE 化へ統一。`009-1020` は LocalServer の安全消去を含むタスクとして整理し、ID対応表を追加。理由: 同一IDの意味衝突を解消し、ゲート判定時の誤判定を防ぐため。
- 2026-03-16: `ProductionTool` へ名称統一し、Rust 共通化は `LocalServer` 側共通部再利用であってソフト統合ではないことを追記。理由: 配布設計でも名称と分離方針を一致させるため。

- 2026-03-15: 新規作成。009 以降向け EXE インストーラ設計。Inno Setup 採用、Python 不要、導入要件最小化を定義。理由: 本番・客先環境の導入負荷低減のため。
