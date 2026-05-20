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
  - rollback試験モード切替: `esp32lab/call/rollbackTestEnable|rollbackTestDisable/<deviceName>`
  - status通知受信: `esp32lab/notice/status/<senderName>`
- [重要] 将来の多重構成では `serverId` / `brokerId` / `wifiApId` をメタ情報として管理し、`Server` と `MQTT Broker` を別責務で扱う。

### 1.2 セキュリティ必須条件
- [厳守] MQTTはTLS（通常 `8883`）で接続する。
- [厳守] MQTTログインID/パスワード認証を有効化する。
- [禁止] 平文MQTT（通常 `1883`）を運用利用しない。
- [禁止] 証明書検証を無効化しない。
- [厳守] MQTT payload本文は `k-device` による AES-256-GCM で全文暗号化する（トピックは平文）。
- [厳守] 暗号化エンベロープは `security.mode=k-device-a256gcm-v1` / `enc.alg=A256GCM` / `enc.iv` / `enc.ct` / `enc.tag` を必須とする。
- [厳守] `enc` 欠損、アルゴリズム不一致、認証タグ不一致時は復号失敗として処理拒否する。
- [推奨] 本番は暗号化必須（strict）運用とし、移行期間のみ compat、障害切り分け時のみ plain を短時間許可する。
- [新規利用禁止] 恒久的な plain 運用。理由: MQTTモニタから機密情報が読み取れるため。
- [厳守] `publicId` を使用し、Wi-Fiステーション名や通常 topic に base_mac を直接使用しない。

### 1.3 メッセージ例
- 起動通知:
  - `{"deviceId":"esp32s3-001","status":"running","fwVersion":"x.y.z"}`
  - [重要] 7015/7025試験中は一時的に `runningPartition` / `bootPartition` / `nextUpdatePartition` を付与し、試験完了後に削除する。
- LED指令:
  - `{"requestId":"req-001","ledState":"on"}`
 - 接続状態:
   - `{"deviceId":"esp32s3-001","phase":"mqttAuthenticating","result":"success"}`
- Wi-Fi設定更新（暗号化）:
  - `{"requestId":"req-010","publicId":"a1b2c3d4","ciphertext":"...","nonce":"...","tag":"...","version":1}`
- Wi-Fi更新確定:
  - `{"requestId":"req-010","confirm":true}`
- payload全文暗号化エンベロープ:
  - `{"v":"1.0.0","security":{"mode":"k-device-a256gcm-v1"},"enc":{"alg":"A256GCM","iv":"...","ct":"...","tag":"..."}}`

### 1.4 QoS方針
- 起動通知: QoS1
- 指令系: QoS1
- 進捗通知: QoS0またはQoS1（運用選択）

## 2. HTTPS OTA IF（ESP側）
- [重要] OTA方式/安全要件は `OTA仕様書.md`、HTTP/HTTPS API詳細は `OTA_HTTPコマンド仕様書.md` を参照する。
- [厳守] OTA開始は MQTT `otaStart` 指令で実施し、ESP32はHTTPSで `manifest` と `firmware.bin` を取得する。
- [厳守][2026-05-07変更] `otaStart` 指令の復号後payloadには `sigAlg=HMAC-SHA256` と `signature` を含め、ESP32 は `k-device` による HMAC 検証失敗時に OTA を開始しない。理由: MQTT command 本文の改ざんやなりすまし開始を拒否するため。
- [厳守][2026-05-10変更] `set/keyDeviceSet` と `set/fileLogSet` の復号後payloadにも `sigAlg=HMAC-SHA256` と `signature` を含め、ESP32 は `k-device` による HMAC 検証失敗時に設定変更を適用しない。理由: `k-device` 更新やログ設定変更の改ざん投入を拒否するため。
- [厳守] ESP32は分割書込み時に進捗をLCD表示し、MQTTで `otaProgress` 通知する。
- [厳守] リトライは「同一進捗率停止: 5秒間隔3回」「最初から再試行: 3回」を上限とする。
- [厳守] サーバー提示SHA256とESP32計算SHA256を一致確認できた場合のみ更新確定する。

## 3. 認証情報の扱い
- [禁止] MQTT/HTTPSの認証実値（ユーザー、パスワード、トークン、秘密鍵）を本書へ記載しない。
- [推奨] 変数名・参照先のみ記載（例: `${MQTT_USERNAME}`）。
- [厳守] MQTTクライアント設定は `MQTT_HOST` `MQTT_PORT` `MQTT_USERNAME` `MQTT_PASSWORD` `MQTT_CA_CERT_PATH` 等の参照名で管理する。
- [厳守] `wrapped_secret` はユーザー環境でのみ保持し、デバイスへ配布しない。
- [厳守] `k-device` は `HMAC-SHA256(key=k-user, message=target_device_name)`（`target_device_name` = `publicId`、初期値は `IoT_<base MAC からコロン除去>`）で導出する。実装: `SecretCore/src/key_manager.rs:454-465 get_k_device`。[補足][2026-05-19] 旧 HKDF 式は撤回。理由は `鍵管理および初期セットアップ設計仕様書.md` §6.2 / §16 参照。

## 4. AP Pairing IF
### 4.1 AP 共通トップ画面 IF
- [厳守] AP 名は `AP-esp32lab-<MAC(no colon)>` とする。
- [厳守] AP 接続後は共通トップ画面パスワード認証を要求する。
- [厳守] 共通トップ画面から `ProductionTool` 画面へ進む場合は、メーカーモード専用パスワードの追加認証を要求する。
- [厳守] 未認証状態では設定値参照、設定更新、FW更新、画像更新APIを拒否する。
- [推奨] AP Web UI の初期パスワード（各ロール）は、初回ログイン時または運用開始前に変更する。
- [推奨] 例外的に固定値継続する場合でも、理由・対象・予定期間を監査ログへ記録する。
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
- [重要] APモードのロール定義、ユーザー別権限、画面項目詳細は `APメンテナンス画面仕様書.md` を参照する。

