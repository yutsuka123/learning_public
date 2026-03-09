# MQTTコマンド仕様書

[重要] 本書は、ESP32とサーバー（Local/Cloud）間のMQTT通信仕様を定義する。
理由: 通信プロトコルの整合性を保ち、デバイス・サーバー間の相互運用性を確保するため。

## 1. 通信概要

### 1.1 プロトコル
- **Transport**: MQTT over TCP
- **Encryption**:
  - [初期] 非TLS（デバッグ容易性のため）
  - [厳守] 運用時はTLS（MQTTS, Port 8883）へ切り替えること。
- **Quality of Service (QoS)**:
  - [推奨] QoS 1 (At least once) を基本とする。
- **Retain**:
  - [推奨] Retainは使用しない（過去のコマンドが誤って再配信される事故を防ぐため）。

## 2. トピック設計

### 2.1 トピック構成
以下の構成とする。定義値は `shared/include/common.h` を参照のこと。

| 分類 | トピックパターン | 説明 |
| :--- | :--- | :--- |
| **通知/要求** | `esp32lab/<kind>/<sub>/<name>` | kind=`notice/set/get/call/network` |

- `<kind>`: メッセージ種別（`notice`, `set`, `get`, `call`, `network`）。
- `<sub>`: サブコマンド値（`kSub` と一致）。
- `<name>`: 識別子（`status` は発信者名、`set/get/call/network` は受信者名）。
- 発信者名はデバイス側で `public_id` を既定とし、初期値は `IoT_<macアドレスからコロン除去>` とする。

### 2.2 定義済み CommandName / EventName
`common.h` に定義された以下のコマンド名を基本とする。

| 名称 | 定義値 | 目的 |
| :--- | :--- | :--- |
| **set** | `set` | 設定要求（パラメータ変更など） |
| **get** | `get` | 取得要求（情報の取得） |
| **call** | `call` | 実行要求（アクションのトリガー） |
| **status** | `status` | 状態通知（定期通知、起動通知など） |
| **network** | `network` | ネットワーク/MQTT設定更新 |

※ `reboot` などの具体的動作は `call` コマンドの `sub` パラメータや、個別の `<commandName>` として定義することも許容するが、基本は上記に集約することを推奨する。

## 3. ペイロード設計

JSON形式とし、リクエスト（Request）とレスポンス（Response）を明確に分ける。

### 3.1 共通ヘッダ
すべてのメッセージに以下のフィールドを含めること。

| フィールド | キー | 型 | 必須 | 説明 |
| :--- | :--- | :--- | :--- | :--- |
| バージョン | `v` | int | Yes | スキーマバージョン。現在は `1`。 |
| 宛先ID | `DstID` | string | Yes | 発信先ID。ブロードキャスト時は `all` を使用。 |
| 送信元ID | `SrcID` | string | Yes | 発信元ID。 |
| MACアドレス | `macAddr` | string | Yes | デバイスのBase MACアドレス（変更不可の固有ID）。 |
| ネットワークMACアドレス | `macAddrNetwork` | string | No | ネットワークアダプタMAC。ESP32は通常Wi-Fi MACを設定する。 |
| メッセージID | `id` | string | Yes | `<deviceId>-<timestamp>-<seq>` 形式を推奨。Req/Res紐付け用。 |
| タイムスタンプ | `ts` | string | Rec | ISO8601拡張形式（UTCミリ秒、末尾`Z`）を使用。 |
| 操作名 | `op` | string | Yes | トピックの `<commandName>` と一致させる。 |
| 応答結果 | `Res` | string | Rec | 要求を受けた側の実行結果。`OK` / `NG` / `BUSY` を使用する。`NG` の場合は `detail` に理由を記載する。 |

### 3.2 コマンドリクエスト (cmd/...)
サーバーからデバイスへの要求。

```json
{
    "v": 1,
    "DstID": "all",
    "SrcID": "server-001",
    "macAddr": "00:11:22:33:44:55",
    "id": "server-001-20260301120000-001",
    "ts": "2026-03-01T12:00:00.000Z",
    "op": "call",
    "sub": "reboot",
    "args": {
        "delayMs": 500
    }
}
```

- **sub**: サブコマンド（任意）。`op` が `call` の場合に `reboot` などを指定する。
- **args**: 引数オブジェクト（任意）。コマンドごとのパラメータ。

**sub値（`kSub`）の運用**:
- [厳守] `shared/include/common.h` の `iotCommon::mqtt::subCommand` 定義値を使用する（ハードコード禁止）。

### 3.3 コマンドリクエスト詳細: network
ネットワーク設定およびMQTT接続設定の変更を行う。

**トピック**: `esp32lab/network/<sub>/<receiverName>`

