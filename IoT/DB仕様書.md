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
- [厳守] `ProductionTool` は製造ログ主体であり、本書の通常運用DB対象へ混在させない。
- [厳守] 詳細な責任境界は `モジュール仕様書.md` を親定義とする。

## 2. 保存ポリシー
- [厳守] 保持期間は日単位で設定する。**実効の正本**は `data/settings.json` の `localHistoryRetentionDays`（`LocalServer` 設定 API `GET/PUT /api/settings`）とする。
- [厳守] `localHistoryRetentionDays = 0` は無期限保存（期限パージなし）を意味する。
- [厳守] `1`〜`99999` は「その日数より古い行を削除する」（LocalServer プロセスの時計・`recordedAt` / `executedAt` の UTC ISO8601 比較）を意味する。
- [厳守] 既定値は **`30` 日**とする（新規 `settings.json` 生成時およびキー欠落時のマージ既定。初回のみ環境変数 `LOCAL_HISTORY_RETENTION_DAYS` が `defaultSettings` の種になりうる。実運用では UI/API で設定を確定すること）。
- [推奨] プロセス内のパージ間隔は `LOCAL_HISTORY_PURGE_INTERVAL_MS`（既定 24h、最小 60s）で行う（`localHistoryStore.purgeExpired`）。
- [禁止] raw `k-user`、raw `k-device`、ECDH 共有秘密、`k-pairing-session`、秘密鍵実値を保存しない。
- [推奨] `LocalServer` が SQLite の `commandHistory.detail` に保存する JSON は、`mqttGateway.ts` がキー名に応じ機密値を `<password>` / `<token>` / `<keyMaterial>` 等の角括弧ラベルへ置換する（後から検索可能にしつつ平文保存を避ける）。完全な漏えい防止は送信ペイロード設計（機密を `args` に載せない）が主防衛線とする。

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
- [実装索引][2026-05-17] 手動エクスポート: `POST /api/admin/local-history/export`（管理者トークン必須）。**DB ファイル全消去・空再作成**（ irrevocable ）: `POST /api/admin/local-history/delete-database`（`confirm: "DELETE_LOCAL_HISTORY_DB"`）。**定期エクスポート**: 環境変数 `LOCAL_HISTORY_SCHEDULED_EXPORT_ENABLED` 等（`config.ts`）。いずれも `exportHistory` テーブルおよび `security-audit.log` の `localHistoryExported` / `localHistoryDatabaseReset` を参照。

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
- 2026-05-17（続⁵）: 2章を **`settings.json` の `localHistoryRetentionDays` 正本**へ更新（既定 **30** 日、`0`＝無期限、`1`〜`99999`）。3章に **DB 全削除 API** 索引を追記。理由: 運用設定で保持方針とファイル削除を一元化するため。
- 2026-05-17（続⁴）: 3章に **定期エクスポート**（`LOCAL_HISTORY_SCHEDULED_EXPORT_*`、`triggerType=scheduled`）の実装索引を追記。理由: 自動／手動の両方を本書から一方通行で辿れるようにするため。
- 2026-05-17（続³）: 3章に手動エクスポート API（`POST /api/admin/local-history/export`）の実装索引を追記。理由: 平文出力・監査記録の実体を本書から一方通行で辿れるようにするため。
- 2026-05-17（続²）: SQLite `commandHistory.detail` のマスク表記を `<password>` / `<token>` 等の英語角括弧ラベルへ変更（実装: `mqttGateway.ts` の `getHistoryRedactionPlaceholderForKey`）。理由: マスク種別が人間可読となり、監査・検索時の判断をしやすくするため。
- 2026-05-17（続）: SQLite `commandHistory.detail` の運用方針として、キー名ベースで機密値をマスクする旨を追記（実装: `mqttGateway.ts`）。理由: 履歴の検索性と機密平文保存回避の両立のため。
- 2026-05-17: `LocalServer` 実装済みストア（`localHistoryStore.ts`）で本書 4章テーブルを作成・利用開始。環境変数: `LOCAL_HISTORY_DB_PATH` / `LOCAL_HISTORY_RETENTION_DAYS` / `LOCAL_HISTORY_PURGE_INTERVAL_MS`。理由: 009-0002 第1段（SQLite 保存）のコード実体を本書と突き合わせられるようにするため。
- 2026-03-13: `LocalSoft` 保存主体を廃止し、`LocalServer` 一体化前提へ全面更新。理由: 保存・参照・出力機能を単一運用面へ統合し、運用・保守の重複を解消するため。
- 2026-03-09: `LocalSoft` / `LocalServer` / `SecretCore` / `ProductionTool` の責任範囲と、保存禁止対象の秘密値を追記。理由: 保存主体と秘密処理主体の境界を実装寄り文書でも明確にするため。
- 2026-03-07: 新規作成。SQLite保持仕様（既定10日、0=無期限）を定義。
