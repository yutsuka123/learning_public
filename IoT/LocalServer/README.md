# LocalServer (TypeScript + Node.js)

[重要] 本モジュールは、複数ESP32をWeb画面で管理し、MQTT command（status/OTA）を発行するローカル管理サーバーです。  
理由: ローカル常駐で監視・制御・OTA配布を一元化するため。

## 1. 提供機能
- [厳守] Web GUI（ブラウザ）でESP32一覧を表示（online/offline/unknown）。
- [厳守] `status` 取得要求を単体/選択/全体へ送信。
- [厳守] `otaStart` 要求を単体/選択/全体へ送信。
- [重要] 起動時に `status` call を自動送信（初期同期）。
- [重要] OTA配布用エンドポイントを提供（`/ota/manifest.json`, `/ota/firmware.bin`）。

## 2. セットアップ
1. `cp .env.example .env`（Windowsは手動コピー）。
2. `.env` の値を環境に合わせて編集。
3. 依存導入: `npm install`
4. 開発起動: `npm run dev`
5. 本番ビルド: `npm run build`
6. 本番起動: `npm run start`

## 3. 疎通確認
- MQTT接続試験: `npm run test:connect`
- APIヘルス確認: `GET http://localhost:3100/api/health`
- GUI: `http://localhost:3100/`

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