**Payload**:
```json
{
    "v": 1,
    "DstID": "all",
    "SrcID": "server-001",
    "macAddr": "...",
    "id": "...",
    "ts": "...",
    "op": "network",
    "args": {
        "wifiSSID": "my-ssid",
        "wifiPass": "my-pass",
        "mqttUrl": "mqtt.example.com",
        "mqttUser": "user",
        "mqttPass": "pass",
        "mqttTls": true,
        "mqttPort": 8883,
        "server": {
            "serverUrl": "api.example.com",
            "serverUser": "apiUser",
            "serverPass": "apiPass",
            "serverPort": 443,
            "serverTls": true
        },
        "ota": {
            "otaUrl": "ota.example.com",
            "otaUser": "otaUser",
            "otaPass": "otaPass",
            "otaPort": 443,
            "otaTls": true
        },
        "timeServer": {
            "timeServerUrl": "ntp.example.com",
            "timeServerPort": 123,
            "timeServerTls": false
        },
        "apply": false,
        "reboot": true
    }
}
```

| キー | 型 | 必須 | 説明 |
| :--- | :--- | :--- | :--- |
| `wifiSSID` | string | No | 新しいSSID。指定なしなら変更しない。 |
| `wifiPass` | string | No | 新しいWi-Fiパスワード。 |
| `mqttUrl` | string | No | MQTTブローカーのアドレス（ホスト名/IP）。 |
| `mqttUser` | string | Yes | [厳守] MQTTユーザー名。空文字は禁止。 |
| `mqttPass` | string | Yes | [厳守] MQTTパスワード。空文字は禁止。 |
| `mqttTls` | bool | Yes | [厳守] TLSを使用するか (`true`/`false`)。運用時は `true` 固定。 |
| `mqttPort` | int | Yes | [厳守] MQTTポート番号。TLS時は `8883` を使用。 |
| `server` | object | No | [重要] HTTP/HTTPS 通信先設定。`serverUrl/serverUser/serverPass/serverPort/serverTls` を保持。 |
| `ota` | object | No | [重要] OTA配信先設定。`otaUrl/otaUser/otaPass/otaPort/otaTls` を保持。 |
| `timeServer` | object | No | [重要] 時刻同期先設定。`timeServerUrl/timeServerPort/timeServerTls` を保持。 |
| `apply` | bool | Yes | `true`: 即時反映を試みる, `false`: 再起動後に反映。 |
| `reboot` | bool | Yes | `true`: コマンド処理後に再起動する。 |

**備考**:
- パスワード等の機密情報が含まれるため、通信経路の暗号化（MQTTS）が必須である。
- [厳守] TLS有効時は `sensitiveData.h` の `SENSITIVE_MQTT_TLS_CA_CERT` にCA証明書(PEM)を設定する。未設定時は接続を拒否する。
- Wi-Fi設定変更時は、接続断が発生する可能性がある。`apply: false` + `reboot: true` の組み合わせを推奨する。
- [厳守] `serverTls` / `otaTls` が `true` の場合は `serverPort` / `otaPort` を `443` に揃えること。理由: HTTPSの標準ポートに統一し、運用ミスを減らすため。
- [推奨] `timeServer` はNTP(UDP/123)を基本とし、`timeServerTls` は原則 `false` 運用とする。理由: 現行実装がNTP前提のため。

### 3.3.1 Pairing / Re-Pairing 補足
- [重要] `k-device` 初回投入および `k-user` 再発行後の再ペアリングは、MQTT 経由ではなく AP モード + `runPairingSession()` を正規経路とする。
- [厳守] `createPairingBundle` は `runPairingSession()` 内部 helper とし、少なくとも `targetDeviceId` `sessionId` `keyVersion` `requestedSettings` を入力に含める。
- [厳守] `requestedSettings` の必須項目（Wi-Fi / MQTT / OTA / 認証情報）は TS 側で事前検証し、不足時は `SecretCore` を呼び出さない。
- [厳守] ESP32 は固定公開鍵で bundle 署名を検証し、AP モード接続時の ECDH による `k-pairing-session` で bundle を復号する。
- [厳守] TS へ raw `k-device`、ECDH 共有秘密、`k-pairing-session` を返さない。
- [厳守] ESP32 は `k-device`、Wi-Fi / MQTT / OTA / 各認証情報を NVS に保存し、current/previous の 2 スロットで管理する。
- [禁止] `POST /api/pairing/bundle` のような `createPairingBundle` 直公開 API を提供しない。

### 3.4 コマンドレスポンス (res/...)
デバイスからサーバーへの応答。

```json
{
    "v": 1,
    "DstID": "all",
    "SrcID": "device-001",
    "macAddr": "AA:BB:CC:DD:EE:FF",
    "id": "server-001-20260301120000-001",
    "ts": "2026-03-01T12:00:01.000Z",
    "result": "OK",
    "detail": null
}
```

