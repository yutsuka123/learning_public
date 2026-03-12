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
  - [厳守] コマンド系（`set` / `get` / `call` / `network`）の publish は QoS 1 (At least once) を使用する。
  - [重要] LocalServer 実装では QoS 1 固定で publish する。
  - [将来対応] ESP32 側 publish はライブラリ制約により QoS 0 となる経路があるため、QoS 1 対応クライアントへの移行を検討する。
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

### 2.3 サブコマンド一覧（現行確定）
[重要] 実装者が `sub` の命名ゆれで相互接続不良を起こさないよう、現行採用値を固定する。

| `op` | `sub` | 方向 | 主用途 | 必須 `args` |
| :--- | :--- | :--- | :--- | :--- |
| `call` | `status` | Server -> ESP32 | 状態応答要求 | なし |
| `call` | `otaStart` | Server -> ESP32 | OTA開始 | `firmwareVersion` `manifestUrl` `firmwareUrl` `sha256` |
| `call` | `rollbackTestEnable` | Server -> ESP32 | rollback試験有効化 | `requestType` `mode` |
| `call` | `rollbackTestDisable` | Server -> ESP32 | rollback試験無効化 | `requestType` `mode` |
| `network` | `network` | Server -> ESP32 | 接続・運用設定更新 | `mqttUser` `mqttPass` `mqttTls` `mqttPort` `apply` `reboot` |
| `call` | `fileSyncPlan` | Server -> ESP32 | 差分更新計画通知 | `sessionId` `targetArea` `basePath` `deleteMode` `files` |
| `call` | `fileSyncChunk` | Server -> ESP32 | ファイルチャンク転送 | `sessionId` `targetArea` `path` `chunkIndex` `chunkCount` `dataBase64` |
| `call` | `fileSyncCommit` | Server -> ESP32 | 更新反映・削除適用 | `sessionId` `targetArea` `deleteMode` |
| `notice` | `fileSyncStatus` | ESP32 -> Server | 差分更新進捗通知 | `sessionId` `targetArea` `phase` `result` |
| `notice` | `otaProgress` | ESP32 -> Server | OTA進捗通知 | `otaSessionId` `phase` `progressPercent` |
| `notice` | `status` | ESP32 -> Server | 定期/起動/応答通知 | `result` または状態情報 |
| `call` | `imagePackageApply` | Server -> ESP32 | ZIP一括画像更新（将来） | `sessionId` `packageUrl` `packageSha256` `destinationDir` |

- [厳守] `sub` 値は大文字小文字を区別し、文書定義どおりに実装する。
- [禁止] 同じ意味の別名 `sub` を実装ごとに増やさない。

### 2.4 サブコマンド一覧（I/O運用追加）
[重要] 現場運用で使用する温湿度、リレー、LED、ボタン、GPIO、ログ、再起動、メンテナンスモード遷移の `sub` を以下に追加定義する。

| `op` | `sub` | 方向 | 主用途 | 必須 `args` |
| :--- | :--- | :--- | :--- | :--- |
| `notice` | `trh` | ESP32 -> Server | 温湿度通知（TRH） | `temperatureC` `humidityRh` |
| `get` | `trh` | Server -> ESP32 | 温湿度取得要求（TRH） | なし |
| `set` | `relay` | Server -> ESP32 | リレー状態設定 | `channel` `state` |
| `get` | `relay` | Server -> ESP32 | リレー状態取得 | `channel` |
| `set` | `led_ON` | Server -> ESP32 | 指定番号LEDをON | `index` |
| `set` | `led_OFF` | Server -> ESP32 | 指定番号LEDをOFF | `index` |
| `set` | `led_Blink` | Server -> ESP32 | 指定番号LEDを点滅 | `index` `intervalMs` |
| `get` | `led` | Server -> ESP32 | LED状態取得 | `index` |
| `get` | `button` | Server -> ESP32 | 指定番号ボタン状態取得 | `index` |
| `set` | `gpio_H` | Server -> ESP32 | 指定GPIOをHighへ設定 | `index` |
| `set` | `gpio_L` | Server -> ESP32 | 指定GPIOをLowへ設定 | `index` |
| `get` | `gpio` | Server -> ESP32 | GPIO状態取得 | `index` |
| `get` | `log` | Server -> ESP32 | ログ取得 | `limit`（任意） |
| `call` | `restart` | Server -> ESP32 | 再起動命令 | `delayMs`（任意） |
| `call` | `maintenance` | Server -> ESP32 | メンテナンス(AP)モード遷移命令 | `reason`（任意） |

