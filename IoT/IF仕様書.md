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
- [厳守] `publicId` を使用し、Wi-Fiステーション名や通常 topic に base_mac を直接使用しない。

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

## 4. AP Pairing IF
### 4.1 AP 共通トップ画面 IF
- [厳守] AP 名は `esplab-ap-<shortId6>` とする。
- [厳守] AP 接続後は共通トップ画面パスワード認証を要求する。
- [厳守] 共通トップ画面から Production 画面へ進む場合は、メーカーモード専用パスワードの追加認証を要求する。
- [重要] 共通トップ画面の表示項目は少なくとも以下とする。
  - `shortId`
  - `publicId`
  - `firmwareVersion`
  - `productionReady`
  - `efuseApplied`
  - `secureBootEnabled`
  - `flashEncryptionEnabled`
  - `serialMode`
  - `currentKeyVersion`
  - `previousKeyState`
- [重要] 未ペアリング時の `publicId` 表示値は `pending` とする。

### 4.2 `publicId` 初期値
- [仕様変更] `publicId` の初期値は `IoT_<macアドレスからコロン除去>` とする。
- [重要] 初期値は公開識別子として扱い、必要に応じてユーザーが変更できる。

### 4.3 `runPairingSession()` 入力 IF
- 目的: AP モード接続中の個体へ `k-device` と関連設定を一括投入する。
- 呼出し元: `LocalServer`
- 実行主体: `SecretCore`
- [厳守] 現行の高リスク処理は `runPairingSession()` ワークフローを正規経路とし、`createPairingBundle` はその内部処理として扱う。
- 最低入力項目:
  - `targetDeviceId`
  - `sessionId`
  - `keyVersion`
  - `requestedSettings`
- [厳守] `requestedSettings` の必須項目は TS 側で事前検証し、不足時は `SecretCore` を呼び出さない。
- [厳守] 必須項目:
  - `wifi`
  - `mqtt`
  - `ota`
  - `credentials`
- [任意] `ntp`

### 4.4 `requestedSettings` 内部項目
- `wifi`
  - `ssid`
  - `password`
- `mqtt`
  - `host`
  - `port`
  - `tls`
  - `username`
  - `password`
  - `caCertRef`
- `ota`
  - `host`
  - `port`
  - `tls`
  - `username`
  - `password`
  - `caCertRef`
- `credentials`
  - `wifiUsername`（必要時）
  - `wifiPassword`
  - `mqttUsername`
  - `mqttPassword`
  - `otaUsername`
  - `otaPassword`
- `ntp`
  - `host`
  - `port`
  - `tls`

### 4.5 `createPairingBundle` 内部生成項目
- `publicId`
- `kDevice`
- `keyVersion`
- `requestedSettings`
- `bundleId`
- `sessionId`
- `targetDeviceId`
- `nonce`
- `signature`
- [厳守] `kDevice`、ECDH 共有秘密、`k-pairing-session` は TS へ平文返却しない。

### 4.5.1 `runPairingSession()` ワークフロー IF
- 目的: AP モード初回投入または再ペアリングを、`SecretCore` が対ESP32通信開始から完了判定まで責任を持って実行する。
- 呼出し元: `LocalServer`
- 実行主体: `SecretCore`
- 最低入力項目:
  - `targetDeviceId`
  - `sessionId`
  - `keyVersion`
  - `requestedSettings`
- 初期応答項目:
  - `workflowId`
  - `state`
  - `acceptedAt`
- [厳守] `state` の正規値は `queued` / `running` / `waiting_device` / `verifying` / `completed` / `failed` とする。
- [厳守] `LocalServer` は進捗状態と結果のみを受け取り、bundle の中間内容や逐次通信手順を保持しない。

### 4.6 暗号・検証方式
- [厳守] `SecretCore` と ESP32 は AP モード接続中に ECDH を行い、`k-pairing-session` を都度生成する。
- [厳守] ESP32 は固定公開鍵で bundle 署名を検証する。
- [厳守] bundle は `k-pairing-session` で暗号化する。
- [厳守] 再送防止・使い回し防止は `nonce`、`sessionId`、`bundleId` で行う。
- [禁止] 固定共有秘密鍵を全台共通で pairing 復号用に使わない。

### 4.7 ESP32 応答 IF
- 応答項目例:
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `result`
  - `detail`
  - `savedCurrentKeyVersion`
  - `previousKeyState`
- [厳守] NVS 保存成功時のみ `result=OK` とする。
- [厳守] 署名検証失敗、復号失敗、NVS 保存失敗時は `result=NG` とする。

### 4.7.1 workflow 状態 IF
- 応答項目例:
  - `workflowId`
  - `state`
  - `targetDeviceId`
  - `result`
  - `errorSummary`
  - `updatedAt`
- [厳守] `LocalServer` へ返す状態は進捗と結果のみとし、raw key、中間秘密、逐次署名素材を含めない。

### 4.8 current / previous key IF
- ESP32 は少なくとも以下を管理する。
  - `currentKeyVersion`
  - `currentKDevice`
  - `previousKeyVersion`
  - `previousKDevice`
  - `previousKeyState`
  - `graceActiveRuntimeMinutes`
  - `retainedRuntimeMinutes`
- [厳守] 稼働時間 48h 到達で `previousKeyState=expired-retained` とし、旧鍵認証を拒否する。
- [厳守] さらに稼働時間 240h 保持後に previous key を自動削除する。
- [厳守] previous key 自動削除結果は監査ログのみに残す。

## 5. Cloud IF [将来対応]
- AWS IoT Core または Google Cloud IoT相当サービスを候補とする。
- 認証フロー、デバイス証明書配布、更新承認フローは第4段階で確定する。

## 6. 変更履歴
- 2026-03-09: 4.3 見出しを `runPairingSession()` 中心の表現へ更新。理由: IF 文書の章タイトルも公開 workflow と内部 helper の関係に合わせるため。
- 2026-03-09: `runPairingSession()` ワークフロー IF と workflow 状態 IF を追加し、`createPairingBundle` を内部処理として整理。理由: 高リスク処理を `SecretCore` 主導で完了判定まで実行する構成へ IF を揃えるため。
- 2026-03-09: [仕様変更] `publicId` 初期値を `IoT_<macアドレスからコロン除去>` とする方針へ更新。理由: 運用上わかりやすさを優先し、初期導入時の識別を容易にするため。
- 2026-03-09: AP 共通トップ画面 IF、`createPairingBundle` の内部項目、ECDH + 固定公開鍵による pairing IF、current/previous key 運用 IF を追加。理由: AP モード投入と逐次再ペアリングの I/F を詳細化するため。
- 2026-03-08: 鍵導出方式を `TPM + wrapped_secret + HKDF` 方式へ更新。理由: IF前提となる鍵生成式と保存物を正式仕様へ合わせるため。
- 2026-03-07: OTA IFを現行仕様へ更新し、`OTA仕様書.md` 参照と進捗/リトライ/SHA256検証要件を追記。
- 2026-03-07: LocalServer初期実装の topic（status/otaStart/status notice）を追記。
- 2026-02-24: 新規作成。MQTT/HTTPS IFの基本仕様を定義。
- 2026-02-24: 第3段階方針へ合わせ、MQTT TLS + ID/パスワード認証を必須化。第2段階OTA IFを現行対象外へ変更。
- 2026-02-24: Wi-Fi設定更新IF（public_idトピック、AES-GCM payload、confirm）を追加。
