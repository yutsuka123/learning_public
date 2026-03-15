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
- [厳守] AP 名は `AP-esp32lab-<MAC(no colon)>` とする。
- [厳守] AP 接続後は共通トップ画面パスワード認証を要求する。
- [厳守] 共通トップ画面から Production 画面へ進む場合は、メーカーモード専用パスワードの追加認証を要求する。
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
- [重要] LocalServer / Production は、指定USB Wi-Fiインタフェースで `AP-esp32lab-<MAC(no colon)>` を探索する。
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
- [重要] `k-user` 保護方式は `TPM由来方式` または `暗号化ファイル + パスワード方式` を許容し、復号処理は `SecretCore` の Rust モジュール内で完結する。
- [厳守] `k-device` は `Server` ごとに分離する運用と、複数 `Server` 間で共通利用する運用をユーザー選択で切り替える。

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

## 7. 変更履歴
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
- 2026-03-08: 鍵導出方式を `TPM + wrapped_secret + HKDF` 方式へ更新。理由: IF前提となる鍵生成式と保存物を正式仕様へ合わせるため。
- 2026-03-07: OTA IFを現行仕様へ更新し、`OTA仕様書.md` 参照と進捗/リトライ/SHA256検証要件を追記。
- 2026-03-07: LocalServer初期実装の topic（status/otaStart/status notice）を追記。
- 2026-02-24: 新規作成。MQTT/HTTPS IFの基本仕様を定義。
- 2026-02-24: 第3段階方針へ合わせ、MQTT TLS + ID/パスワード認証を必須化。第2段階OTA IFを現行対象外へ変更。
- 2026-02-24: Wi-Fi設定更新IF（public_idトピック、AES-GCM payload、confirm）を追加。