- [旧仕様] `Botton` / `giio` / `giio_H` / `giio_L` / `mentenance` は過去表記として受信許容してよい。
- [推奨] 新規実装と新規送信では `button` / `gpio` / `gpio_H` / `gpio_L` / `maintenance` を使用する。
- [厳守] メンテナンスモード命令は受信後に再起動して AP モードへ遷移する。

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

### 3.1.1 セキュリティ拡張ヘッダ（推奨/一部厳守）
[重要] `fileSync` 系と `otaStart` は高リスク操作のため、HMAC/署名検証前提の拡張ヘッダを定義する。

| フィールド | キー | 型 | 必須 | 説明 |
| :--- | :--- | :--- | :--- | :--- |
| 署名方式 | `sigAlg` | string | Rec | 例: `HMAC-SHA256` / `ECDSA-P256-SHA256` |
| 署名値 | `signature` | string | Rec | Base64署名値。 |
| キー識別子 | `keyId` | string | Rec | 検証鍵識別子。 |
| リプレイ防止nonce | `nonce` | string | Rec | 使い回し防止値。 |
| 期限 | `exp` | string | Rec | ISO8601 UTC。期限切れ要求を拒否する。 |

- [厳守] `fileSyncPlan` / `fileSyncChunk` / `fileSyncCommit` / `imagePackageApply` は `signature` を必須扱いにする。
- [推奨] `otaStart` も同等に署名保護する。
- [禁止] 検証失敗要求を「ログのみ」で継続実行しない。

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

### 3.2.1 I/O運用サブコマンド payload 詳細

#### a) `notice/trh` 温湿度通知
**トピック**: `esp32lab/notice/trh/<senderName>`

```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "IoT_F0D0F94EB580-20260311102000-00001",
    "ts": "2026-03-11T10:20:00.000Z",
    "op": "notice",
    "sub": "trh",
    "args": {
        "temperatureC": 23.4,
        "humidityRh": 58.2,
        "sensorId": "sht31-1"
    },
    "Res": "OK"
}
```

#### b) `get/trh` 温湿度取得要求
**トピック**: `esp32lab/get/trh/<receiverName>`

```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102005-00001",
    "ts": "2026-03-11T10:20:05.000Z",
    "op": "get",
    "sub": "trh",
    "args": {}
}
```

**応答例 (`notice/trh`)**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "server-001-20260311102005-00001",
    "ts": "2026-03-11T10:20:05.120Z",
    "op": "notice",
    "sub": "trh",
    "args": {
        "temperatureC": 23.5,
        "humidityRh": 58.0,
        "sensorId": "sht31-1"
    },
    "Res": "OK"
}
```

#### c) `set relay` リレー制御
**トピック**: `esp32lab/set/relay/<receiverName>`

```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102100-00001",
    "ts": "2026-03-11T10:21:00.000Z",
    "op": "set",
    "sub": "relay",
    "args": {
        "channel": 1,
        "state": "on"
    }
}
```

**応答例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "server-001-20260311102100-00001",
    "ts": "2026-03-11T10:21:00.050Z",
    "op": "set",
    "sub": "relay",
    "Res": "OK",
    "detail": "channel=1 state=on"
}
```

#### d) `get relay` リレー状態取得
**トピック**: `esp32lab/get/relay/<receiverName>`

```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102110-00001",
    "ts": "2026-03-11T10:21:10.000Z",
    "op": "get",
    "sub": "relay",
    "args": {
        "channel": 1
    }
}
```

