# CLAUDE.md — Claude Code / Cursor 共通プロジェクト引継ぎノート

[重要] 本書は **Claude Code と Cursor の両方** で読み込まれる引継ぎノートである。セッションや使用ツールが変わってもプロジェクト状態を素早く把握できるようにすることを目的とする。  
理由: 本プロジェクトは Cursor と Claude Code を **併用**しており、どちらを起動しても同じ判断ルールに従えるよう、設定ファイルを相互参照する形で同期管理しているため。

[相互参照] 本ファイルとペアになる Cursor 公式設定は **`.cursorrules`**（リポジトリルート）。判断ルールは下記の優先順で適用する：
1. **`.cursorrules`** 冒頭の「現在地ブロック」（最重要事項・Git タグ・進捗）
2. **`CLAUDE.md`** §5 重要ルール（機密情報、Git 運用、タスク管理、セキュア設計の正本）
3. **`IoT/todo.md`** の「再開インデックス」「直近ゴール」「次セッション開始メモ」
4. **`IoT/ドキュメント概要.md`** から個別仕様書

[ツール切替時の注意] Cursor → Claude Code、または逆へツールを変える場合、`.cursorrules` と `CLAUDE.md` が同期されていることを確認する。差分があれば変更履歴の日付で新しい方を正とする。**最終整合確認日：2026-05-23（012-0008 全件クリア・7205/7206 OTA PASS）**。

[参照] 詳細ルールは `.cursorrules`、文書索引は `IoT/ドキュメント概要.md`、未完了タスクは `IoT/todo.md` を参照。本書はそれらの **最重要点だけ** を抽出する。

---

## 1. プロジェクト概要

**`learning_public`** リポジトリの中核は **`IoT/`** 配下の IoT プロジェクト（ESP32-S3 + Local + Cloud）。  
- 目的：ESP32 デバイスを PC のローカルサーバ越しに安全に制御・更新する第3段階セキュア化構成を確立し、将来クラウド（AWS）へ拡張する。
- 構成：**ESP32-S3** ファームウェア（C++/PlatformIO）+ **LocalServer** (TypeScript/Node.js) + **SecretCore** (Rust 秘密処理層) + **ProductionTool** (Rust 工場ツール) + **Mosquitto** (MQTT broker) + **CoreDNS** + **ルータ**（`172.17.1.x`）。
- その他言語（`csharp/` / `cpp_m/` / `python/` / `rust/` / `typescript/` 等）は **学習用最小サンプル** が置いてある。

---

## 2. 現在地（2026-05-23 セッション終了時点・012-0008 全件クリア・012-0009 進行中）

### Git タグ
- **`v1.0.0-local`**：ローカル環境構築完了バージョン（2026-05-19）
- **`local-env-complete-20260519`**：マイルストーンタグ
- **`v1.1.0-cloud-ota`**：クラウド OTA S3 配信完了マイルストーン（2026-05-23 予定）← `012-0009` 完了後に付与

### 進捗
- **未完了 11 / 90（約 88%）**
- 直近成果（2026-05-23）：**`012-0008` / `012-0009` 全件クリア・`012` 章完了**。S3 presigned URL 経由クラウド OTA・dual-partition 動作確認・文書整合完了。タグ `v1.1.0-cloud-ota` 付与。
- 主要バグ修正（本セッション）：IoT Policy `esp32lab/*` 拡張 / `ota.cpp` cloud mode AmazonRootCA1.pem 切替

### 残タスク
| 章 | 件数 | 内容 |
|---|---|---|
| **007** 章 | 0 件 | 全件 `020-0011`〜`020-0013` へ移管済み（空） |
| **008** 章 | 0 件 | 全件退避済み |
| **009** 章 | 1 件 | `009-0004`（`012` 章移管済み・前提待ち） |
| **010** 章 | 3 件 | `010-0014` ブローカ専用 LAN 分離 / `010-0011`/`010-0012` DNS 将来対応 |
| **011** 章 | 0 件 | 完了 |
| **012** 章 | 0 件 | 全件完了（2026-05-23）。`012-0008` / `012-0009` 退避済み → `todo_old20260523.md` |
| **013** 章 | 0 件 | 全件完了（文書整備、2026-05-19） |
| **020** 章 | 17 件 | 将来対応（`020-0011`〜`020-0025`） |

### 実環境の現在状態（2026-05-23 セッション終了時点）
- **ESP32**：local mode（`brokerMode=local`, `AP-IoTESP32Test`, IP=`172.17.1.200`）。FW=`1.1.0-beta.40`（cloud OTA 試験後・ローカル復帰済み）
- **LocalServer**：local mode（`CLOUD_MQTT_ENABLED=false`）
- **Mosquitto**：稼働中
- **CoreDNS**：`Corefile bind 172.17.1.100` 限定（cloud 試験で適用済み）
- **AWS**：無接続（課金なし）

