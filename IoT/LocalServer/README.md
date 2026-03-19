# LocalServer (TypeScript + Node.js)

[重要] 本モジュールは、複数ESP32をWeb画面で管理し、MQTT command（status/OTA）を発行するローカル管理サーバーです。  
理由: ローカル常駐で監視・制御・OTA配布を一元化するため。

## 0. 責任範囲
- [厳守] `LocalServer` は通常運用 UI、REST API、MQTT 制御、OTA 実行管理、AP セッション管理、監査表示を担当する。
- [厳守] 秘密処理は `SecretCore` が担当し、`LocalServer` は raw `k-user`、raw `k-device`、ECDH 共有秘密、`k-pairing-session` を保持しない。
- [厳守] `ProductionTool` は製造、初回セキュア化、eFuse 実行主体であり、通常運用の `LocalServer` と混在させない。
- [厳守] `ProductionTool` は `LocalServer` と別ソフト、別実行物として独立動作させる。
- [厳守] 詳細な責任境界は `IoT/モジュール仕様書.md` を親定義とする。

## 1. 提供機能
- [厳守] Web GUI（ブラウザ）でESP32一覧を表示（online/offline/unknown）。
- [厳守] `status` 取得要求を単体/選択/全体へ送信。
- [厳守] `otaStart` 要求を単体/選択/全体へ送信。
- [重要] 起動時に `status` call を自動送信（初期同期）。
- [重要] OTA配布用エンドポイントを提供（`/ota/manifest.json`, `/ota/firmware.bin`）。
- [重要] AP 共通トップ画面の入力検証、`runPairingSession()` 要求前の前段チェック、進行状態表示を担当する。
- [厳守] 設定 -> 管理者画面遷移時は `username` / `password` 認証を必須とする。
- [重要] 管理者画面では `k-user` / `k-device` 発行、AP一括設定、統合一覧表示、MQTTメンテナンス再起動を提供する。
- [厳守] eFuse 実行機能は `ProductionTool` 専用とし、LocalServer では提供しない。

## 2. セットアップ
1. `env.example.sample.txt` をコピーして `.env` を作成する。
2. `.env` の値を環境に合わせて編集する。
3. [重要] 通信先は `*_HOST_NAME` と `*_HOST_IP` の両方を設定する。
4. [推奨] 通常運用では `*_HOST_NAME` を正規値とし、`*_HOST_IP` は障害切り分けやDNS不達時の代替値として保持する。
5. 依存導入: `npm install`
6. 開発起動: `npm run dev`
7. 本番ビルド: `npm run build`
8. 本番起動: `npm run start`

## 3. 疎通確認
- MQTT接続試験: `npm run test:connect`
- APIヘルス確認: `GET http://localhost:3100/api/health`
- GUI: `http://localhost:3100/`
- [推奨] MQTT接続試験時は `.env` の `MQTT_HOST_NAME` / `MQTT_HOST_IP` / `MQTT_FALLBACK_IP` の整合を確認する。

## 4. OTA HTTPS
- [厳守] 証明書と秘密鍵を `certs/server.crt`, `certs/server.key` へ配置する。
- 未配置時はOTA HTTPSサーバーを起動せず警告ログを出力する。
- 配布バイナリは `ota/firmware.bin` に置く。

## 5. インストール/アンインストール（003-0015）
- インストール: `scripts/install-local-server.ps1` を実行（npm install / build、data/logs/uploads/certs 作成、オプションで Task Scheduler 登録）。
- アンインストール: `scripts/uninstall-local-server.ps1` を実行（プロセス停止、Task Scheduler 解除、機密データ上書き消去、監査ログ出力）。
- [厳守] 詳細は `IoT/コマンド仕様書.md` 4.1 を参照する。

## 6. 自動起動（Task Scheduler）
- PowerShell（管理者）で `scripts/install-task-scheduler.ps1` を実行。
- 実行後、PC起動時に `npm run start` が自動起動する。

## 7. API一覧（最小）
- `GET /api/health`
- `GET /api/devices`
- `POST /api/commands/status` body: `{ "targetNames": "all" }` または `{ "targetNames": ["IoT_xxx"] }`
- `POST /api/commands/ota` body: `{ "targetNames": "all" }` または `{ "targetNames": ["IoT_xxx"] }`（[003-0001] `Authorization: Bearer <adminToken>` 必須）
- [重要][2026-03-16] 完了確認まで到達している workflow API は次の 2 つ。
- `POST /api/workflows/signed-ota/start`
- `GET /api/workflows/{workflowId}`
- [進捗][2026-03-16] `POST /api/workflows/pairing/start`
  - TS 側の REST 窓口、`requestedSettings` 直接検証、`ap/configure` 系入力からの変換 helper 呼び出しを実装済み
  - Rust 側の workflow 骨格（`queued -> running -> waiting_device -> verifying -> failed(placeholder)`）、`createPairingBundle` 相当の内部生成、AP モード到達・認証 precheck、ESP32 `GET /api/pairing/state` 参照、`POST /api/pairing/session` による session metadata 登録、`POST /api/pairing/bundle-summary` による非秘密 bundle summary staging、`POST /api/pairing/transport-session` による transport 準備、`POST /api/pairing/transport-handshake` による P-256 ECDH handshake は実装済み
  - [将来対応] `SecretCore` の encrypted bundle 本体送達 / ESP32 側復号 / NVS 完了判定を追加後に end-to-end 有効化する