**応答例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "server-001-20260311102110-00001",
    "ts": "2026-03-11T10:21:10.050Z",
    "op": "get",
    "sub": "relay",
    "Res": "OK",
    "detail": {
        "channel": 1,
        "state": "on"
    }
}
```

#### e) `set led_ON` / `set led_OFF` / `set led_Blink`
**トピック**:
- `esp32lab/set/led_ON/<receiverName>`
- `esp32lab/set/led_OFF/<receiverName>`
- `esp32lab/set/led_Blink/<receiverName>`

**`set/led_ON` 要求例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102200-00001",
    "ts": "2026-03-11T10:22:00.000Z",
    "op": "set",
    "sub": "led_ON",
    "args": {
        "index": 2
    }
}
```

**`set/led_Blink` 要求例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102205-00001",
    "ts": "2026-03-11T10:22:05.000Z",
    "op": "set",
    "sub": "led_Blink",
    "args": {
        "index": 2,
        "intervalMs": 500
    }
}
```

#### f) `get led` LED状態取得
**トピック**: `esp32lab/get/led/<receiverName>`

```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102210-00001",
    "ts": "2026-03-11T10:22:10.000Z",
    "op": "get",
    "sub": "led",
    "args": {
        "index": 2
    }
}
```

**応答例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "server-001-20260311102210-00001",
    "ts": "2026-03-11T10:22:10.050Z",
    "op": "get",
    "sub": "led",
    "Res": "OK",
    "detail": {
        "index": 2,
        "state": "blink",
        "intervalMs": 500
    }
}
```

#### g) `get button` ボタン状態取得
**トピック**: `esp32lab/get/button/<receiverName>`

```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102220-00001",
    "ts": "2026-03-11T10:22:20.000Z",
    "op": "get",
    "sub": "button",
    "args": {
        "index": 0
    }
}
```

**応答例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "server-001-20260311102220-00001",
    "ts": "2026-03-11T10:22:20.040Z",
    "op": "get",
    "sub": "button",
    "Res": "OK",
    "detail": {
        "index": 0,
        "state": "released"
    }
}
```

#### h) `set gpio_H` / `set gpio_L` / `get gpio`
**トピック**:
- `esp32lab/set/gpio_H/<receiverName>`
- `esp32lab/set/gpio_L/<receiverName>`
- `esp32lab/get/gpio/<receiverName>`

**`set/gpio_H` 要求例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102230-00001",
    "ts": "2026-03-11T10:22:30.000Z",
    "op": "set",
    "sub": "gpio_H",
    "args": {
        "index": 4
    }
}
```

**`get/gpio` 要求例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102235-00001",
    "ts": "2026-03-11T10:22:35.000Z",
    "op": "get",
    "sub": "gpio",
    "args": {
        "index": 4
    }
}
```

**応答例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "server-001-20260311102235-00001",
    "ts": "2026-03-11T10:22:35.030Z",
    "op": "get",
    "sub": "gpio",
    "Res": "OK",
    "detail": {
        "index": 4,
        "level": "high"
    }
}
```

- [厳守] `index` が未許可ポートの場合は `INVALID_ARGUMENT` を返し、操作しない。

#### i) `get log` ログ取得
**トピック**: `esp32lab/get/log/<receiverName>`

```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102240-00001",
    "ts": "2026-03-11T10:22:40.000Z",
    "op": "get",
    "sub": "log",
    "args": {
        "limit": 50
    }
}
```

