# IoT DB仕様書

[重要] 本書は `LocalServer` 一体化後の保存仕様（SQLite）を定義する。  
理由: `LocalSoft` 廃止方針に合わせ、保存主体と出力運用を `LocalServer` に統一するため。

## 1. 対象
- `LocalServer` のローカル保存DB（SQLite）
- 保存対象: ESP32 status、コマンド実行結果、監査ログ、エクスポート実行履歴

## 1.1 責任範囲
- [厳守] 本書の対象は `LocalServer` の保存責務とする。
- [廃止の方針] `LocalSoft` は新規利用しない。理由: 保存・参照・出力機能を `LocalServer` へ統合するため。
- [厳守] `SecretCore` は秘密処理主体であり、SQLite へ raw key や秘密値を書き込まない。
- [厳守] `Production` は製造ログ主体であり、本書の通常運用DB対象へ混在させない。
- [厳守] 詳細な責任境界は `モジュール仕様書.md` を親定義とする。

## 2. 保存ポリシー
- [厳守] 保持期間は日単位で設定する。
- [厳守] `retentionDays=0` は無期限保存を意味する。
- [厳守] 既定値は `retentionDays=10` とする。
- [推奨] 日次バッチで期限超過データを削除する。
- [禁止] raw `k-user`、raw `k-device`、ECDH 共有秘密、`k-pairing-session`、秘密鍵実値を保存しない。
- [推奨] 鍵関連で保存可能なのは `keyVersion`、`public_id`、状態フラグ、監査用メタ情報までとする。

## 3. 出力仕様（平文ファイル）
- [重要] 平文ファイル出力は `LocalServer` から実行する。
- [厳守] 出力対象はフィルタ条件を指定可能とする。
  - `command`
  - `subCommand`
  - `deviceNo`（または `deviceName/public_id`）
  - `fromAt` / `toAt`
  - 複合条件: `AND` / `OR`
- [厳守] 出力方式は「自動（継続保存）」と「手動（任意タイミング）」の両方を許可する。
- [禁止] 平文出力ファイルへ秘密値（鍵実体、パスワード実値、トークン実値）を含めない。
- [厳守] 出力実行時は監査ログへ条件・実行者・日時・件数・出力先を記録する。

## 4. テーブル案（初期）
- `deviceStatusHistory`
  - `id` INTEGER PRIMARY KEY AUTOINCREMENT
  - `deviceName` TEXT NOT NULL
  - `publicId` TEXT
  - `macAddr` TEXT
  - `onlineState` TEXT NOT NULL
  - `detail` TEXT
  - `recordedAt` TEXT NOT NULL (ISO8601)
- `commandHistory`
  - `id` INTEGER PRIMARY KEY AUTOINCREMENT
  - `requestId` TEXT NOT NULL
  - `commandName` TEXT NOT NULL
  - `subCommand` TEXT
  - `targetName` TEXT NOT NULL
  - `result` TEXT NOT NULL
  - `detail` TEXT
  - `recordedAt` TEXT NOT NULL
- `exportHistory`
  - `id` INTEGER PRIMARY KEY AUTOINCREMENT
  - `triggerType` TEXT NOT NULL (`manual` / `scheduled`)
  - `filterJson` TEXT NOT NULL
  - `exportPath` TEXT NOT NULL
  - `recordCount` INTEGER NOT NULL
  - `executedBy` TEXT NOT NULL
  - `executedAt` TEXT NOT NULL

## 5. 変更履歴
- 2026-03-13: `LocalSoft` 保存主体を廃止し、`LocalServer` 一体化前提へ全面更新。理由: 保存・参照・出力機能を単一運用面へ統合し、運用・保守の重複を解消するため。
- 2026-03-09: `LocalSoft` / `LocalServer` / `SecretCore` / `Production` の責任範囲と、保存禁止対象の秘密値を追記。理由: 保存主体と秘密処理主体の境界を実装寄り文書でも明確にするため。
- 2026-03-07: 新規作成。SQLite保持仕様（既定10日、0=無期限）を定義。