### cloud モードへ戻す手順（参考）
1. `IoT/LocalServer/.env` の `CLOUD_MQTT_ENABLED=true`
2. LocalServer 再起動
3. 回復スクリプト `cloud` 引数で実行: `node scripts/createSensitiveDataJsonForRecovery.cjs IoT_04CEF94EB580 cloud`
4. ESP32 へ `pio run -e esp32s3_secure --target uploadfs --upload-port COM4`
5. ESP32 自動リセット → Buffalo-G-41E0 + AWS IoT Core で再接続

---

## 3. 次の本線：012-0009 まとめ（012 章クラウド連携は 0008 まで完了）

### 012 章完成状況（2026-05-23）
- **`012-0001`〜`012-0008`** ✅ 完了（AWS IoT Core 接続 + TLS + MQTT + OTA S3 全て動作確認済み）
- **`012-0009`** 進行中（まとめ・文書整合）

### 012-0009 残作業
1. `設計議事録20260520.md` に実装結果・試験証跡を追記
2. `クラウド構築手順書.md` を実施結果で更新
3. `ドキュメント概要.md` / `設計概要.md` に 012 章クラウド連携完了の相互参照を同期
4. `todo_old20260523.md` に 012-0008 を退避
5. Git コミット + タグ **`v1.1.0-cloud-ota`** 付与

### 完成した 012 章の構成（参考）
- **AWS IoT Core**（東京リージョン `ap-northeast-1`）を MQTT broker として使用
- **TLS 8883** + **X.509 クライアント証明書認証**（mTLS）
- **IoT Policy** でトピック単位の認可（`esp32lab/*`）
- **S3 presigned URL** 経由のクラウド OTA（TTL=600s、AmazonRootCA1.pem で TLS 検証）
- LocalServer がブローカ役を「ローカル Mosquitto + クラウド AWS IoT」のハイブリッドで担う

---

## 4. セッション開始時に読む順

[厳守] **新セッション開始時は必ず以下の順で読む**。

[相互参照の理由] 本プロジェクトは **Cursor（`.cursorrules` 自動読込）** と **Claude Code（`CLAUDE.md` 自動読込）** を **併用**する。`.cursorrules` は Cursor エージェント用の公式設定ファイルであり、Claude Code はそれを自動読み込みしない。しかし両ツールが同じプロジェクト・同じ判断ルールで動くよう、Claude Code セッション開始時にも `.cursorrules` を明示的に Read して同期する。

| 順 | ファイル | 用途 |
|---|---|---|
| 0 | **`.cursorrules`**（リポジトリルート）| **Cursor 用公式設定・現在地ブロック・Git タグ・進捗・判断ルール優先順の正本**。Cursor と Claude Code の**併用同期**のため毎セッション冒頭で明示的に Read すること（Claude Code では自動読込されないため） |
| 1 | `IoT/todo.md` | **現在地サマリ + 直近ゴール + 次セッション開始メモ + 進捗統計（直近 5 日）** |
| 2 | `IoT/システム構成と通信フロー.md` | 初心者向け全体像（ASCII 構成図、通信経路、シーケンス、用語辞典）|
| 3 | `IoT/セキュア全般ノウハウ_設計から実装まで.md` | セキュア技術深掘り（18 章、約 1200 行）|
| 4 | `IoT/設計書実装マッピング表.md` | 設計書 ↔ 実装ファイル:行 の横串表 |
| 5 | `IoT/ドキュメント概要.md` | 全文書索引 |
| 6 | 必要に応じて `IoT/設計概要.md` / `IoT/モジュール仕様書.md` / `IoT/機能仕様書.md` 等 |

---

## 5. 重要ルール（要点抜粋）

### Git
- **コミットは明示的に依頼されたときのみ**
- コミットメッセージ：`260519-N` 形式（日付 + 連番）+ 詳細本文（HEREDOC 推奨）
- **メインへのマージ運用**：worktree でコミット後、`git -C メイン merge claude/clever-swartz-48653b --no-ff` でメインへ反映。ユーザーがローカル確認できるよう毎タスク後にマージする方針。

### 機密情報
- **Git に絶対に上げない**：実パスワード、raw 鍵値、AWS 認証情報、eFuse 実値、証明書秘密鍵、`wrapped_secret.bin` 中身
- 実値は `.env` / `*.json`（Git 除外設定済み）に置き、サンプルは `*.sample.txt` / `*Sample.json` で構造のみ共有
- AWS 認証情報の保存先：`IoT/LocalServer/.env`（`.gitignore:10` で除外確認済み）