**応答例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "server-001-20260311102240-00001",
    "ts": "2026-03-11T10:22:40.060Z",
    "op": "get",
    "sub": "log",
    "Res": "OK",
    "detail": {
        "count": 50,
        "summary": "latest 50 lines returned"
    }
}
```

- [推奨] MQTT 応答は要約とし、全文取得は HTTP API を使用する。

#### j) `call restart` 再起動命令
**トピック**: `esp32lab/call/restart/<receiverName>`

```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102250-00001",
    "ts": "2026-03-11T10:22:50.000Z",
    "op": "call",
    "sub": "restart",
    "args": {
        "delayMs": 500
    }
}
```

#### k) `call maintenance` メンテナンスモード遷移命令
**トピック**: `esp32lab/call/maintenance/<receiverName>`

```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311102300-00001",
    "ts": "2026-03-11T10:23:00.000Z",
    "op": "call",
    "sub": "maintenance",
    "args": {
        "reason": "remote-maintenance"
    }
}
```

- [厳守] `restart` / `maintenance` 実行前に可能な範囲で状態保存（NVS flush）を行う。
- [厳守] `maintenance` 実行時は再起動して AP モードへ遷移する。
- [厳守] 再起動後の AP 名は `AP-esp32lab-<MAC(no colon)>` とする。

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
        "mqttUrlName": "mqtt.example.com",
        "mqttUser": "user",
        "mqttPass": "pass",
        "mqttTls": true,
        "mqttPort": 8883,
        "keyDevice": "<base64-k-device>",
        "server": {
            "serverUrl": "api.example.com",
            "serverUrlName": "api.example.com",
            "serverUser": "apiUser",
            "serverPass": "apiPass",
            "serverPort": 443,
            "serverTls": true
        },
        "ota": {
            "otaUrl": "ota.example.com",
            "otaUrlName": "ota.example.com",
            "otaUser": "otaUser",
            "otaPass": "otaPass",
            "otaPort": 443,
            "otaTls": true
        },
        "timeServer": {
            "timeServerUrl": "ntp.example.com",
            "timeServerUrlName": "ntp.example.com",
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
| `mqttUrlName` | string | No | `mqttUrl` がIPの場合に併記する論理ホスト名。 |
| `mqttUser` | string | Yes | [厳守] MQTTユーザー名。空文字は禁止。 |
| `mqttPass` | string | Yes | [厳守] MQTTパスワード。空文字は禁止。 |
| `mqttTls` | bool | Yes | [厳守] TLSを使用するか (`true`/`false`)。運用時は `true` 固定。 |
| `mqttPort` | int | Yes | [厳守] MQTTポート番号。TLS時は `8883` を使用。 |
| `keyDevice` | string | No | Base64エンコード済み `k-device`。 |
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
- [将来対応] `mqttUrlName` / `serverUrlName` / `otaUrlName` / `timeServerUrlName` / `keyDevice` は `common.h` へ追加済み。実装側の読書き反映は別タスクで実施する。

### 3.3.1 Pairing / Re-Pairing 補足
- [重要] `k-device` 初回投入および `k-user` 再発行後の再ペアリングは、MQTT 経由ではなく AP モード + `runPairingSession()` を正規経路とする。
- [厳守] `createPairingBundle` は `runPairingSession()` 内部 helper とし、少なくとも `targetDeviceId` `sessionId` `keyVersion` `requestedSettings` を入力に含める。
- [厳守] `requestedSettings` の必須項目（Wi-Fi / MQTT / OTA / 認証情報）は TS 側で事前検証し、不足時は `SecretCore` を呼び出さない。
- [厳守] ESP32 は固定公開鍵で bundle 署名を検証し、AP モード接続時の ECDH による `k-pairing-session` で bundle を復号する。
- [厳守] TS へ raw `k-device`、ECDH 共有秘密、`k-pairing-session` を返さない。
- [厳守] ESP32 は `k-device`、Wi-Fi / MQTT / OTA / 各認証情報を NVS に保存し、current/previous の 2 スロットで管理する。
- [禁止] `POST /api/pairing/bundle` のような `createPairingBundle` 直公開 API を提供しない。

### 3.3.2 コマンドリクエスト詳細: fileSyncPlan
`LittleFS` の画像または証明書を差分更新するための計画通知を行う。

**トピック**: `esp32lab/call/fileSyncPlan/<receiverName>`

**Payload例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260309113000-00001",
    "ts": "2026-03-09T11:30:00.000Z",
    "op": "call",
    "sub": "fileSyncPlan",
    "args": {
        "sessionId": "filesync-20260309113000-001",
        "targetArea": "images",
        "basePath": "/images",
        "deleteMode": "listed",
        "files": [
            {
                "path": "/images/top/banner.png",
                "size": 10240,
                "sha256": "0123456789abcdef...",
                "action": "upsert"
            },
            {
                "path": "/images/old/logo.png",
                "size": 0,
                "sha256": "",
                "action": "delete"
            }
        ]
    }
}
```

