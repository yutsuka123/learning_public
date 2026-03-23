# ProductionTool `tests`

[重要] 本フォルダは `ProductionTool` 基本機能の試験補助資材格納先です。  
理由: `7086`〜`7088` と `004-0008`〜`004-0010` を実装フォルダの近くで管理し、再現性を高めるため。

## 想定対象
- 起動・分離試験
- 追加認証試験
- 対象機誤選択防止試験
- dry-run 停止点確認
- 監査ログ出力確認
- 安全停止確認
- 安全消去スモーク試験

## 現在の主要試験補助
- `testProductionToolInstallHelperSmoke.ps1`
  [重要][2026-03-23] 一時ディレクトリ上に擬似 `installRoot` / 擬似 `ProgramData` / 擬似 `installer-audit` を作成し、`install-production-tool.ps1` の配置結果と監査ログ生成を確認する。
- `test7090ProductionToolUninstallSmoke.ps1`
  [重要][2026-03-23] 一時ディレクトリ上に擬似インストール先と擬似 `ProgramData` を作成し、`uninstall-production-tool.ps1` の安全消去・監査ログ出力を確認する。

## 実装時の注意
- [厳守] 不可逆処理本体の自動試験をこの段階で混在させない。
- [厳守] 秘密値を固定値サンプルとして保存しない。
- [推奨] 試験番号と対応する補助スクリプト名・記録ファイル名を対応づける。

## 変更履歴
- 2026-03-23: `testProductionToolInstallHelperSmoke.ps1` を追加。理由: `install-production-tool.ps1` を実インストール先に依存せず検証できるようにするため。
- 2026-03-23: `test7090ProductionToolUninstallSmoke.ps1` を追加。理由: 実インストール先を壊さずに `7090` の ProductionTool 側安全消去を前倒し検証できるようにするため。
- 2026-03-17: 新規作成。理由: 基本機能試験の置き場を先に固定するため。
