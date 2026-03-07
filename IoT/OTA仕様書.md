# IoT OTA仕様書

[重要] 本書はESP32のOTA更新仕様を定義する。  
理由: MQTT指令、HTTPS取得、進捗通知、再試行、整合性確認を一貫した手順で実装するため。

[厳守] OTAは「指令はMQTT」「実ファイル取得はHTTPS（ESP32がpull）」で実施する。  
理由: サーバーからESP32への直接書換を避け、NAT配下でも安定運用するため。

[禁止] 検証省略（SHA256未検証、TLS検証無効）を本番運用に持ち込まない。  
理由: 改ざん・破損・なりすまし更新を防止するため。

## 1. OTA方式
### 1.1 方式①（主系）
- LocalServer（またはCloudServer）から MQTT で `otaStart` を指令する。
- ESP32は指令を受信後、指定URLへHTTPS接続してOTAを開始する。
- ESP32は分割書込みを行い、LCD表示とMQTT通知で進捗を報告する。

### 1.2 方式②（副系）
- ESP32 APモード（メンテナンスモード）での書換を提供する。
- 本書の詳細対象は方式①とする（方式②は別途拡張）。

## 2. 方式①の詳細仕様
### 2.1 MQTT指令（otaStart）
- トピック: `esp32lab/call/otaStart/<deviceName|all>`
- `args` 必須項目:
  - `firmwareVersion`（更新後バージョン）
  - `firmwareUrl`（`https://.../firmware.bin`）
  - `manifestUrl`（`https://.../manifest.json`）
  - `sha256`（配布前計算済み SHA256 16進文字列）
  - `chunkSize`（分割書込みサイズ。例: 4096）
  - `sameProgressRetryMax`（同一進捗率リトライ回数。固定値: `3`）
  - `sameProgressRetryIntervalSeconds`（同一進捗率リトライ間隔秒。固定値: `5`）
  - `fullRestartRetryMax`（最初から再試行回数。固定値: `3`）

### 2.2 HTTPS取得と分割書込み
- ESP32は `manifestUrl` を取得し、配布メタ情報を確認する。
- 続けて `firmwareUrl` をHTTPSで取得し、分割書込みする。
- 書込み中は受信済みバイト数から進捗率を算出する（0〜100%）。
- [推奨] 進捗通知は1%以上変化時、または5秒ごとに送信する。

### 2.3 進捗通知（LCD + MQTT）
- LCD表示: `OTA xx%` を常時更新する。
- MQTT通知:
  - トピック: `esp32lab/notice/otaProgress/<deviceName>`
  - 送信項目例:
    - `otaSessionId`
    - `firmwareVersion`
    - `progressPercent`（0〜100）
    - `phase`（`download` / `write` / `verify` / `done` / `error`）
    - `retryCountSameProgress`
    - `retryCountFullRestart`
    - `detail`
- LocalServer画面はデバイス別に最新進捗を表示する。

### 2.4 リトライ制御
- 同一進捗率で停止した場合:
  - 5秒おきに最大3回まで再試行する。
  - 3回失敗で「最初から再試行」へ移行する。
- 最初から再試行:
  - 最大3回まで実施する。
  - 3回失敗でOTA失敗として終了し、MQTTへNG通知する。

### 2.5 整合性確認（SHA256）
- サーバー側で配布前に `firmware.bin` の SHA256 を計算し、`otaStart` で送信する。
- ESP32は受信・書込み完了後に同じSHA256を計算し、指令値と一致比較する。
- 一致時のみ更新完了とし、次回起動パーティションを更新先へ切替える。
- 不一致時はNG終了し、更新確定を行わない。

### 2.6 デュアルパーティション運用
- ESP32は2面（A/B）構成で更新する。
- 非実行側へ書込み後、検証成功時のみ `boot` 対象を更新側へ切替える。
- 次回起動で更新側を起動し、起動成功後に確定通知を送る。

## 3. 完了・異常通知
- 完了時:
  - MQTTで `result=OK` と `firmwareVersion`、`sha256Verified=true` を通知する。
- 異常時:
  - MQTTで `result=NG`、`errorCode`、`detail`、`retryCount` を通知する。
- [推奨] 失敗要因は `networkTimeout` / `httpStatus` / `shaMismatch` / `writeError` などに分類する。

## 4. セキュリティ要件
- [厳守] HTTPS証明書検証を有効化する。
- [厳守] MQTT経路はTLS + ID/パスワード認証を必須とする。
- [厳守] 配布URLは短寿命トークン化または閉域配布を推奨する。
- [禁止] ファームウェア実体や署名鍵を公開領域へ平文配置しない。

## 5. 変更履歴
- 2026-03-07: 新規作成。方式①（MQTT指令 + HTTPS OTA）の詳細仕様、リトライ制御、SHA256検証、2面切替を定義。