| キー | 型 | 必須 | 説明 |
| :--- | :--- | :--- | :--- |
| `sessionId` | string | Yes | 更新セッション識別子。 |
| `targetArea` | string | Yes | `images` または `certs`。 |
| `basePath` | string | Yes | `LittleFS` 上の基準パス。 |
| `deleteMode` | string | Yes | `none` / `listed` / `all`。 |
| `files` | array | Yes | 更新候補一覧。 |
| `files[].path` | string | Yes | 更新対象パス。 |
| `files[].size` | int | Yes | ファイルサイズ。 |
| `files[].sha256` | string | Yes | [厳守] 事前計算済み `SHA-256`。 |
| `files[].action` | string | Yes | `upsert` または `delete`。 |

**備考**:
- [厳守] `targetArea=images` の主保存先は `/images`、`targetArea=certs` の主保存先は `/certs` とする。
- [厳守] ESP32 は既存ファイルの `SHA-256` を比較し、転送不要ファイルを再受信しない。
- [厳守] `/logs` は `fileSyncPlan` の更新対象に含めない。
- [厳守] 画像更新系コマンド（`fileSyncPlan` / `fileSyncChunk` / `fileSyncCommit`）は、OTA と同等に HMAC または署名検証を必須とする。  
  理由: 未検証コマンドで画像差し替えが可能だと、UI 偽装や悪性誘導による乗っ取りリスクがあるため。

### 3.3.3 コマンドリクエスト詳細: fileSyncChunk
`fileSyncPlan` で必要と判定されたファイルだけをチャンク転送する。

**トピック**: `esp32lab/call/fileSyncChunk/<receiverName>`

**Payload例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260309113005-00001",
    "ts": "2026-03-09T11:30:05.000Z",
    "op": "call",
    "sub": "fileSyncChunk",
    "args": {
        "sessionId": "filesync-20260309113000-001",
        "targetArea": "images",
        "path": "/images/top/banner.png",
        "chunkIndex": 0,
        "chunkCount": 3,
        "dataBase64": "<base64>",
        "isLast": false
    }
}
```

- [厳守] ESP32 は `tmp` ファイルへ追記し、最終チャンク受信後に `SHA-256` を再計算する。
- [禁止] `SHA-256` 一致前に本番ファイルへ切り替えない。
- [厳守] `chunkIndex` は `0` 始まり連番、`chunkCount` は総チャンク数とし、欠番がある場合は `NG` 応答する。
- [厳守] `isLast=true` は `chunkIndex == chunkCount - 1` の場合のみ許可する。

### 3.3.4 コマンドリクエスト詳細: fileSyncCommit
検証成功ファイルの反映と削除適用を要求する。

**トピック**: `esp32lab/call/fileSyncCommit/<receiverName>`

**Payload例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260309113100-00001",
    "ts": "2026-03-09T11:31:00.000Z",
    "op": "call",
    "sub": "fileSyncCommit",
    "args": {
        "sessionId": "filesync-20260309113000-001",
        "targetArea": "images",
        "deleteMode": "listed"
    }
}
```

- [厳守] `deleteMode=listed` は manifest で `action=delete` 指定されたもののみ削除する。
- [厳守] `deleteMode=all` は `targetArea` 配下の対象ファイルのみ全削除する。
- [厳守] `deleteMode=all` でも `/logs` は削除しない。
- [厳守] 失敗時は旧ファイル維持とする。

### 3.3.5 通知詳細: fileSyncStatus
`fileSync` 実行状況を通知する。

