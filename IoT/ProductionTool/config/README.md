# ProductionTool `config`

[重要] 本フォルダは `ProductionTool` の設定ひな形、既定値、配布設定の格納先です。  
理由: 実装コードと設定を分離し、工場端末配布時の差し替え点を明確にするため。

## 想定対象
- 監査ログ保存先の既定値
- dry-run 関連設定
- 画面表示用の定数
- インストーラ連携用の設定ひな形

## 現在の主要設定ひな形
- `productionTool.settings.example.json`
  [重要] 開発実行向けの最小設定ひな形。監査ログは相対パス `logs` を使用する。
- `productionTool.settings.installed.example.json`
  [重要][2026-03-22] インストール版向けの設定ひな形。監査ログは `C:\ProgramData\IoT\ProductionTool\logs\audit` を使用する。

## 実装時の注意
- [厳守] 実パスワード、秘密鍵、raw key、実運用機密を配置しない。
- [厳守] `.local` や `.secret` 系のローカル設定は Git 管理対象へ含めない。
- [推奨] 配布物ごとの差分は本フォルダで吸収し、ソース本体へ直書きしない。

## 変更履歴
- 2026-03-22: `productionTool.settings.installed.example.json` を追加。理由: 開発用相対パス設定と、インストール版の `ProgramData` ベース設定を分離するため。
- 2026-03-17: 新規作成。理由: 設定ファイルとコードの責務を先に分離するため。