### 4.1.1 AP メンテナンス一括運用 IF（PC側）
- [重要] `LocalServer` / `ProductionTool` は、指定USB Wi-Fiインタフェースで `AP-esp32lab-<MAC(no colon)>` を探索する。
- [重要] 推奨フロー:
  1. AP探索
  2. 対象APへ接続
  3. `POST /api/auth/ap-top` でログイン
  4. 設定更新または更新処理実行
  5. `POST /api/device/reboot` 実行
  6. 再起動後の `status` topic で成功判定
- [厳守] AP切断直後の見かけ上成功ではなく、再起動後 `status` 応答で最終判定する。
- [推奨] 成功判定項目は `publicId`、`fwVersion`、`configVersion`、`updatedAt`、`errorSummary` を含める。

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
  - `hostName`（`mqttUrlName` 相当）
  - `port`
  - `tls`
  - `username`
  - `password`
  - `caCertRef`
- `ota`
  - `host`
  - `hostName`（`otaUrlName` 相当）
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
  - `keyDevice`（Base64）
- `server`
  - `host`
  - `hostName`（`serverUrlName` 相当）
  - `port`
  - `tls`
- `ntp`
  - `host`
  - `hostName`（`timeServerUrlName` 相当）
  - `port`
  - `tls`
- [重要] `mqttUrlName` / `serverUrlName` / `otaUrlName` / `timeServerUrlName` / `keyDevice` は ESP32 / LocalServer の保存読書きとAPI入出力へ反映済み。

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

### 4.7.0 AP Pairing session metadata IF
- 目的: `runPairingSession()` の placeholder 段階で、AP 側が `sessionId` / `bundleId` / `targetDeviceId` / `keyVersion` の受理状態を保持する。
- HTTP:
  - `POST /api/pairing/session`
- 要求項目:
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `keyVersion`
- 応答項目例:
  - `result`
  - `state`
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `detail`
- [厳守] 現段階の placeholder IF では bundle 本文、raw `kDevice`、ECDH 共有秘密、`k-pairing-session` を送らない。
- [厳守] 本 IF は AP 側状態の受理枠であり、NVS 保存成功を意味しない。
- [将来対応] ECDH / 署名検証 / 復号 / NVS 保存本体を実装したら、metadata 受理から secure bundle 適用完了 IF へ拡張する。

### 4.7.0.1 AP Pairing bundle summary IF
- 目的: `runPairingSession()` の placeholder 段階で、AP 側が bundle 本体ではなく非秘密 summary の受理状態を保持する。
- HTTP:
  - `POST /api/pairing/bundle-summary`
- 要求項目:
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `publicId`
  - `keyVersion`
  - `nonce`
  - `signature`
  - `requestedSettingsSha256`
- 応答項目例:
  - `result`
  - `state`
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `publicId`
  - `keyVersion`
  - `requestedSettingsSha256`
  - `detail`
- [厳守] 現段階の placeholder IF では raw `kDevice`、Wi-Fi/MQTT/OTA 認証情報、bundle 平文 JSON、ECDH 共有秘密を送らない。
- [厳守] `requestedSettingsSha256` は平文設定送達の代替ではなく、placeholder 段階の整合確認用 summary として扱う。
- [厳守] 本 IF は AP 側状態の受理枠であり、署名検証成功や NVS 保存成功を意味しない。
- [将来対応] ECDH / secure bundle transport / NVS 保存本体を実装したら、summary 受理から暗号化済み bundle 適用完了 IF へ拡張する。

### 4.7.0.2 AP Pairing transport session IF
- 目的: `runPairingSession()` の placeholder 段階で、AP 側が secure transport 本体に入る前の交渉状態を保持する。
- HTTP:
  - `POST /api/pairing/transport-session`
- 要求項目:
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `requestedKeyAgreement`
  - `requestedBundleProtection`
- 応答項目例:
  - `result`
  - `state`
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `acceptedKeyAgreement`
  - `acceptedBundleProtection`
  - `detail`
- [厳守] 現段階の placeholder IF では ECDH 公開鍵、ECDH 共有秘密、暗号化済み bundle 本体を送らない。
- [厳守] `requestedKeyAgreement` / `requestedBundleProtection` は実暗号処理成功ではなく、secure transport 本体を差し込む前の交渉 placeholder として扱う。
- [厳守] 本 IF は AP 側状態の受理枠であり、復号成功や NVS 保存成功を意味しない。
- [将来対応] ECDH / secure bundle transport / NVS 保存本体を実装したら、交渉 placeholder から実ハンドシェイクへ拡張する。

### 4.7.0.3 AP Pairing transport handshake IF
- 目的: `runPairingSession()` が AP 側と P-256 ECDH handshake を行い、encrypted bundle 送達前の transport session key を双方で導出する。
- HTTP:
  - `POST /api/pairing/transport-handshake`
- 要求項目:
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `clientPublicKeyBase64`
- 応答項目例:
  - `result`
  - `state`
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `acceptedKeyAgreement`
  - `acceptedBundleProtection`
  - `serverPublicKeyBase64`
  - `sharedSecretFingerprint`
  - `detail`