- **id**: リクエストの `id` をそのまま返却する（相関ID）。
- **result**: `OK` またはエラーコード（`NG`, `ERROR_xxx`）。
- **detail**: 追加情報（エラー詳細や取得データ）。

### 3.5 コマンドリクエスト詳細: otaStart
OTA開始要求を行う。

**トピック**: `esp32lab/call/otaStart/<receiverName>`

**Payload例**:
```json
{
    "v": 1,
    "DstID": "all",
    "SrcID": "server-001",
    "id": "server-001-20260307150000-00001",
    "ts": "2026-03-07T15:00:00.000Z",
    "op": "call",
    "sub": "otaStart",
    "args": {
        "firmwareVersion": "1.2.3",
        "manifestUrl": "https://ota.example.local/ota/manifest.json",
        "firmwareUrl": "https://ota.example.local/ota/firmware.bin",
        "sha256": "0123456789abcdef...",
        "chunkSize": 4096,
        "sameProgressRetryMax": 3,
        "sameProgressRetryIntervalSeconds": 5,
        "fullRestartRetryMax": 3
    }
}
```

| キー | 型 | 必須 | 説明 |
| :--- | :--- | :--- | :--- |
| `firmwareVersion` | string | Yes | 更新対象バージョン。 |
| `manifestUrl` | string | Yes | HTTPSのmanifest URL。 |
| `firmwareUrl` | string | Yes | HTTPSのfirmware URL。 |
| `sha256` | string | Yes | サーバー事前計算済みSHA256。 |
| `chunkSize` | int | Yes | 分割書込みサイズ（byte）。 |
| `sameProgressRetryMax` | int | Yes | 同一進捗率リトライ上限（固定:3）。 |
| `sameProgressRetryIntervalSeconds` | int | Yes | 同一進捗率リトライ間隔秒（固定:5）。 |
| `fullRestartRetryMax` | int | Yes | 最初から再試行の上限（固定:3）。 |

### 3.6 OTA進捗通知: otaProgress
ESP32からサーバーへ進捗を通知する。

**トピック**: `esp32lab/notice/otaProgress/<senderName>`

**Payload例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "IoT_F0D0F94EB580-20260307150100-00010",
    "ts": "2026-03-07T15:01:00.000Z",
    "op": "notice",
    "sub": "otaProgress",
    "otaSessionId": "ota-20260307150000-00001",
    "firmwareVersion": "1.2.3",
    "phase": "write",
    "progressPercent": 30,
    "retryCountSameProgress": 1,
    "retryCountFullRestart": 0,
    "Res": "OK",
    "detail": "writing"
}
```

## 4. ステータス・Will

### 4.1 Status (notice/...)
デバイス起動時や定期的、または状態変化時に送信する。

- **起動通知**: 電源ON時に `op`: `status`, `sub`: `start-up` 等で通知。
- **切断通知**: LWT (Last Will and Testament) を利用し、切断時にサーバーへ通知されるよう設定する。
- **detail運用**: [重要] `detail` には通知理由を設定する。現在の正規化値は `StartUp` / `button` / `Reply` / `Restart(Button)` / `Restart(abort)` / `Restart(Call)`。
- [仕様変更] `public_id` の初期値は `IoT_<macアドレスからコロン除去>` を許容する。

### 4.2 Will (遺言)
- トピック: `esp32lab/notice/status/<senderName>`
- ペイロード: `{"op": "status", "result": "disconnect", ...}` 相当のJSON。
- デバイス接続時にMQTTブローカへ登録しておくこと。

## 5. 変更履歴
- 2026-03-09: Pairing / Re-Pairing 補足を `runPairingSession()` 正規経路と `createPairingBundle` 内部 helper 前提へ更新。理由: MQTT 仕様文書でも公開 workflow と内部秘密処理の境界を他文書と一致させるため。
- 2026-03-09: [仕様変更] `public_id` の初期値を `IoT_<macアドレスからコロン除去>` とする前提へ更新。理由: 運用上わかりやすさを優先し、初期導入時の識別を容易にするため。
- 2026-03-09: `createPairingBundle`、AP モード ECDH、固定公開鍵検証、ESP32 NVS current/previous 2 スロット運用の補足を追加。理由: MQTT 仕様から見た pairing と識別子運用の前提を明確化するため。
- 2026-03-07: `otaStart` 指令ペイロードと `otaProgress` 通知仕様（進捗/再試行/SHA256）を追加。
- 2026-03-05: `network` コマンド引数に `server` / `ota` / `timeServer` 設定を追加。理由: `sensitiveData.json` の管理項目拡張とプロトコル定義を同期するため。
- 2026-03-01: 初版作成。`common.h` と定義を同期。
