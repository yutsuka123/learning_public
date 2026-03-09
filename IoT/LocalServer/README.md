# LocalServer (TypeScript + Node.js)

[重要] 本モジュールは、複数ESP32をWeb画面で管理し、MQTT command（status/OTA）を発行するローカル管理サーバーです。  
理由: ローカル常駐で監視・制御・OTA配布を一元化するため。

## 0. 責任範囲
- [厳守] `LocalServer` は通常運用 UI、REST API、MQTT 制御、OTA 実行管理、AP セッション管理、監査表示を担当する。
- [厳守] 秘密処理は `SecretCore` が担当し、`LocalServer` は raw `k-user`、raw `k-device`、ECDH 共有秘密、`k-pairing-session` を保持しない。
- [厳守] `Production` は製造、初回セキュア化、eFuse 実行主体であり、通常運用の `LocalServer` と混在させない。
- [厳守] 詳細な責任境界は `IoT/モジュール仕様書.md` を親定義とする。

## 1. 提供機能
- [厳守] Web GUI（ブラウザ）でESP32一覧を表示（online/offline/unknown）。
- [厳守] `status` 取得要求を単体/選択/全体へ送信。
- [厳守] `otaStart` 要求を単体/選択/全体へ送信。
- [重要] 起動時に `status` call を自動送信（初期同期）。
- [重要] OTA配布用エンドポイントを提供（`/ota/manifest.json`, `/ota/firmware.bin`）。
- [重要] AP 共通トップ画面の入力検証、`runPairingSession()` 要求前の前段チェック、進行状態表示を担当する。

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

## 5. 自動起動（Task Scheduler）
- PowerShell（管理者）で `scripts/install-task-scheduler.ps1` を実行。
- 実行後、PC起動時に `npm run start` が自動起動する。

## 6. API一覧（最小）
- `GET /api/health`
- `GET /api/devices`
- `POST /api/commands/status` body: `{ "targetNames": "all" }` または `{ "targetNames": ["IoT_xxx"] }`
- `POST /api/commands/ota` body: `{ "targetNames": "all" }` または `{ "targetNames": ["IoT_xxx"] }`
- `POST /api/workflows/pairing/start`
- `POST /api/workflows/key-rotation/start`
- `POST /api/workflows/signed-ota/start`
- `GET /api/workflows/{workflowId}`

## 7. セキュリティ境界
- [厳守] `LocalServer` は `SecretCore` の用途固定 API を呼び出すのみとし、汎用署名APIや raw key 取得APIを持たない。
- [厳守] `runPairingSession()` 実行時は Wi-Fi / MQTT / OTA / 認証情報の必須項目を `LocalServer` 側で事前検証し、不足時は処理を開始しない。
- [禁止] `POST /api/pairing/bundle` のような内部 helper 直公開 API を持たない。
- [厳守] `Production` 専用の eFuse 最終有効化機能は、本READMEの通常運用スコープへ含めない。

## 8. 変更履歴
- 2026-03-09: workflow 公開 API（`/api/workflows/...`）と `createPairingBundle` 直公開禁止を追記。理由: 公開 API と内部 helper の境界を README でも明確化するため。
- 2026-03-09: `LocalServer` の責任範囲、`SecretCore` との境界、`Production` 非混在、`createPairingBundle` 前段検証責務を追記。理由: 通常運用サーバーとしての役割を現在設計へ合わせるため。