- [厳守] P-256 の ECDH 共有秘密そのものは REST 応答へ含めず、双方のプロセス内メモリだけに保持する。
- [厳守] `sharedSecretFingerprint` は transport session key の照合用摘要であり、raw secret ではない。
- [厳守] 本 IF は transport session key 導出までを担当し、encrypted bundle の復号成功や NVS 保存成功を意味しない。
- [将来対応] 後続の encrypted bundle 受理 IF で、本 handshake により導出した transport session key を利用する。

### 4.7.0.4 AP Pairing secure bundle apply IF
- 目的: `runPairingSession()` が導出済み transport session key を使って encrypted bundle 本体を送達し、ESP32 側で復号・NVS 保存を完了させる。
- HTTP:
  - `POST /api/pairing/secure-bundle`
- 要求項目:
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `keyVersion`
  - `ivBase64`（12byte nonce）
  - `cipherBase64`（AES-256-GCM 暗号文）
  - `tagBase64`（16byte GCM tag）
  - `payloadSha256`（復号平文の SHA-256）
- 応答項目例:
  - `result`
  - `state`（`applied`）
  - `targetDeviceId`
  - `sessionId`
  - `bundleId`
  - `savedCurrentKeyVersion`
  - `previousKeyState`
  - `detail`
- [厳守] 復号 AAD は `pairing-secure-bundle-v1|targetDeviceId|sessionId|bundleId|keyVersion` で固定し、IF 改変時は送受双方を同時改修する。
- [厳守] `state=transport_established` かつ transport session key 保持時のみ本 IF を受理する。
- [厳守] 復号平文および transport session key の raw 値をログ・REST 応答に出力しない。
- [禁止] payload identity（`targetDeviceId/sessionId/bundleId/keyVersion`）不一致時に保存処理を継続しない。

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

### 4.9 `LittleFS` 資産更新 IF
- 目的: `/images` と `/certs` を全面書換せず局所更新する。
- 第一経路: `MQTT`
- 代替経路: AP モード HTTP
- [厳守] 更新対象領域は `targetArea=images|certs` を使用する。
- [厳守] `/logs` は更新対象に含めない。
- [厳守] 画像更新系コマンド（`fileSyncPlan` / `fileSyncChunk` / `fileSyncCommit`）は、OTAと同等に HMAC/署名検証を必須とする。
- manifest 項目例:
  - `sessionId`
  - `targetArea`
  - `deleteMode`
  - `files[]`
- `files[]` の項目例:
  - `path`
  - `size`
  - `sha256`
  - `action`
- [厳守] `action` は少なくとも `upsert` / `delete` を許容し、`deleteMode=all` により全削除も指示できる。
- [厳守] ESP32 は manifest 受信後に既存ファイルとの差分比較を行い、必要ファイルのみ転送要求する。
- [厳守] 書込みは `tmp` ファイルへ行い、`SHA-256` 一致時のみ `rename` で反映する。
- [厳守] 失敗時は旧ファイルを維持する。
- [将来対応] `imagePackageApply`（ZIP一括更新）を追加し、MQTTトリガで HTTPS からZIPを取得し `destinationDir` 配下へ展開する方式を許容する。
- [厳守] `imagePackageApply` でも MQTT指令の HMAC/署名検証、HTTPS/TLS 検証、ZIP全体 `SHA-256` 検証を必須とする。
- [厳守] `destinationDir` は `/images` 配下に限定し、同名ファイルは `overwrite=true` 指定時のみ上書きする。

### 4.10 証明書 IF
- [厳守] MQTT / HTTPS 用証明書実体は `LittleFS` `/certs` に保存する。
- [厳守] `NVS` には少なくとも以下を保持する。
  - `activeCertSet`
  - `certSha256`
  - `certVersion`
  - `certUpdatedAt`
- [推奨] 証明書更新後は再接続前に `certSha256` と実ファイル整合を確認する。

### 4.11 ファイルログ IF
- [厳守] ファイルログはシリアル出力と別関数で扱う。
- [厳守] 時刻同期済み時の保存形式は `/logs/<年>/<月日>/<年月日時分秒ms-連番>.log` とする。
- [厳守] 時刻同期できない場合は `/logs/<起動回数>/00001.log` 形式とする。
- [厳守] 再起動時または 1KB 到達時に新しいファイルへ切り替える。
- [厳守] 30 日経過または総量 100KB 超過時は古いログから削除する。

### 4.12 多重接続 / 冗長化 IF
- [厳守] `Server` は `MQTT Broker` と別主体として扱い、`serverId` と `brokerId` を混同しない。
- [厳守] `ESP32` は将来の複数 `Server` 同時接続を考慮し、`serverId` 単位で少なくとも以下を区別できる保存構造を前提とする。
  - `brokerId`
  - `wifiApId`
  - MQTT 接続設定
  - 証明書メタ情報
  - `k-device`
- [厳守] システム全体では複数 `WiFi AP` を許容するが、各 `ESP32` の現行運用は固定1件とする。
- [重要] テレメトリおよび `status` は同一 `Broker` を subscribe する複数 `Server` で共有してよい。
- [厳守] OTA は要求元 `Server` の情報に対してのみ応答する。
- [厳守] 同一種別の要求が複数 `Server` から到着した場合は、早いもの順に処理する。
- [重要] `k-user` 保護方式は `OS暗号化サービス由来方式`（現行: Windows DPAPI、ソフトウェア暗号化・チップ非依存）または `暗号化ファイル + パスワード方式` を許容し、復号処理は `SecretCore` の Rust モジュール内で完結する。Mac/Linux 汎用化は `020-0004`。
- [厳守] `k-device` は `Server` ごとに分離する運用と、複数 `Server` 間で共通利用する運用をユーザー選択で切り替える。