- [将来対応] `POST /api/workflows/key-rotation/start`
- [将来対応] `POST /api/workflows/production/start`
- `POST /api/admin/auth/login`
- `POST /api/admin/auth/logout`
- `POST /api/admin/keys/k-user/issue`
- `POST /api/admin/keys/k-device/issue`
- `POST /api/admin/ap/batch/start`
- `GET /api/admin/ap/batch/{batchId}`
- `GET /api/admin/devices`
- `POST /api/admin/commands/maintenance-reboot`

## 8. セキュリティ境界
- [厳守] `LocalServer` は `SecretCore` の用途固定 API を呼び出すのみとし、汎用署名APIや raw key 取得APIを持たない。
- [厳守] `runPairingSession()` 実行時は Wi-Fi / MQTT / OTA / 認証情報の必須項目を `LocalServer` 側で事前検証し、不足時は処理を開始しない。
- [禁止] `POST /api/pairing/bundle` のような内部 helper 直公開 API を持たない。
- [厳守] `ProductionTool` 専用の eFuse 最終有効化機能は、本READMEの通常運用スコープへ含めない。

## 9. 変更履歴
- 2026-03-16: `ProductionTool` へ名称統一し、`LocalServer` とは別ソフト・独立動作であることを追記。理由: 通常運用 README でも名称統一と責務分離を明確化するため。
- 2026-03-16: ESP32 AP 側 `POST /api/pairing/transport-handshake` と、Rust 側 P-256 ECDH handshake を README へ反映。理由: Pairing workflow が secure transport の実ハンドシェイクまで進み、残課題が encrypted bundle 本体送達以降へ絞られたため。
- 2026-03-16: ESP32 AP 側 `POST /api/pairing/transport-session` placeholder と、Rust 側 secure transport negotiation を README へ反映。理由: Pairing workflow が summary staging の次に transport 交渉枠まで持てる段階へ進んだことを README でも追えるようにするため。
- 2026-03-16: ESP32 AP 側 `POST /api/pairing/bundle-summary` placeholder と、Rust 側 bundle summary staging を README へ反映。理由: Pairing workflow が session metadata 受理だけでなく、非秘密 summary を AP 側へ stage できる段階へ進んだことを README でも追えるようにするため。
- 2026-03-16: ESP32 AP 側 `POST /api/pairing/session` placeholder と、Rust 側 `verifying` 手前までの session metadata 登録を README へ反映。理由: Pairing workflow の現在地が `waiting_device` のみではなく `verifying` 手前まで進んだことを README でも追えるようにするため。
- 2026-03-16: ESP32 AP 側 `GET /api/pairing/state` placeholder と Rust からの参照を追加した現在地へ更新。理由: Pairing workflow の監視導線が一段進んだことを README でも共有するため。
- 2026-03-16: Pairing workflow が Rust 側で AP モード到達・認証 precheck まで実行する段階へ進んだことを追記。理由: README から見た現在地を「bundle 生成のみ」より一段進めて共有するため。
- 2026-03-16: `createPairingBundle` 相当の Rust 内部 helper 追加に合わせ、`POST /api/workflows/pairing/start` の現在地を更新。理由: Pairing workflow が「受理のみ」ではなく「内部 bundle 生成済み」へ進んだことを README でも共有するため。
- 2026-03-16: `SecretCore` の Pairing workflow 骨格追加に合わせ、`POST /api/workflows/pairing/start` の現状を「TS 窓口 + Rust placeholder workflow 実装済み」へ更新。理由: README 上も unknown-command 解消後の現在地を正確に共有するため。
- 2026-03-16: `POST /api/workflows/pairing/start` を [進捗] へ更新し、TS 側 REST 窓口と事前検証の実装済み、`SecretCore` 本体は継続中であることを追記。理由: code 上は route が追加された一方、README で [将来対応] のままだと実装現況と矛盾するため。
- 2026-03-16: workflow API 一覧を実装現況へ合わせ、`signed-ota/start` と `GET /api/workflows/{workflowId}` のみを実装済み、`pairing` / `key-rotation` / `production` を [将来対応] へ整理。理由: README だけを見ると未実装 API まで既に使えるように見える矛盾を解消するため。
- 2026-03-14: [003-0015] インストール/アンインストール（5節）を追加。コマンド仕様書 4.1 参照を記載。理由: 安全消去付き運用を README から辿れるようにするため。
- 2026-03-11: 管理者画面認証、鍵発行、AP一括設定、統合一覧、メンテナンス再起動、eFuse分離方針を追記。理由: LocalServer 管理画面要件を README で即参照可能にするため。
- 2026-03-09: workflow 公開 API（`/api/workflows/...`）と `createPairingBundle` 直公開禁止を追記。理由: 公開 API と内部 helper の境界を README でも明確化するため。
- 2026-03-09: `LocalServer` の責任範囲、`SecretCore` との境界、`ProductionTool` 非混在、`createPairingBundle` 前段検証責務を追記。理由: 通常運用サーバーとしての役割を現在設計へ合わせるため。
