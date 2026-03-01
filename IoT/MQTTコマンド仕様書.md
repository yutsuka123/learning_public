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
| **コマンド** | `cmd/esp32lab/<deviceId>/<commandName>` | サーバー → デバイス（要求） |
| **応答** | `res/esp32lab/<deviceId>/<commandName>` | デバイス → サーバー（結果） |
| **通知** | `notice/esp32lab/<deviceId>/<eventName>` | 双方（イベント通知） |

- `<deviceId>`: デバイス固有ID（MACアドレス等）。`all` を指定した場合は全デバイス対象（ブロードキャスト）。
- `<commandName>` / `<eventName>`: 操作またはイベント名。`op` フィールドと一致させること。

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
| デバイスID | `deviceId` | string | Yes | 発信元のID。 |
| MACアドレス | `macAddr` | string | Yes | デバイスのBase MACアドレス（変更不可の固有ID）。 |
| メッセージID | `id` | string | Yes | `<deviceId>-<timestamp>-<seq>` 形式を推奨。Req/Res紐付け用。 |
| タイムスタンプ | `ts` | string | Rec | ISO8601拡張形式（UTC）等を推奨。デバッグ・運用ログ用。 |
| 操作名 | `op` | string | Yes | トピックの `<commandName>` と一致させる。 |

### 3.2 コマンドリクエスト (cmd/...)
サーバーからデバイスへの要求。

```json
{
    "v": 1,
    "deviceId": "server-001",
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

### 3.3 コマンドリクエスト詳細: network
ネットワーク設定およびMQTT接続設定の変更を行う。

**トピック**: `cmd/esp32lab/<deviceId>/network`

**Payload**:
```json
{
    "v": 1,
    "deviceId": "server-001",
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
| `mqttUser` | string | No | MQTTユーザー名。 |
| `mqttPass` | string | No | MQTTパスワード。 |
| `mqttTls` | bool | No | TLSを使用するか (`true`/`false`)。 |
| `mqttPort` | int | No | MQTTポート番号。 |
| `apply` | bool | Yes | `true`: 即時反映を試みる, `false`: 再起動後に反映。 |
| `reboot` | bool | Yes | `true`: コマンド処理後に再起動する。 |

**備考**:
- パスワード等の機密情報が含まれるため、通信経路の暗号化（MQTTS）が必須である。
- Wi-Fi設定変更時は、接続断が発生する可能性がある。`apply: false` + `reboot: true` の組み合わせを推奨する。

### 3.4 コマンドレスポンス (res/...)
デバイスからサーバーへの応答。

```json
{
    "v": 1,
    "deviceId": "device-001",
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

## 4. ステータス・Will

### 4.1 Status (notice/...)
デバイス起動時や定期的、または状態変化時に送信する。

- **起動通知**: 電源ON時に `op`: `status`, `sub`: `start-up` 等で通知。
- **切断通知**: LWT (Last Will and Testament) を利用し、切断時にサーバーへ通知されるよう設定する。

### 4.2 Will (遺言)
- トピック: `notice/esp32lab/<deviceId>/status`
- ペイロード: `{"op": "status", "result": "disconnect", ...}` 相当のJSON。
- デバイス接続時にMQTTブローカへ登録しておくこと。

## 5. 変更履歴
- 2026-03-01: 初版作成。`common.h` と定義を同期。