### 4.13 LocalServer 設定復旧 IF
- 目的: LocalServer の設定画面から `device_db` スナップショットの退避・復元を行う。
- HTTP:
  - `POST /api/settings/backups/device-db/export`
  - `POST /api/settings/backups/device-db/restore`
- 要求項目:
  - `backupRoot`（退避時、任意）
  - `backupDir`（復元時、任意）
- 応答項目例:
  - `result`
  - `backupDir`
  - `manifestPath`
  - `memoPath`
  - `fileCount`
  - `restoredFileNames`
  - `restoredFileCount`
  - `detail`
- [重要] 退避/復元対象は `settings.json` / `securityState.json` / `keyStore.json` / `wrapped_secret.bin` / `wrapped_k_user.bin` を最小集合とする。
- [厳守] 復元元未指定時は `data/secure-backups` 配下の最新スナップショットを使う。
- [厳守] 退避先は既定で `data/secure-backups` 配下へ保存し、設定画面上で変更先を明示できるようにする。
- [禁止] `device_db` に raw `k-user` を含める前提の保存形式へ戻さない。

### 4.14 `k-user` 暗号化バックアップ IF
- 目的: LocalServer の設定画面から `SecretCore` を経由して `k-user` の暗号化バックアップを出力・復元する。
- HTTP:
  - `POST /api/settings/backups/k-user/export`
  - `POST /api/settings/backups/k-user/import`
- 要求項目:
  - `backupPassword`
  - `backupFilePath`（任意、export 時）
- 応答項目例:
  - `result`
  - `exported`
  - `imported`
  - `backupFilePath`
  - `keyFingerprint`
  - `source`
  - `format`
  - `detail`
- [厳守] raw `k-user` は REST 応答、UI、ログへ含めない。
- [厳守] バックアップファイルは `scrypt + AES-256-GCM` の暗号化形式を使う。
- [重要] export 時の既定出力先は `data/secure-backups` 配下のタイムスタンプ付きファイルとし、import 時は明示ファイルパスを必須とする。
- [禁止] `k-user` の平文バックアップファイルを新規作成しない。

## 5. クラウド連携（第4段階）責務分界表 [012-0004][2026-05-20 初版]

[重要] 本章はローカル鍵階層とクラウド側デバイス認証の **責務分界・禁止事項** を初版化する。`012-0005` / `009-0004`（LocalServer クラウド連携 I/F 設計）の前提とする。  
[参照] クラウド連携ロードマップは `todo.md` `### 012. クラウド連携・第4段階` を参照。AWS 認証情報は `IoT/LocalServer/.env`（Git 除外）に保存済み。

### 5.1 責務分界の概念図

```text
[ローカル環境（Windows PC）]                   [クラウド環境（AWS ap-northeast-1）]
┌──────────────────────────────────────┐      ┌──────────────────────────────────────┐
│ SecretCore（Rust）                    │      │ AWS IoT Core                         │
│  wrapped_secret                      │      │                                      │
│    ↓ Windows DPAPI 復号              │      │  ・X.509 クライアント証明書認証       │
│  S_random                            │      │  ・IoT Policy（topic 単位の認可）     │
│    ↓ HKDF + S_app                    │      │  ・TLS 8883                          │
│  k-user                              │      │  ・MQTT topic routing                │
│    ↓ HMAC-SHA256(publicId)           │      │  ・デバイス証明書管理               │
│  k-device  ← [平文禁出境界] ─────── │×     │  （k-device は到達しない）           │
│                                      │      │                                      │
│ LocalServer（TypeScript）            │      │                                      │
│  ・Mosquitto MQTT ブローカ（ローカル）│←TLS→│  ・AWS IoT MQTT ブローカ（クラウド） │
│  ・MQTT payload AES-256-GCM 暗号化   │      │  ・デバイス証明書認証               │
│  ・SecretCore IPC 経由で鍵管理       │      │  ・IoT Policy 評価                  │
│  ・ProductionTool（不可逆 eFuse 等） │      │                                      │
└──────────────────────────────────────┘      └──────────────────────────────────────┘
         ↑ TLS 8883（ID/Password + CA）               ↑ TLS 8883（X.509 証明書）
      ESP32 MQTT（現行ローカル）              ESP32 MQTT（将来クラウド直接 or 経由）
```

### 5.2 責務分界表

| 責務カテゴリ | ローカル側（LocalServer / SecretCore）| クラウド側（AWS IoT Core）| 方針 |
|---|---|---|---|
| **MQTT ブローカ** | Mosquitto（`172.17.1.100:8883`）← ローカルモード | AWS IoT エンドポイント（TLS 8883）← クラウドモード | ESP32 が NVS `brokerMode` で直接切替。LocalServer は両方を subscribe |
| **デバイス認証（MQTT）** | ID/Password（`allow_anonymous false`）| X.509 クライアント証明書 | クラウド側は X.509 が必須（ID/Password より強い）|
| **MQTT topic 認可** | Mosquitto `acl_file`（device / role 単位）| IoT Policy（ARN / topic 単位）| 構造は同一（`esp32lab/<kind>/<sub>/<name>`）を維持 |
| **MQTT payload 暗号化** | AES-256-GCM（`k-device`、現行実装）| AES-256-GCM（`k-device`、変更なし）| クラウドに転送されても payload は暗号化済みのまま |
| **k-device 管理** | SecretCore が LocalServer ローカルで導出・保持 | **不関与**（k-device をクラウドへ送出しない）| ローカル境界外に raw k-device を出さない原則を維持 |
| **S_random / k-user 管理** | Windows DPAPI で保護（SecretCore 内部のみ）| **不関与** | DPAPI 保護は OS ユーザー資格情報拘束のため転送不可 |
| **デバイス証明書管理** | （クラウド連携前は不関与）| AWS IoT Core が device cert + Root CA を管理 | cert は `LittleFS /certs` に保存、AWS コンソール or CLI で発行 |
| **OTA 配布** | LocalServer HTTPS（`:4443`）が現行の主経路 | （将来拡張：AWS IoT Jobs / S3 等）| 現行は変更なし |
| **不可逆処理（eFuse / Secure Boot）** | ProductionTool（ローカル工場ツール）のみ | **永久禁止（クラウドトリガ不可）** | 不可逆工程はネットワーク非依存の物理工程として維持 |

