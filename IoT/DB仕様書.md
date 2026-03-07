# IoT DB仕様書

[重要] 本書は LocalSoft（Python）の保存仕様を定義する。  
理由: データ保持要件と削除ポリシーを実装前に固定するため。

## 1. 対象
- LocalSoft のローカル保存DB（SQLite）
- 保存対象: ESP32 status、コマンド実行結果、監査ログ

## 2. 保存ポリシー
- [厳守] 保持期間は日単位で設定する。
- [厳守] `retentionDays=0` は無期限保存を意味する。
- [厳守] 既定値は `retentionDays=10` とする。
- [推奨] 日次バッチで期限超過データを削除する。

## 3. テーブル案（初期）
- `deviceStatusHistory`
  - `id` INTEGER PRIMARY KEY AUTOINCREMENT
  - `deviceName` TEXT NOT NULL
  - `macAddr` TEXT
  - `onlineState` TEXT NOT NULL
  - `detail` TEXT
  - `recordedAt` TEXT NOT NULL (ISO8601)
- `commandHistory`
  - `id` INTEGER PRIMARY KEY AUTOINCREMENT
  - `requestId` TEXT NOT NULL
  - `commandName` TEXT NOT NULL
  - `targetName` TEXT NOT NULL
  - `result` TEXT NOT NULL
  - `detail` TEXT
  - `recordedAt` TEXT NOT NULL

## 4. 変更履歴
- 2026-03-07: 新規作成。SQLite保持仕様（既定10日、0=無期限）を定義。
