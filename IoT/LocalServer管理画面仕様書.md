# LocalServer管理画面仕様書

[重要] 本書は LocalServer の「設定 -> 管理者画面」およびデバイス一覧管理画面の仕様を定義する。  
理由: APモード機体と通常MQTT機体を同一運用画面で扱うため、認証境界と操作範囲を先に固定する必要があるため。

[厳守] 管理者画面への遷移はユーザー名/パスワード認証を必須とする。  
理由: `k-user` 発行、`k-device` 発行、再起動指令など高リスク操作を保護するため。

[厳守] eFuse 設定は `ProductionTool` ツールのみで実行し、LocalServer からは実行不可とする。  
理由: 不可逆処理を通常運用画面から分離し、誤操作・不正操作を防止するため。

## 1. 対象画面
- 設定画面（通常）
- 管理者ログイン画面
- 管理者画面（高権限）
- デバイス一覧画面（AP/MQTT統合表示）

## 2. 管理者認証仕様
### 2.1 認証入力
- `username`
- `password`

### 2.2 認証成功時
- 管理者セッションを発行する。
- 管理者画面へ遷移する。

### 2.3 認証失敗時
- [厳守] エラーメッセージを返し、管理者画面へ遷移させない。
- [推奨] 連続失敗回数を監査ログへ記録する。

## 3. 管理者画面の機能
### 3.1 鍵管理
- `k-user` 発行
- `k-device` 発行

### 3.2 AP一括設定
- Wi-Fi USB インタフェースを指定する。
- `AP-esp32lab-<MAC(no colon)>` を探索する。
- 対象ESP32へ接続し、自動設定を行う。
  - ネットワーク設定
  - パスワード設定
  - `k-device` 設定
- [重要] 「AP探索・接続・設定投入」の Server/OTA 既定値は `port=443` / `TLS=true` とする。  
  理由: [厳守] 認証情報や設定投入内容を平文HTTPで流さないため。
- [重要] AP作業ステータス表示（接続中、接続済、通信中、読み込み中、書き込み中、再起動要求送信済、エラー）を常時表示する。  
  理由: 実機保守時に処理段階を可視化し、失敗箇所の切り分けを迅速化するため。

### 3.3 AP Web手動遷移
- [重要] 一覧表の各AP機体に「Webページへ移動」ボタンを表示する。
- 遷移先: `http://192.168.4.1/`（または取得済みAP内IP）

### 3.4 デバイス一覧統合表示
- APモード接続中のESP32
- 通常モードでMQTT接続中のESP32
- [厳守] 同一一覧で状態を表示する。

### 3.5 一覧表示項目
- デバイス識別子（`public_id` / `shortId` / `deviceName`）
- 接続種別（`AP` / `MQTT`）
- 接続状態（online/offline/processing/error）
- FWバージョン
- FSバージョン
- 書換進捗（OTA・設定適用進捗）
- [重要][2026-03-16] `SecretCore` 有効時の OTA は単一対象機 workflow 状態（`queued` / `running` / `waiting_device` / `verifying` / `completed` / `failed`）を同一一覧へ表示する。
- 最終更新時刻

### 3.6 MQTT経由メンテナンス再起動
- [重要] MQTT接続中のESP32へ `call/maintenance` を送信できること。
- [厳守] 再起動後、AP遷移を想定した状態管理へ更新すること。

## 4. 画面操作権限
- 一般設定画面:
  - 閲覧中心
- 管理者画面:
  - `k-user` / `k-device` 発行
  - AP一括設定
  - AP Web 手動遷移
  - MQTT メンテナンス再起動
- [禁止] eFuse 実行ボタンを管理者画面へ表示しない。

## 5. API対応方針
- [厳守] 管理者画面系APIは管理者セッション必須とする。
- [厳守][003-0001] OTA実行系API（`POST /api/commands/ota`、`POST /api/workflows/signed-ota/start`、`GET /api/workflows/{workflowId}`）は管理者トークン（`Authorization: Bearer <token>`）必須とする。同一セッション内3時間有効、sessionStorage使用でブラウザ終了時に失効。
- [厳守][2026-03-16] Web UI の signed OTA は単一対象機のみ対応とし、「全体」一括実行は未対応表示とする。理由: Stage7 workflow が単一対象機前提のため。
- [厳守] AP一括設定APIは「対象一覧」「処理状態」「結果要約」を返す。
- [推奨] 一覧APIは AP / MQTT を統合して返す。
- [重要] MQTT通信は「トピック平文 + payload全文暗号化（k-device/A256GCM）」を標準とし、管理画面からのコマンド送信も同方式を使用する。  
  理由: MQTTモニタ上で運用機密（args/設定値）が露出しないようにするため。
- [推奨] 互換運用の切替はサーバ設定（`MQTT_PAYLOAD_ENCRYPTION_MODE`）で制御し、UI上に平文切替ボタンは設けない。  
  理由: 誤操作で平文運用へ戻る事故を防ぐため。

## 6. 変更履歴
- 2026-03-16: [003-0012] `index.html` を signed OTA workflow UI へ接続し、単一対象機の workflow 状態表示を一覧へ追加。`otaAllButton` は未対応表示へ変更。理由: Rust 側 Stage7 実装済み workflow を UI まで貫通し、TS 側が逐次手順を持たない構成へ近づけるため。
- 2026-03-14: [003-0001] OTA再認証を実装。server.ts: /api/commands/ota に requireAdminSession を追加。index.html: OTA実行前に管理者認証モーダルを表示、トークン送信、401時は再ログインを促す。同一セッション内3時間有効、sessionStorageでブラウザ終了時に失効。理由: OTA実行の誤操作・不正実行を防止するため。
- 2026-03-14: [003-0011] eFuse 実行不可の担保。server.ts で `/api/admin/efuse`, `/api/commands/efuse` を 404 で明示拒否。admin.html / index.html に「[禁止] eFuse 実行ボタン・UIを追加しない」コメントを追加。理由: IF仕様書「eFuse 操作 API は LocalServer 側へ実装しない」を担保するため。
- 2026-03-14: [003-0008] AP Web手動遷移を実装。index.html 操作列に apWebUrl 時「Webページへ移動」、admin.html AP探索横に同ボタン、管理対象一覧に AP機体の Webリンクを追加。理由: 実機保守時の手動設定導線を明確化するため。
- 2026-03-11: 新規作成。理由: LocalServer 管理者画面の認証境界、鍵発行、AP一括設定、統合一覧、MQTTメンテナンス再起動を仕様として固定するため。
- 2026-03-12: AP探索・接続・設定投入の既定値を `Server/OTA=443,TLS=true` へ統一。理由: `IoT/ESP32/header/sensitiveData.h` と LocalServer UI/API の整合性を取り、セキュア既定値へ統一するため。
- 2026-03-12: 管理者画面経由コマンドの MQTT payload 暗号化方針（`k-device` / `A256GCM`）と運用切替方針（設定ファイル制御）を追記。理由: 本日実装した暗号化モジュール運用を画面仕様へ反映するため。