### 5.3 禁止事項（クラウド境界を越えてはならない情報）

| 禁止内容 | 理由 |
|---|---|
| [禁止] raw `k-device` をクラウドへ平文送信しない | クラウド側の漏えいがローカル全デバイス通信の解読につながるため |
| [禁止] `wrapped_secret` / `S_random` をクラウドへ送信しない | DPAPI 保護が無効化され、k-user / k-device 一括解読が可能になるため |
| [禁止] `k-user` をクラウドへ送信しない | k-user が漏えいすると全デバイスの k-device を再導出できるため |
| [禁止] `k-iot-secure-boot` / `k-iot-flash-encryption` 秘密鍵をクラウドへ送信しない | eFuse 不可逆処理に直結し、復旧不能な損害につながるため |
| [禁止] eFuse / Secure Boot / Flash Encryption 書込みをクラウドトリガで実行しない | 不可逆工程は ProductionTool + ローカル物理工程として分離する設計のため |
| [禁止] AWS 認証情報（Access Key / Secret Key）を TS/Rust ソースに平文埋込しない | `IoT/LocalServer/.env`（Git 除外）のみに保持する。誤コミット防止 |

### 5.4 AWS IoT Core 側のデバイス認証方式（X.509）[2026-05-20 Option B 確定]

- **採用アーキテクチャ（ESP32 直接接続方式 Option B）** [2026-05-20 確定]:
  - クラウドモード: ESP32 → AWS IoT Core（X.509 mutual TLS） ← LocalServer（subscriber）
  - ローカルモード: ESP32 → Mosquitto（TLS + ID/Password） ← LocalServer（subscriber）
  - モード切替: ESP32 NVS `brokerMode=local|cloud`（AP モード設定 UI から変更）

- **証明書体制（ESP32 用）**:
  - AWS IoT コンソール or CLI で **ESP32 専用 device cert** を発行
  - 発行したファイルを ESP32 LittleFS `/certs/` に配備:
    - `/certs/aws-device-cert.pem`（デバイス証明書）
    - `/certs/aws-private-key.pem`（秘密鍵）
    - `/certs/AmazonRootCA1.pem`（Amazon Root CA）
  - 証明書の書込み方法: OTA or LocalServer 経由の専用 Web API（`012-0005` で設計）
  - `clientId` はクラウドモードでも `publicId`（`esp32s3-<hex>`）を使用

- **証明書体制（LocalServer 用）**:
  - AWS IoT コンソール or CLI で **LocalServer 専用 device cert** を別途発行
  - 証明書・秘密鍵・Amazon Root CA を LocalServer 側の安全なファイル領域に配備
  - ファイルの実パスは `IoT/LocalServer/.env`（Git 除外）に記載する（§5.6 参照）
  - LocalServer は AWS IoT Core を **subscribe のみ**で使用（受信専用）

- **IoT Policy 設計方針**:
  - topic 構造は現行 `esp32lab/<kind>/<sub>/<name>` を維持（完全互換）
  - ESP32 用 Policy: ESP32 の certARN に `Connect/Publish/Subscribe/Receive` を topic 単位で許可
  - LocalServer 用 Policy: LocalServer の certARN に `Connect/Subscribe/Receive` を許可（コマンド送信が必要になった場合は `Publish` を追加）
  - `clientId` 命名規則: ESP32 は `publicId`（`esp32s3-<hex>`）、LocalServer は `localserver-<hostname>`

- **JITP（Just-In-Time Provisioning）**:
  - 将来拡張候補。初版では静的 X.509 発行＋手動 IoT Policy 設定で実装

- **フォールバック方式**:
  - ESP32: AP モード → Web 設定 UI で `brokerMode=local` に変更 → 再起動 → ローカル Mosquitto へ接続
  - LocalServer: `CLOUD_MQTT_ENABLED=false` に変更 → 再起動 → AWS IoT Core への接続を試みない
  - [厳守] ESP32 と LocalServer は同時にモードを統一すること（片方だけ切替すると通信が分断される）

### 5.6 クラウド接続 ON/OFF 切替 IF [2026-05-20]

**LocalServer 側（`IoT/LocalServer/.env`）**:
- `CLOUD_MQTT_ENABLED=true|false`（クラウド subscribe 有効化フラグ）
- `CLOUD_PROVIDER=aws-iot-core`
- `AWS_IOT_ENDPOINT=<custom endpoint>.iot.ap-northeast-1.amazonaws.com`（AWS コンソールで取得）
- `AWS_IOT_CLIENT_CERT_PATH=<絶対パス>/localserver-cert.pem`（LocalServer 用証明書）
- `AWS_IOT_PRIVATE_KEY_PATH=<絶対パス>/localserver-private.key`（LocalServer 用秘密鍵）
- `AWS_IOT_CA_CERT_PATH=<絶対パス>/AmazonRootCA1.pem`（Amazon Root CA）
- `AWS_IOT_CLIENT_ID=localserver-<hostname>`（固定 clientId）

