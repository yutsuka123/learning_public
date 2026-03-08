# IoT IF仕様書

[重要] 本書はESP/Local/Cloudのインターフェース仕様を定義する。  
理由: 実装者ごとの差異を抑え、接続試験を容易にするため。

## 1. MQTT IF（Localブローカ: mosquitto想定 / TLS必須）

### 1.1 トピック命名規則
詳細は `MQTTコマンド仕様書.md` を参照のこと。
- [推奨] `cmd/esp32lab/{deviceId}/{commandName}` 等を採用する。
- 定義: `shared/include/common.h` 参照。
- [重要] LocalServer初期実装では以下を使用する。
  - status要求: `esp32lab/call/status/<deviceName|all>`
  - OTA要求: `esp32lab/call/otaStart/<deviceName|all>`
  - status通知受信: `esp32lab/notice/status/<senderName>`

### 1.2 セキュリティ必須条件
- [厳守] MQTTはTLS（通常 `8883`）で接続する。
- [厳守] MQTTログインID/パスワード認証を有効化する。
- [禁止] 平文MQTT（通常 `1883`）を運用利用しない。
- [禁止] 証明書検証を無効化しない。
- [厳守] payload内のWi-Fi設定はAES-256-GCMで暗号化する。
- [厳守] `publicId` を使用し、base_macをトピックへ直接使用しない。

### 1.3 メッセージ例
- 起動通知:
  - `{"deviceId":"esp32s3-001","status":"running","fwVersion":"x.y.z"}`
- LED指令:
  - `{"requestId":"req-001","ledState":"on"}`
 - 接続状態:
   - `{"deviceId":"esp32s3-001","phase":"mqttAuthenticating","result":"success"}`
- Wi-Fi設定更新（暗号化）:
  - `{"requestId":"req-010","publicId":"a1b2c3d4","ciphertext":"...","nonce":"...","tag":"...","version":1}`
- Wi-Fi更新確定:
  - `{"requestId":"req-010","confirm":true}`

### 1.4 QoS方針
- 起動通知: QoS1
- 指令系: QoS1
- 進捗通知: QoS0またはQoS1（運用選択）

## 2. HTTPS OTA IF（ESP側）
- [重要] OTA詳細仕様は `OTA仕様書.md` を参照する。
- [厳守] OTA開始は MQTT `otaStart` 指令で実施し、ESP32はHTTPSで `manifest` と `firmware.bin` を取得する。
- [厳守] ESP32は分割書込み時に進捗をLCD表示し、MQTTで `otaProgress` 通知する。
- [厳守] リトライは「同一進捗率停止: 5秒間隔3回」「最初から再試行: 3回」を上限とする。
- [厳守] サーバー提示SHA256とESP32計算SHA256を一致確認できた場合のみ更新確定する。

## 3. 認証情報の扱い
- [禁止] MQTT/HTTPSの認証実値（ユーザー、パスワード、トークン、秘密鍵）を本書へ記載しない。
- [推奨] 変数名・参照先のみ記載（例: `${MQTT_USERNAME}`）。
- [厳守] MQTTクライアント設定は `MQTT_HOST` `MQTT_PORT` `MQTT_USERNAME` `MQTT_PASSWORD` `MQTT_CA_CERT_PATH` 等の参照名で管理する。
- [厳守] `wrapped_secret` はユーザー環境でのみ保持し、デバイスへ配布しない。
- [厳守] `k-device` は `HKDF(ikm=k-user, salt=SHA256(base_mac), info="k-device-v1")` 方式で導出する。

## 4. Cloud IF [将来対応]
- AWS IoT Core または Google Cloud IoT相当サービスを候補とする。
- 認証フロー、デバイス証明書配布、更新承認フローは第4段階で確定する。

## 5. 変更履歴
- 2026-03-08: 鍵導出方式を `TPM + wrapped_secret + HKDF` 方式へ更新。理由: IF前提となる鍵生成式と保存物を正式仕様へ合わせるため。
- 2026-03-07: OTA IFを現行仕様へ更新し、`OTA仕様書.md` 参照と進捗/リトライ/SHA256検証要件を追記。
- 2026-03-07: LocalServer初期実装の topic（status/otaStart/status notice）を追記。
- 2026-02-24: 新規作成。MQTT/HTTPS IFの基本仕様を定義。
- 2026-02-24: 第3段階方針へ合わせ、MQTT TLS + ID/パスワード認証を必須化。第2段階OTA IFを現行対象外へ変更。
- 2026-02-24: Wi-Fi設定更新IF（public_idトピック、AES-GCM payload、confirm）を追加。