**トピック**: `esp32lab/notice/fileSyncStatus/<senderName>`

**Payload例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "IoT_F0D0F94EB580-20260309113105-00001",
    "ts": "2026-03-09T11:31:05.000Z",
    "op": "notice",
    "sub": "fileSyncStatus",
    "sessionId": "filesync-20260309113000-001",
    "targetArea": "images",
    "phase": "verifying",
    "result": "OK",
    "detail": "banner.png sha256 matched"
}
```

- [厳守] `phase` は少なくとも `planning` / `receiving` / `verifying` / `applying` / `completed` / `failed` を許容する。
- [厳守] `targetArea=certs` の更新完了時は、次回接続前に `NVS` 側の証明書メタ情報も更新する。
- [重要] `failed` 時は `errorCode` を含める。

**`fileSyncStatus.errorCode` 正規値**:
- `FSYNC_SIG_INVALID`: 署名検証失敗
- `FSYNC_SESSION_MISMATCH`: `sessionId` 不一致
- `FSYNC_CHUNK_MISSING`: チャンク欠番
- `FSYNC_SHA_MISMATCH`: `SHA-256` 不一致
- `FSYNC_IO_ERROR`: 書込み/rename失敗
- `FSYNC_SCOPE_VIOLATION`: 更新対象外パス指定

### 3.3.6 コマンドリクエスト詳細: imagePackageApply（将来対応）
複数画像をZIP化したパッケージを、MQTTトリガで HTTPS 取得・展開して反映する。

**トピック**: `esp32lab/call/imagePackageApply/<receiverName>`

**Payload例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260310160000-00001",
    "ts": "2026-03-10T16:00:00.000Z",
    "op": "call",
    "sub": "imagePackageApply",
    "args": {
        "sessionId": "imgpkg-20260310160000-001",
        "packageUrl": "https://mqtt.esplab.home.arpa:4443/assets/images-pack.zip",
        "packageSha256": "0123456789abcdef...",
        "destinationDir": "/images/top",
        "overwrite": true
    }
}
```

- [厳守] `imagePackageApply` は MQTT メッセージの HMAC / 署名検証に成功した場合のみ実行する。
- [厳守] ZIP取得は HTTPS/TLS 証明書検証を必須とし、`packageSha256` 一致時のみ展開する。
- [厳守] `destinationDir` の配下へ展開し、同名ファイルは `overwrite=true` の場合のみ上書きする。
- [禁止] `destinationDir` で `/logs` を指定できないようにし、ログ領域の上書き・削除を防止する。
- [推奨] フローは OTA と同様に `download -> verify -> stage(tmp) -> apply(rename)` を採用する。

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

**`otaStart` 追加ルール**:
- [厳守] `manifestUrl` / `firmwareUrl` は `https://` のみ許可する。
- [厳守] `sha256` は16進64桁固定とし、形式不正は即時 `NG` とする。
- [厳守] `firmwareVersion` が現在版と同一か旧版の場合、既定は拒否し、明示フラグなしでダウングレードしない。

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

### 3.8 MQTTコマンド共通エラーコード
[重要] 失敗解析の再現性を高めるため、`result=NG` 時は以下の `errorCode` を優先使用する。

| `errorCode` | 意味 | 主な対象 |
| :--- | :--- | :--- |
| `AUTH_REQUIRED` | 認証不足 | AP連携、高権限操作 |
| `SIG_INVALID` | HMAC/署名検証失敗 | OTA / fileSync |
| `NONCE_REPLAY` | nonce再利用検知 | OTA / fileSync |
| `EXPIRED_REQUEST` | 要求期限切れ | OTA / fileSync |
| `INVALID_ARGUMENT` | 引数不正 | 全コマンド |
| `UNSUPPORTED_SUB` | 未対応 `sub` | 全コマンド |
| `BUSY_RETRY_LATER` | 実行中で受付不可 | OTA / fileSync |
| `TLS_REQUIRED` | TLS必須条件違反 | network / 接続 |
| `NVS_WRITE_FAILED` | NVS保存失敗 | network / pairing |
| `FS_IO_FAILED` | ファイルI/O失敗 | fileSync / imagePackageApply |