**ESP32 側（NVS、AP モード設定 UI から書込み）**:
- `brokerMode=local|cloud`（接続先切替フラグ）
- `cloudEndpoint=<endpoint>.iot.ap-northeast-1.amazonaws.com`（クラウド接続先）
- X.509 証明書は LittleFS `/certs/` に配備（上記 §5.4 参照）

- [厳守] 証明書実ファイルパスは `.env`（Git 除外）のみに記載し、ソースには記載しない
- [厳守] `CLOUD_MQTT_ENABLED=false` 時は AWS IoT Core への接続を試みない（LocalServer 起動時に判定）
- [厳守] LocalServer のクラウド設定を変更しても、ESP32 ↔ ローカル Mosquitto 通信には影響を与えない
- [厳守] ESP32 と LocalServer のモードは常に統一する（片方だけクラウドモードにしない）

### 5.7 Lambda / Cognito の役割 [2026-05-20]

| サービス | 本プロジェクトの役割 | 採用タイミング |
|---|---|---|
| **AWS IoT Core** | クラウド MQTT ブローカー（必須）| `012-0005` 実装時 |
| **IoT Rules Engine** | topic フィルタ → Lambda 実行 | 必要に応じて追加 |
| **Lambda** | IoT Rules 経由のメッセージ処理（DynamoDB / S3 等）| 必要に応じて追加 |
| **Cognito** | Web ダッシュボード / Mobile からの MQTT subscribe | 将来拡張候補 |
| **IAM** | LocalServer の AWS SDK 認証（`.env` の AccessKey/SecretKey）| `012-0005` 実装時 |

### 5.5 `009-0004`（LocalServer クラウド連携 I/F）への前提制約

[重要] `009-0004` / `012-0005` の LocalServer クラウド連携 I/F 設計は、本章の以下制約を必須前提とする。

| 前提制約 | 詳細 |
|---|---|
| k-device 非公開 | REST API / MQTT bridge いずれの経路でも raw k-device をクラウドへ送信しない |
| MQTT payload 暗号化維持 | クラウド経由時も AES-256-GCM（k-device）による payload 暗号化を外さない |
| 認証境界の分離 | ローカル側認証（ID/Password）とクラウド側認証（X.509）は別個に管理し、混在させない |
| 不可逆操作の排除 | クラウド連携 I/F から eFuse / Secure Boot 操作への経路を持ち込まない |
| AWS 認証情報の保護 | `LocalServer/.env` のみに保持し、API 応答・ログ・TS ソースへ含めない |

## 5. Cloud IF [将来対応]
- AWS IoT Core または Google Cloud IoT相当サービスを候補とする。
- 認証フロー、デバイス証明書配布、更新承認フローは第4段階で確定する。

## 6. LocalServer 管理者画面 IF
### 6.1 管理者認証 IF
- [厳守] `POST /api/admin/auth/login` は `username` と `password` を受け取り、成功時のみ管理者セッションを発行する。
- [厳守] 未認証状態では管理者画面系 API を `AUTH_REQUIRED` で拒否する。
- [推奨] `POST /api/admin/auth/logout` で管理者セッションを明示終了できるようにする。

### 6.1.1 パスワード変更・推奨運用 IF
- [重要] AP Web UI ロール別パスワード、LocalServer 管理者パスワード、関連サービス認証情報（MQTT/DB/外部連携）は同一ポリシーで管理する。
- [推奨] 初期値の継続利用を避けるため、変更 API または画面導線を提供する。
- `POST /api/admin/auth/password/change`
  - 用途: LocalServer 管理者パスワード変更
  - 最低入力: `currentPassword`、`newPassword`
- `POST /api/admin/ap/password/change`
  - 用途: AP Web UI ロール別パスワード変更
  - 最低入力: `role`、`currentPassword`、`newPassword`
- `POST /api/admin/credentials/rotation`
  - 用途: 関連サービス認証情報（MQTT/DB/外部連携）の変更
  - 最低入力: `targetType`、`targetId`、`newCredential`
- [厳守] パスワード/認証情報変更 API の監査ログには、`targetType`、`targetId`、`changedBy`、`changedAt`、`reason`、`result` を記録する。
- [厳守] 監査ログへ平文パスワードや復元可能な秘密値を出力しない。
- [推奨] 例外継続（固定値運用）を登録する場合は `reason`、`scope`、`expiresAt` を必須入力にする。

### 6.2 管理者機能 IF（鍵発行）
- `POST /api/admin/keys/k-user/issue`
  - 用途: `k-user` 発行要求
- `POST /api/admin/keys/k-device/issue`
  - 用途: `k-device` 発行要求
- [厳守] 応答へ raw key を返さない。

### 6.3 AP一括設定 IF
- `POST /api/admin/ap/batch/start`
  - 用途: Wi-Fi USB 指定で AP探索 -> 接続 -> 自動設定を開始する。
- `GET /api/admin/ap/batch/{batchId}`
  - 用途: 進捗と結果取得。
- [厳守] `requestedSettings` にネットワーク設定、パスワード、`k-device` を含める。

### 6.4 統合デバイス一覧 IF
- `GET /api/admin/devices`
  - 用途: AP接続個体と MQTT接続個体を統合表示する。
- [厳守] 応答項目へ少なくとも以下を含める。
  - `deviceId` / `publicId`
  - `connectionMode` (`ap` / `mqtt`)
  - `fwVersion`
  - `fsVersion`
  - `rewriteProgress`
  - `apWebUrl`（AP機体のみ）

### 6.5 メンテナンス再起動 IF
- `POST /api/admin/commands/maintenance-reboot`
  - 用途: MQTT接続中ESP32へ `call/maintenance` を送信する。
- [厳守] eFuse 操作 API は LocalServer 側へ実装しない。

