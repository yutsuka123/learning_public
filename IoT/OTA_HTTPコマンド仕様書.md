# IoT OTA/画像更新 HTTPコマンド仕様書

[重要] 本書は OTA および画像更新で使用する HTTP/HTTPS API の詳細仕様を定義する。  
理由: MQTT は「指令」、HTTP は「実体転送」で責務が異なるため、payload と認証要件を分離して明確化するため。

[厳守] 本番運用の実体転送は HTTPS（TLS 検証有効）を前提とする。  
理由: 配布物改ざん、盗聴、なりすまし配布を防ぐため。

[禁止] 認証なしで OTA 実行API、画像更新API、設定更新APIへ到達できる状態を残さない。  
理由: AP 到達だけで高リスク操作が可能になるため。

## 1. 適用範囲
- 対象1: LocalServer 配下の OTA 配布 API（ESP32 が取得）
- 対象2: AP メンテナンスモード Web API（ブラウザ/Production/LocalServer が利用）
- 対象3: 画像更新（単体/一括 ZIP）

## 2. 共通仕様
### 2.1 共通ヘッダ
- `Content-Type: application/json`（JSON API）
- `Authorization: Bearer <token>` または Cookie セッション
- [厳守] APモード高リスクAPIは認証済みセッション必須

### 2.2 共通レスポンス
```json
{
  "result": "OK",
  "errorCode": null,
  "detail": "accepted",
  "requestId": "req-20260311-0001",
  "ts": "2026-03-11T10:30:00.000Z"
}
```

- `result`: `OK` / `NG`
- `errorCode`: 失敗時のみ設定
- `detail`: 人間可読の補足
- `requestId`: 追跡用ID

### 2.3 共通エラーコード
- `AUTH_REQUIRED`: 認証不足
- `AUTH_EXPIRED`: セッション期限切れ
- `INVALID_ARGUMENT`: パラメータ不正
- `FORBIDDEN_SCOPE`: 許可外パス/対象
- `SHA_MISMATCH`: SHA-256 不一致
- `SIGNATURE_INVALID`: 署名/HMAC検証失敗
- `IO_ERROR`: ファイルI/O失敗
- `BUSY_RETRY_LATER`: 実行中で受付不可

## 3. LocalServer OTA配布 API（ESP32取得側）
### 3.1 `GET /ota/manifest.json`
- 目的: OTA メタ情報を配布する
- 認証: [推奨] 閉域配布 + 短寿命トークン
- 応答例:
```json
{
  "firmwareVersion": "1.2.3",
  "firmwareUrl": "https://localserver:4443/ota/firmware.bin",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "minBatteryPercent": 20
}
```

### 3.2 `GET /ota/firmware.bin`
- 目的: OTA 対象ファームウェアを配布する
- 認証: `manifest` と同一方針
- [厳守] `Content-Length` を返し、ESP32 側の進捗算出に利用可能にする

### 3.3 `GET /assets/images-pack.zip`
- 目的: `imagePackageApply` 用 ZIP を配布する
- [厳守] HTTPS + TLS 検証必須
- [厳守] サーバー側でも `SHA-256` を保持し、要求時に照合可能にする

## 4. APメンテナンス API（ESP32側Web）
### 4.1 認証
#### 4.1.1 `POST /api/auth/ap-top`
- 目的: AP共通トップ画面ログイン
- 要求:
```json
{
  "username": "maintenance",
  "password": "<role-password>"
}
```

#### 4.1.2 `POST /api/auth/production`
- 目的: Production 追加認証
- 要求:
```json
{
  "username": "mfg",
  "password": "<manufacturer-mode-password>"
}
```

- [厳守] AP共通パスワードと Production パスワードを分離する。
- [厳守] AP認証ロール（`user`/`maintenance`/`admin`/`mfg`）を実装し、権限に応じて画面/API到達可否を制御する。
- [重要] ロール別の画面項目・権限は `APメンテナンス画面仕様書.md` を正とする。