### 3.7 コマンドリクエスト詳細: rollbackTestEnable / rollbackTestDisable
7025 の `未確定起動失敗` を再現するための試験専用コマンド。

**トピック**:
- `esp32lab/call/rollbackTestEnable/<receiverName>`
- `esp32lab/call/rollbackTestDisable/<receiverName>`

**Payload例（有効化）**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "local-server-001",
    "id": "local-server-001-20260310100000-00001",
    "ts": "2026-03-10T10:00:00.000Z",
    "op": "call",
    "sub": "rollbackTestEnable",
    "args": {
        "requestType": "rollbackTest",
        "mode": "enable"
    }
}
```

**Payload例（無効化）**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "local-server-001",
    "id": "local-server-001-20260310103000-00001",
    "ts": "2026-03-10T10:30:00.000Z",
    "op": "call",
    "sub": "rollbackTestDisable",
    "args": {
        "requestType": "rollbackTest",
        "mode": "disable"
    }
}
```

- [重要] `rollbackTestEnable` は次回OTA後の初回起動で rollback を強制再現するための試験専用機能。
- [厳守] 試験完了後は必ず `rollbackTestDisable` を実行する。
- [禁止] 通常運用で本コマンドを常用しない。

## 4. ステータス・Will

### 4.1 Status (notice/...)
デバイス起動時や定期的、または状態変化時に送信する。

- **起動通知**: 電源ON時に `op`: `status`, `sub`: `start-up` 等で通知。
  - [重要] 7015/7025試験のA/Bパーティション切替確認のため、一時的に `runningPartition`, `bootPartition`, `nextUpdatePartition` を付加してよい。
  - [廃止の方針] これらの一時項目は試験完了後に `status` 通知から削除する。
- **切断通知**: LWT (Last Will and Testament) を利用し、切断時にサーバーへ通知されるよう設定する。
- **detail運用**: [重要] `detail` には通知理由を設定する。現在の正規化値は `StartUp` / `button` / `Reply` / `Restart(Button)` / `Restart(abort)` / `Restart(Call)`。
- [仕様変更] `public_id` の初期値は `IoT_<macアドレスからコロン除去>` を許容する。
- [重要] ファイルログはシリアル出力とは別経路で管理し、必要時のみ ON にする。
- [重要] ファイルログは時刻同期済み時 `/logs/<年>/<月日>/<年月日時分秒ms-連番>.log`、未同期時 `/logs/<起動回数>/00001.log` を使用する。
- [重要] 将来の冗長構成では、同一 `Broker` を複数 `Server` が subscribe して同じ `status` / テレメトリを共有してよい。
- [厳守] OTA や高リスク要求は要求元 `Server` の情報に対してのみ応答する。
- [厳守] `Server` と `MQTT Broker` は別責務として扱い、必要に応じて `serverId` / `brokerId` をメタ情報で管理する。

**`call/status` 要求例**:
```json
{
    "v": 1,
    "DstID": "IoT_F0D0F94EB580",
    "SrcID": "server-001",
    "id": "server-001-20260311110000-00001",
    "ts": "2026-03-11T11:00:00.000Z",
    "op": "call",
    "sub": "status",
    "args": {}
}
```

**`notice/status` 応答例**:
```json
{
    "v": 1,
    "DstID": "server-001",
    "SrcID": "IoT_F0D0F94EB580",
    "id": "server-001-20260311110000-00001",
    "ts": "2026-03-11T11:00:00.080Z",
    "op": "notice",
    "sub": "status",
    "Res": "OK",
    "detail": "Reply",
    "args": {
        "fwVersion": "1.2.3",
        "networkState": "running",
        "uptimeSeconds": 3600
    }
}
```

### 4.2 Will (遺言)
- トピック: `esp32lab/notice/status/<senderName>`
- ペイロード: `{"op": "status", "result": "disconnect", ...}` 相当のJSON。
- デバイス接続時にMQTTブローカへ登録しておくこと。

