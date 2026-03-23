# ProductionTool `scripts`

[重要] 本フォルダは `ProductionTool` の開発補助スクリプト格納先です。  
理由: ビルド補助、試験補助、ログ採取補助を本体実装から分離するため。

## 想定対象
- 起動確認補助
- ログ採取補助
- テストデータ準備補助
- インストーラ作成補助

## 現在の主要スクリプト
- `install-production-tool.ps1`
  [重要][2026-03-23] `ProductionTool.exe`、設定、補助スクリプト、`ProgramData` 配下の `logs\audit` / `work` / `keys` を配置するインストール補助スクリプト。`LocalServer` 混在痕跡がある場合は中断する。
- `build-production-tool-installer.ps1`
  [重要][2026-03-22] `cargo build --release` と Inno Setup (`ISCC.exe`) を連携し、`ProductionToolSetup.exe` を生成する補助スクリプト。
- `uninstall-production-tool.ps1`
  [重要][2026-03-22] `安全消去ポリシー.md` 4.2 に従い、鍵素材・実行履歴・一時ファイルを上書き消去し、`C:\ProgramData\IoT\InstallerAudit\ProductionTool\` へ監査ログを残す。

## 実装時の注意
- [厳守] セキュリティ解除を前提にした一時しのぎスクリプトを常設しない。
- [厳守] 実運用の秘密値を引数やファイルへ平文保存しない。
- [推奨] 実行手順は `IoT/コマンド仕様書.md` と同時更新する。

## 変更履歴
- 2026-03-23: `install-production-tool.ps1` を追加。理由: `003-0016` のインストール側を、実配布前でも擬似環境で確認できる形へ前倒し実装するため。
- 2026-03-22: `build-production-tool-installer.ps1` と `uninstall-production-tool.ps1` を追加。理由: `003-0016` / `009-1021` のインストーラ骨格と安全消去付きアンインストーラ骨格を配置するため。
- 2026-03-17: 新規作成。理由: 補助スクリプトの配置先を先に固定するため。
