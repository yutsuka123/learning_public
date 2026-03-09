# IoT DB仕様書

[重要] 本書は LocalSoft（Python）の保存仕様を定義する。  
理由: データ保持要件と削除ポリシーを実装前に固定するため。

## 1. 対象
- LocalSoft のローカル保存DB（SQLite）
- 保存対象: ESP32 status、コマンド実行結果、監査ログ

## 1.1 責任範囲
- [厳守] 本書の対象は `LocalSoft` の保存責務のみとする。
- [厳守] `LocalServer` は通常運用 UI、REST API、OTA 実行管理、監査表示の主体であり、SQLite 保存主体にはしない。
- [厳守] `SecretCore` は秘密処理主体であり、SQLite へ raw key や秘密値を書き込まない。
- [厳守] `Production` は製造ログ主体であり、本書の運用DB対象へ混在させない。
- [厳守] 詳細な責任境界は `モジュール仕様書.md` を親定義とする。

## 2. 保存ポリシー
- [厳守] 保持期間は日単位で設定する。
- [厳守] `retentionDays=0` は無期限保存を意味する。
- [厳守] 既定値は `retentionDays=10` とする。
- [推奨] 日次バッチで期限超過データを削除する。
- [禁止] raw `k-user`、raw `k-device`、ECDH 共有秘密、`k-pairing-session`、秘密鍵実値を保存しない。
- [推奨] 鍵関連で保存可能なのは `keyVersion`、`public_id`、状態フラグ、監査用メタ情報までとする。

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
- 2026-03-09: `LocalSoft` / `LocalServer` / `SecretCore` / `Production` の責任範囲と、保存禁止対象の秘密値を追記。理由: 保存主体と秘密処理主体の境界を実装寄り文書でも明確にするため。
- 2026-03-07: 新規作成。SQLite保持仕様（既定10日、0=無期限）を定義。