### 4.2 設定取得/更新
#### 4.2.1 `GET /api/settings/network`
- 目的: 現在のネットワーク設定取得（機密値はマスク）
- [厳守] 未認証は `AUTH_REQUIRED`

#### 4.2.2 `PUT /api/settings/network`
- 目的: Wi-Fi/MQTT/HTTPS/NTP 設定更新
- 要求例:
```json
{
  "wifi": {
    "ssid": "office-ssid",
    "password": "********"
  },
  "mqtt": {
    "host": "mqtt.esplab.home.arpa",
    "port": 8883,
    "tls": true,
    "username": "iot-user",
    "password": "********"
  },
  "ota": {
    "host": "ota.esplab.home.arpa",
    "port": 443,
    "tls": true
  },
  "timeServer": {
    "host": "ntp.esplab.home.arpa",
    "port": 123,
    "tls": false
  },
  "apply": false,
  "reboot": true
}
```

- [厳守] 保存先は `NVS` を正とする
- [厳守] `apply=false` + `reboot=true` を既定推奨

### 4.3 OTA/画像更新（AP経由）
#### 4.3.1 `POST /api/maintenance/ota/start`
- 目的: AP経由で OTA 実行要求
- 要求例:
```json
{
  "manifestUrl": "https://localserver:4443/ota/manifest.json",
  "firmwareUrl": "https://localserver:4443/ota/firmware.bin",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "signature": "<base64>"
}
```

- [厳守] 署名/HMAC 検証成功時のみ実行
- [厳守] HTTPS/TLS 検証失敗時は即時 `NG`

#### 4.3.2 `POST /api/maintenance/image/upload`
- 目的: 単体画像アップロード
- 形式: `multipart/form-data`
- 項目:
  - `targetPath`（`/images` 配下限定）
  - `file`（画像）
  - `sha256`（16進64桁）
- [厳守] `tmp` 書込み後、SHA一致で `rename`

#### 4.3.3 `POST /api/maintenance/image-package/apply`
- 目的: ZIP一括画像更新
- 要求例:
```json
{
  "packageUrl": "https://localserver:4443/assets/images-pack.zip",
  "packageSha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "destinationDir": "/images/top",
  "overwrite": true,
  "signature": "<base64>"
}
```

- [厳守] `destinationDir` は `/images` 配下限定
- [禁止] `/logs` への書込み/展開

### 4.4 再起動
#### 4.4.1 `POST /api/device/reboot`
- 目的: 処理完了後に再起動を要求
- [厳守] 成否判定は再起動後 `status` topic で実施する（AP切断だけでは確定しない）

## 5. 複数台一括メンテナンス時のHTTP運用
- [重要] LocalServer / Production は USB Wi-Fi で `AP-esp32lab-*` を探索し、1台ずつ処理する
- 推奨シーケンス:
  1. AP探索
  2. AP接続
  3. `POST /api/auth/ap-top`
  4. 設定更新または更新API実行
  5. `POST /api/device/reboot`
  6. 通常ネットワーク復帰後の `status` 受信で成功判定
- [厳守] 途中失敗個体はスキップせず、`failed` としてキュー管理する

## 6. 文書分担
- `MQTTコマンド仕様書.md`: 指令・通知・進捗のメッセージ仕様
- `OTA仕様書.md`: OTA方式、再試行、A/B切替、安全要件
- `OTA_HTTPコマンド仕様書.md`（本書）: HTTP/HTTPS API、payload、認証、実体転送仕様

## 7. 変更履歴
- 2026-03-11: AP認証APIを `username + password` 形式へ更新し、ロールベース権限制御と `APメンテナンス画面仕様書.md` 参照を追加。理由: 閲覧/保守/管理/製造の権限分離をHTTP API仕様へ反映するため。
- 2026-03-11: 新規作成。理由: OTAと画像更新の HTTP API（配布/認証/更新/再起動）を MQTT 仕様から分離し、実装時の責務境界と payload 定義を明確化するため。