### 6.6 `ProductionTool` 基本機能 IF [実装後に使用]
- [重要] 本節は `004-0008`〜`004-0010` の基本機能安定化フェーズを対象とする。
- [厳守] 本節の IF は起動、追加認証、対象機確認、dry-run、監査ログ確認までとし、eFuse / Secure Boot / Flash Encryption の不可逆処理本体は含めない。
- [厳守] `ProductionTool` は `LocalServer` とは別の実行物、別の認証入口、別の監査ログ導線を持つ。
- [厳守] `ProductionTool` は `LocalServer` と別ソフトとして独立動作し、`SecretCore` 共通部再利用はソフト統合を意味しない。

### 6.6.1 `ProductionTool` 追加認証 IF
- `POST /api/production/auth/login`
  - 用途: メーカーモード専用の追加認証を開始する。
  - 最低入力:
    - `operatorId`
    - `password`
    - `workOrderId`
  - 応答項目例:
    - `result`
    - `sessionId`
    - `role`
    - `expiresAt`
    - `detail`
- [厳守] 通常管理者パスワードでは代替できない。
- [厳守] 平文パスワード、復元可能な秘密値を応答や監査ログへ出さない。

### 6.6.2 対象機確認 IF
- `GET /api/production/devices`
  - 用途: `ProductionTool` が対象機の識別情報とセキュア化状態を表示する。
  - 応答項目例:
    - `deviceName`
    - `serialNumber`
    - `macAddress`
    - `publicId`
    - `firmwareVersion`
    - `secureState`
    - `connectionState`
    - `detail`
- [厳守] 本 IF は対象機確認用であり、不可逆処理開始を意味しない。
- [推奨] 対象機確認文言の再入力結果は `ProductionTool` 側ローカル状態または監査ログで保持する。

### 6.6.3 dry-run IF
- `POST /api/production/dry-run/start`
  - 用途: 不可逆処理本体へ入らずに、`ProductionTool` の認証、対象機確認、事前条件確認、監査ログ、停止点を確認する。
  - 最低入力:
    - `targetDeviceName`
    - `sessionId`
    - `requestedOperation`
  - 応答項目例:
    - `result`
    - `workflowId`
    - `state`
    - `detail`
- `GET /api/production/workflows/{workflowId}`
  - 用途: dry-run の進捗状態と結果を取得する。
  - 応答項目例:
    - `workflowId`
    - `state`
    - `result`
    - `errorSummary`
    - `updatedAt`
- [厳守] dry-run は eFuse / Secure Boot / Flash Encryption の本実行手前で停止する。
- [厳守] dry-run 応答に raw key、中間秘密、eFuse 実値、署名素材を含めない。

### 6.6.4 `ProductionTool` 監査ログ IF
- `GET /api/production/audit-logs`
  - 用途: `ProductionTool` の起動、追加認証、対象機確認、dry-run、安全停止の監査ログを取得する。
  - 応答項目例:
    - `eventType`
    - `operatorId`
    - `targetDeviceName`
    - `result`
    - `errorCode`
    - `loggedAt`
- [厳守] 監査ログ IF は秘密値を返さない。
- [重要] `ProductionTool` のログ保存先は `ProductionTool画面仕様書.md` の既定候補と整合させる。