### 進捗統計テーブル運用ルール
- 同日の複数回更新は **1 行に集約**
- 保持は **直近 5 日分** まで
- それより古い詳細経緯は `## 変更履歴` 節と各 `todo_oldYYYYMMDD.md` を参照

### タスク管理
- **未完了タスクのみ管理**：完了したら `todo_oldYYYYMMDD.md` へ退避
- **既存 ID は再採番しない**（章番号3桁-項目番号4桁、欠番許容）
- **完了時の同時更新**：関連仕様書・試験書・問題点記録書も同日中に更新

### セキュア設計の正本
- 鍵保護：**Windows DPAPI**（TPM 不使用、2026-05-19 撤回）。Mac/Linux 汎用化は `020-0004`
- k-device 導出：**HMAC-SHA256(key=k-user, message=publicId)**（HKDF ではない、2026-05-19 整合）
- MQTT topic：**`esp32lab/<kind>/<sub>/<name>`**（`device/<public_id>/wifi/update` 案は不採用）
- 「Dual バンク」の正式名称：**OTA Rollback / `BOOTLOADER_APP_ROLLBACK_ENABLE` / app0+app1 dual partition**

---

## 6. 環境情報

### 主要パス
- **メインリポジトリ**：`C:\mydata\project\myproject\learning_public\`
- **worktree（作業領域）**：`C:\mydata\project\myproject\learning_public\.claude\worktrees\<branch>\`
- **AWS 認証**：`~/.aws/credentials`（default プロファイル、東京リージョン）

### 起動・確認コマンド
- LocalServer 起動：`cd IoT/LocalServer && nvm use 22 && npm run start`
- LocalServer 健康確認：`curl http://localhost:3100/api/health`
- SecretCore ビルド：`cd IoT/SecretCore && cargo build`
- ESP32 ビルド：`cd IoT/ESP32 && python -m platformio run -e esp32s3_secure`
- 詳細：`IoT/日常運用_LocalServerとSTA接続_クイックリファレンス.md`

### Node.js バージョン
- **v22 必須**（`better-sqlite3` ネイティブモジュールが v22 でビルド済み）
- `nvm use 22` で切替。v20 のままだと `ERR_DLOPEN_FAILED` で起動失敗

---

## 7. 注意事項

### 不可逆処理（`007-B`）
- **No-Go 継続中**：`ProductionTool` 最終形実ランナー完成と本番1台目入口ゲート充足まで実行しない
- `espefuse` / `espsecure` を本番個体に直接実行しない（必ず `ProductionTool` 経由）
- 詳細は `020-0011` / `020-0012` / `020-0013`（旧 007 章）と `本番セキュア化出荷準備試験計画書.md` §6.4

### 文書整合性
- `todo.md` の進捗統計と `## 変更履歴` の最新は **必ず同期**
- 設計変更時は **設計書 + 実装 + 試験仕様 + 機能仕様** を同時更新
- 「Dual バンク」「TPM」「HKDF」等の旧用語は本日（2026-05-19）整合済みなので **再導入しない**

### 質問の答え方
- 探索的な質問（「どうするか」「何ができるか」）には **2-3 文で提案 + トレードオフ**、ユーザー合意後に実装
- 大規模変更は **`AskUserQuestion`** で選択肢を提示
- 一度に大量のクローズはせず、**「実装済みか / 既検討済みか / 差分があれば必要性判断 / 差分実装の要否」** の 4 観点で精査してからクローズ

---

## 99. 変更履歴

- 2026-05-23: `012-0008` / `012-0009` 全件クリア・`012` 章完了。進捗 11/90（約 88%）。主要バグ修正：IoT Policy `esp32lab/*` 拡張 / `ota.cpp` cloud mode AmazonRootCA1.pem 切替。コミット `260523-1`・タグ `v1.1.0-cloud-ota` 付与。`§2` / `§3` を最終状態へ更新。
- 2026-05-21: `012-0008` クラウド連携試験 7 項目クリア（7200/7201/7203/7204/7207/7208/7209）に伴い「§2 現在地」を更新。進捗 13/90（約 86%）。主要バグ修正 `#0047`/`#0048`/`#0049` を記載。OTA（7205/7206）は `020-0025`（S3 化）後に延期。
- 2026-05-19: 新規作成。ローカル環境構築完了 v1.0.0-local の節目で、セッション・ツール間引継ぎ用ノートを整備。理由: Claude Code・Cursor・他 AI ツール間でセッションを跨いでも、プロジェクト現在地と次のステップを素早く把握できるようにするため。