## 5. 変更履歴
- 2026-03-11: QoS方針を更新し、コマンド系 publish を QoS1 厳守へ明記。理由: 再送保証を有効化し、指令取りこぼしの運用リスクを下げるため。
- 2026-03-11: `trh` / `relay` / `led` / `button` / `gpio` / `log` / `restart` / `maintenance` と `call/status` / `notice/status` / `rollbackTestDisable` の JSON 見本（Request/Response）を追加。理由: 全サブコマンドで見本を揃え、実装者間の解釈差を無くすため。
- 2026-03-11: I/O運用サブコマンド（`trh`、`relay`、`led_ON`/`led_OFF`/`led_Blink`、`button`、`gpio_H`/`gpio_L`/`gpio`、`log`、`restart`、`maintenance`）と payload 詳細を追加。理由: 現場制御で必要な基本操作を MQTT 仕様として固定するため。
- 2026-03-11: サブコマンド一覧（2.3）、セキュリティ拡張ヘッダ（3.1.1）、`fileSync` 異常コード、MQTT共通エラーコードを追加。理由: 実装前に `sub` / payload / エラー応答の揺れを抑え、接続試験と障害解析を容易にするため。
- 2026-03-10: 4-5 強化として、画像更新系 `fileSync` コマンドへ HMAC/署名必須を追記し、`imagePackageApply`（ZIP一括更新）の将来対応仕様を追加。理由: OTA同等の真正性検証を画像更新にも適用し、改ざん更新リスクを抑えるため。
- 2026-03-10: network payload に `mqttUrlName` / `serverUrlName` / `otaUrlName` / `timeServerUrlName` / `keyDevice` を追記。理由: `common.h` の追加キーを仕様書へ先行反映し、実装追従待ち項目を明確化するため。
- 2026-03-10: `rollbackTestEnable` / `rollbackTestDisable` を追加。理由: 7025 の `未確定起動失敗` を実機で再現するため。
- 2026-03-10: `status` / テレメトリの複数 `Server` 共有、要求元 `Server` に対する OTA 応答、`serverId` / `brokerId` の分離方針を追加。理由: 冗長化と多重接続の前提を MQTT 詳細仕様へ反映するため。
- 2026-03-09: 7015 / 7025 確認用として `status` 通知へ `runningPartition` / `bootPartition` / `nextUpdatePartition` を一時追加し、試験完了後に廃止する方針を追加。理由: A/B 切替の確認を確実に行うため。
- 2026-03-09: `fileSyncPlan` / `fileSyncChunk` / `fileSyncCommit` / `fileSyncStatus` と、`/images` `/certs` `/logs` の運用補足を追加。理由: 画像一斉更新、証明書更新、ログ保持方針を MQTT 仕様へ反映するため。
- 2026-03-09: Pairing / Re-Pairing 補足を `runPairingSession()` 正規経路と `createPairingBundle` 内部 helper 前提へ更新。理由: MQTT 仕様文書でも公開 workflow と内部秘密処理の境界を他文書と一致させるため。
- 2026-03-09: [仕様変更] `public_id` の初期値を `IoT_<macアドレスからコロン除去>` とする前提へ更新。理由: 運用上わかりやすさを優先し、初期導入時の識別を容易にするため。
- 2026-03-09: `createPairingBundle`、AP モード ECDH、固定公開鍵検証、ESP32 NVS current/previous 2 スロット運用の補足を追加。理由: MQTT 仕様から見た pairing と識別子運用の前提を明確化するため。
- 2026-03-07: `otaStart` 指令ペイロードと `otaProgress` 通知仕様（進捗/再試行/SHA256）を追加。
- 2026-03-05: `network` コマンド引数に `server` / `ota` / `timeServer` 設定を追加。理由: `sensitiveData.json` の管理項目拡張とプロトコル定義を同期するため。
- 2026-03-01: 初版作成。`common.h` と定義を同期。