## 7. 変更履歴
- 2026-05-10: `set/keyDeviceSet` と `set/fileLogSet` の復号後payloadにも `sigAlg=HMAC-SHA256` と `signature` を必須化した。理由: `008-0007` 実装に合わせ、重要設定変更 command の改ざん検知条件を IF 契約へ固定するため。
- 2026-05-07: `otaStart` の復号後payloadへ `sigAlg=HMAC-SHA256` と `signature` を必須化し、ESP32 が `k-device` で検証失敗時に開始拒否する条件を追記。理由: `008-0006` 実装に合わせ、OTA command の改ざん検知条件を IF 契約へ固定するため。
- 2026-03-16: AP Pairing secure bundle apply IF（`POST /api/pairing/secure-bundle`）を追加。理由: `009-0019` の encrypted bundle 本体送達 / 復号 / NVS 保存実装に合わせ、送受信契約（AES-256-GCM + 固定AAD + payloadSha256）を IF として固定するため。
- 2026-03-16: `ProductionTool` へ名称統一し、`LocalServer` と別ソフト・独立動作であることを追記。理由: AP 共通画面や基本機能 IF における名称統一と、共通化/分離の解釈ずれを防ぐため。
- 2026-03-16: `6.6 ProductionTool 基本機能 IF` を追加。理由: `004-0008`〜`004-0010` の起動・追加認証・対象機確認・dry-run・監査ログ導線を、不可逆処理本体と分離して IF 契約として先に固定するため。
- 2026-03-16: AP Pairing transport handshake IF（`POST /api/pairing/transport-handshake`）を追加。理由: `runPairingSession()` が secure transport の placeholder 交渉を越えて P-256 ECDH handshake まで進んだため。
- 2026-03-16: AP Pairing transport session IF（`POST /api/pairing/transport-session`）を追加。理由: `runPairingSession()` が secure bundle 本体前の交渉状態を AP 側へ保持できる placeholder 段階へ進んだため。
- 2026-03-16: AP Pairing bundle summary IF（`POST /api/pairing/bundle-summary`）を追加。理由: `runPairingSession()` が秘密本体を送らずに AP 側 `bundle_staged` 状態まで進む placeholder 段階へ進んだため。
- 2026-03-16: AP Pairing session metadata IF（`POST /api/pairing/session`）を追加。理由: `runPairingSession()` が平文秘密を送らずに AP 側受理状態を保持し、`verifying` 手前まで進む placeholder 段階へ進んだため。
- 2026-03-13: AP/管理者/サービス認証情報の「変更推奨運用」IFを追加し、変更APIおよび監査ログ必須項目を明記。理由: 初期パスワード方針変更を他パスワード類へ横展開し、実装・試験の参照先を統一するため。
- 2026-03-12: `mqttUrlName` / `serverUrlName` / `otaUrlName` / `timeServerUrlName` / `keyDevice` の実装追従完了に合わせ、4.4 の「将来対応」表記を「反映済み」へ更新。理由: 仕様先行の注記を残すと実装状態と齟齬になるため。
- 2026-03-12: MQTT payload全文暗号化（`k-device` / `A256GCM`）を必須条件へ昇格し、エンベロープキー（`security.mode` / `enc.*`）と運用モード（plain/compat/strict）を追記。理由: LocalServer-ESP32間の暗号化通信を実装・試験した内容をIF契約へ反映するため。
- 2026-03-11: LocalServer 管理者画面 IF（認証、鍵発行、AP一括設定、統合一覧、メンテナンス再起動）を追加。理由: 管理画面要件を接続契約として実装可能な粒度へ固定するため。
- 2026-03-11: AP画面詳細（ロール定義・権限制御・表示項目）の正規参照先として `APメンテナンス画面仕様書.md` を追加。理由: IF仕様書を接続契約中心に保ちつつ、画面仕様の更新先を固定するため。
- 2026-03-11: HTTPS OTA IF の参照先へ `OTA_HTTPコマンド仕様書.md` を追加。理由: 方式仕様とHTTP API仕様の分離に追従するため。
- 2026-03-11: AP名を `AP-esp32lab-<MAC(no colon)>` へ統一し、APメンテナンス一括運用IF（探索→接続→ログイン→更新→再起動→`status`判定）と未認証API拒否を追加。理由: 複数台メンテナンス運用とログイン必須要件をIFとして固定するため。
- 2026-03-10: 4-5 強化として画像更新IFへ HMAC/署名必須を追記し、`imagePackageApply`（ZIP一括更新）の将来対応を追加。理由: OTA同等のセキュリティ要件を画像更新にも適用するため。
- 2026-03-10: `requestedSettings` へ `hostName` 系と `keyDevice` を追記（実装追従は別タスク）。理由: `common.h` で追加した network キーと IF 定義の整合を先に取るため。
- 2026-03-10: `rollbackTestEnable` / `rollbackTestDisable` のIFを追加。理由: 7025 の `未確定起動失敗` 再現手順をIF仕様へ固定するため。
- 2026-03-10: 4.12（多重接続 / 冗長化 IF）を追加し、`serverId` / `brokerId` / `wifiApId`、複数 `Server` 同時接続前提、`k-user` 保護方式選択、`k-device` 共通/分離選択を追記。理由: 冗長化と分散配置を見込んだ I/F 前提を固定するため。
- 2026-03-09: `status` 通知へ 7015/7025 試験用の一時項目（`runningPartition` / `bootPartition` / `nextUpdatePartition`）を追加し、試験完了後に削除する方針を追記。理由: A/B 切替確認を確実に行いつつ、恒久仕様に試験項目を残さないため。
- 2026-03-09: 4.9〜4.11（`LittleFS` 資産更新 IF、証明書 IF、ファイルログ IF）を追加。理由: `NVS` / `LittleFS` の責務、`/images` `/certs` `/logs`、差分更新とログ保持の I/F を明確化するため。
- 2026-03-09: 4.3 見出しを `runPairingSession()` 中心の表現へ更新。理由: IF 文書の章タイトルも公開 workflow と内部 helper の関係に合わせるため。
- 2026-03-09: `runPairingSession()` ワークフロー IF と workflow 状態 IF を追加し、`createPairingBundle` を内部処理として整理。理由: 高リスク処理を `SecretCore` 主導で完了判定まで実行する構成へ IF を揃えるため。
- 2026-03-09: [仕様変更] `publicId` 初期値を `IoT_<macアドレスからコロン除去>` とする方針へ更新。理由: 運用上わかりやすさを優先し、初期導入時の識別を容易にするため。
- 2026-03-09: AP 共通トップ画面 IF、`createPairingBundle` の内部項目、ECDH + 固定公開鍵による pairing IF、current/previous key 運用 IF を追加。理由: AP モード投入と逐次再ペアリングの I/F を詳細化するため。
- 2026-05-19: **TPM 前提を撤回**し、`k-user` 保護方式を `OS暗号化サービス由来方式`（現行: Windows DPAPI、ソフトウェア暗号化・チップ非依存）へ正本更新。Mac/Linux 汎用化は **`020-0004`**。理由: 実装は最初から DPAPI 固定だが IF 仕様書のみ TPM 前提のまま残置していたため、設計仕様書群と同期して正本を実装側へ揃える。
- 2026-03-08: 鍵導出方式を `TPM + wrapped_secret + HKDF` 方式へ更新。理由: IF前提となる鍵生成式と保存物を正式仕様へ合わせるため。
- 2026-03-07: OTA IFを現行仕様へ更新し、`OTA仕様書.md` 参照と進捗/リトライ/SHA256検証要件を追記。
- 2026-03-07: LocalServer初期実装の topic（status/otaStart/status notice）を追記。
- 2026-02-24: 新規作成。MQTT/HTTPS IFの基本仕様を定義。
- 2026-02-24: 第3段階方針へ合わせ、MQTT TLS + ID/パスワード認証を必須化。第2段階OTA IFを現行対象外へ変更。
- 2026-02-24: Wi-Fi設定更新IF（public_idトピック、AES-GCM payload、confirm）を追加。
