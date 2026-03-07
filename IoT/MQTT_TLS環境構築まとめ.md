# MQTT TLS 環境構築まとめ（ESP32 + Mosquitto + Node）

[重要] 本書は、ESP32 / Mosquitto / Node(TypeScript) の MQTT over TLS 構成を再現するための実装・運用メモです。  
理由: 開発者交代時でも同一構成を短時間で再現できるようにするため。

[厳守] 本書に記載するユーザー名・パスワード・鍵素材は **実値を直接記載しない**。  
理由: ドキュメント経由の機密漏えいを防ぐため。

[禁止] `ca.key` / `server.key` をESP32へ格納しない。  
理由: 端末側は公開情報（CA証明書）だけでサーバー正当性を検証する設計とするため。

[将来対応] Mosquitto から AWS IoT Core へ段階移行できるよう、トピック設計を先に共通化しておく。

## 1. 全体構成

```text
ESP32
   ↓ MQTT over TLS
Mosquitto Broker (Windows PC)
   ↓
Node / TypeScript クライアント
   ↓
将来 AWS IoT へ移行
```

## 2. Mosquitto サーバー設定（Windows）

### 2.1 インストール
- 公式: <https://mosquitto.org/download/>
- 想定インストール先: `C:\Program Files\Mosquitto`

### 2.2 データディレクトリ
- `C:\mosquitto-data` を作成

### 2.3 配置ファイル
- `ca.crt`
- `ca.key`
- `server.crt`
- `server.key`
- `passwd`
- `mosquitto.log`

### 2.4 `mosquitto.conf` 例
配置先: `C:\Program Files\Mosquitto\mosquitto.conf`

```conf
listener 8883

cafile C:\mosquitto-data\ca.crt
certfile C:\mosquitto-data\server.crt
keyfile C:\mosquitto-data\server.key

password_file C:\mosquitto-data\passwd
allow_anonymous false

log_dest file C:\mosquitto-data\mosquitto.log
log_type all
```

### 2.5 サービス運用
- 登録: `mosquitto install`
- 起動: `net start mosquitto`
- 状態確認: `Get-Service mosquitto`

### 2.6 手動起動（検証用）
[推奨] 設定切替の試験時はサービス停止のうえ手動起動でログ確認する。

1. サービス停止（必要時）
```powershell
net stop mosquitto
```

2. Mosquittoフォルダへ移動
```powershell
cd "C:\Program Files\Mosquitto"
```

3. テスト用設定で起動（今回の検証コマンド）
```powershell
.\mosquitto.exe -c .\mosquitto-test.conf -v
```

[重要] `-v` で接続失敗理由（証明書・認証・ポート）を即確認できる。  
理由: ESP32側ログだけでは切り分けに時間がかかるため。

## 3. 認証ユーザー作成

```powershell
cd C:\mosquitto-data
mosquitto_passwd -c passwd <MQTT_USERNAME>
```

[推奨] 例として `esp32lab_mqtt` を使用可能。  
※ 実パスワードは本書に記載しないこと。

## 4. 証明書作成（OpenSSL）

作業ディレクトリ: `C:\mosquitto-data`

### 4.1 CA作成
```bash
openssl genrsa -out ca.key 2048
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt
```

### 4.2 サーバー鍵/CSR
```bash
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr
```

Common Name(CN): `<パソコン名>`（PC hostname と一致させる）

### 4.3 サーバー証明書作成
```bash
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 365
```

### 4.4 SAN付きで server.crt を再発行する（Git Bash 例）
[重要] ESP32のTLS検証差異を吸収するため、`IP` と `DNS` の両方をSANへ入れる。  
理由: PCクライアントとESP32で照合方式が異なる場合でも接続しやすくするため。

```bash
cd /c/mosquitto-data

cat > server.ext <<'EOF'
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=@alt_names

[alt_names]
DNS.1=mqtt.esplab.home.arpa
DNS.2=localhost
DNS.3=CF-FV_1
IP.1=172.16.1.59
EOF

openssl genrsa -out server.key 2048

MSYS_NO_PATHCONV=1 openssl req -new -key server.key -out server.csr -subj "/C=JP/ST=Tokyo/L=Higashiyamato/O=esplab/OU=mqtt/CN=mqtt.esplab.home.arpa"

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 365 -sha256 -extfile server.ext
```

[厳守] 再発行後は `mosquitto-test.conf` / `mosquitto.conf` で参照している `server.crt` を差し替え、Mosquittoを再起動する。

### 4.5 SAN再発行 実行コマンド（PowerShell例 / 2026-03-07）
[重要] `openssl` がPATHに無いWindows環境では `C:\Program Files\Git\usr\bin\openssl.exe` を明示指定して実行する。  
理由: 手順通りでも `openssl is not recognized` で作業が止まるため。

```powershell
Copy-Item "C:\mosquitto-data\server.crt" "C:\mosquitto-data\server.crt.pre_san_fix" -Force
Copy-Item "C:\mosquitto-data\server.csr" "C:\mosquitto-data\server.csr.pre_san_fix" -Force

@'
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=@alt_names

[alt_names]
DNS.1=mqtt.esplab.home.arpa
DNS.2=localhost
DNS.3=CF-FV_1
IP.1=172.16.1.59
'@ | Set-Content -Path "C:\mosquitto-data\server.ext" -Encoding ascii

& "C:\Program Files\Git\usr\bin\openssl.exe" req -new `
  -key "C:\mosquitto-data\server.key" `
  -out "C:\mosquitto-data\server.csr" `
  -subj "/C=JP/ST=Tokyo/L=Higashiyamato/O=esplab/OU=mqtt/CN=mqtt.esplab.home.arpa"

& "C:\Program Files\Git\usr\bin\openssl.exe" x509 -req `
  -in "C:\mosquitto-data\server.csr" `
  -CA "C:\mosquitto-data\ca.crt" `
  -CAkey "C:\mosquitto-data\ca.key" `
  -CAcreateserial `
  -out "C:\mosquitto-data\server.crt" `
  -days 3650 `
  -sha256 `
  -extfile "C:\mosquitto-data\server.ext"

certutil -dump "C:\mosquitto-data\server.crt"
```

[厳守] `certutil -dump` の出力に `Subject Alternative Name` として  
`DNS Name=mqtt.esplab.home.arpa` と `IP Address=172.16.1.59` が両方あることを確認する。  
理由: DNS解決時・フォールバックIP時のどちらでもESP32のTLS検証を通すため。

[重要] `mosquitto` サービス再起動（`sc stop/start mosquitto`）は管理者権限が必要。  
理由: 標準権限では `OpenService FAILED 5 (Access is denied)` となり、再発行済み証明書が反映されないため。

## 5. Mosquitto TLS 接続テスト

### 5.1 subscribe
```bash
mosquitto_sub -h localhost -p 8883 --cafile C:\mosquitto-data\ca.crt -u <MQTT_USERNAME> -P <MQTT_PASSWORD> -t test
```

### 5.2 publish
```bash
mosquitto_pub -h localhost -p 8883 --cafile C:\mosquitto-data\ca.crt -u <MQTT_USERNAME> -P <MQTT_PASSWORD> -t test -m hello
```

成功例:
```text
hello
```

## 6. ESP32 クライアント仕様

### 6.1 通信条件
- WiFi: STA mode
- MQTT Port: `8883`
- TLS: 有効
- username/password: 使用（必須）
- [厳守] payloadは `K_device` によるアプリ層暗号化/復号を必須とする
- MQTT topic: `test`
- Publish message: `hello from esp32 tls`
- Serial baudrate: `115200`

### 6.2 接続先
- `MQTT_HOST = "<パソコン名>"`
- `MQTT_PORT = 8883`

### 6.3 使用ライブラリ
- `WiFiClientSecure`
- `PubSubClient`

### 6.4 ESP32に埋め込む証明書
- `ca.crt`（CA公開証明書）

[禁止] 以下はESP32へ配置しない:
- `server.key`
- `ca.key`

### 6.5 必要機能
1. WiFi接続
2. TLS MQTT接続
3. username/password 認証
4. topic subscribe
5. topic publish
6. 10秒周期publish
7. 受信メッセージをSerial表示
8. 再接続処理

## 7. Node / TypeScript クライアント

### 7.1 ライブラリ
- `mqtt`

```bash
npm install mqtt
```

### 7.2 接続コード例
```typescript
import mqtt from "mqtt";
import fs from "fs";

const client = mqtt.connect("mqtts://<パソコン名>:8883", {
  username: process.env.MQTT_USERNAME,
  password: process.env.MQTT_PASSWORD,
  ca: fs.readFileSync("C:/mosquitto-data/ca.crt"),
});

client.on("connect", () => {
  console.log("connected");
  client.subscribe("test");
  client.publish("test", "hello from node");
});

client.on("message", (topic, msg) => {
  console.log(topic, msg.toString());
});
```

[推奨] 資格情報は `.env` またはOS環境変数で注入する。  
理由: ソースコード直書きによる漏えい防止。

## 8. MQTT.fx 設定例
- Broker address: `<パソコン名>`
- Port: `8883`
- SSL/TLS: `ON`
- Protocol: `TLSv1.2`
- CA file: `C:\mosquitto-data\ca.crt`
- User name: `<MQTT_USERNAME>`
- Password: `<MQTT_PASSWORD>`

## 9. TLS 接続の要点

[重要] 証明書検証は **CNよりSAN優先** で評価される。  
理由: 現行TLS実装ではSAN未設定時に検証失敗しやすいため。

運用パターンA（DNS名運用）:
- 接続先: `mqtts://<パソコン名>:8883` などDNS名
- サーバ証明書SAN: `DNS:<パソコン名>`

運用パターンB（固定IP運用 / DNS不要）:
- 接続先: `172.16.1.59`
- サーバ証明書SAN:
  - [厳守] `IP:172.16.1.59`
  - [推奨] `DNS:172.16.1.59`
  - [任意] `DNS:CF-FV_1`
  - [任意] `DNS:localhost`
- 理由: PC側クライアントがIPとして検証するケースと、ESP32側が文字列ホスト名として評価するケースの両方に対応しやすくするため。

[重要] ESP32で `localhost` は利用できない。  
理由: ESP32から見た `localhost` はESP32自身を指すため。

## 10. 今後の拡張

### 10.1 Node Backend
- `ESP32 -> Mosquitto -> Node -> DB`

### 10.2 AWS移行
- 将来 `Mosquitto -> AWS IoT Core` へ段階移行

### 10.3 トピック設計（推奨）
- `device/esp32/status`
- `device/esp32/data`
- `device/esp32/cmd`

### 10.4 CoreDNSによるローカルDNS運用（将来対応）
[重要] CoreDNSを導入しても、上流DNSへ`forward`する構成にすればインターネットDNSとローカルDNSは両立できる。  
理由: ローカルゾーンだけCoreDNSが回答し、それ以外は上流DNSに転送するため。

[厳守] CoreDNSで管理するのはローカル専用ゾーンのみとし、公開ドメインの上書きは行わない。  
理由: 外部サービスの名前解決失敗や誤接続を防ぐため。

[推奨] `.local` はmDNSと衝突しやすいため、ローカル専用ゾーンは `home.arpa` 等を利用する。

## 11. トラブルポイント

よくある原因:
- 証明書CNと接続ホスト不一致
- ESP32で `localhost` を使用
- TLS証明書未設定
- `1883` / `8883` の混同

### 11.1 今回の検証経緯（記録）
1. `mqttUrl=<パソコン名>` で接続試験  
   - 症状: `DNS Failed for <パソコン名>`  
   - 原因: ESP32側DNS解決が通らない環境
2. `mqttUrl=172.16.1.59` へ変更（DNS不要化）  
   - 症状: `(-9984) X509 - Certificate verification failed`
   - [重要] PCクライアント（mqtt.fx / mosquitto_pub/sub）は接続できる一方で、ESP32のみ失敗する事象を確認
3. `ca.crt` / `server.crt` を更新しSANを調整  
   - 目的: ESP32のTLS検証条件（接続先IP/ホスト名一致）を満たす
   - 対策詳細: `IP:172.16.1.59` に加えて `DNS:172.16.1.59` も付与
4. PCクライアント（mqtt.fx / mosquitto_pub）で疎通確認後、ESP32で再試行
5. 方針確定: TLSだけでなく `K_device` によるpayload暗号化/復号を実装必須とした。

### 11.2 `(-9984) X509` 解消の最終記録（2026-03-07）
[重要] `server.crt` のSAN再発行（`DNS:mqtt.esplab.home.arpa` + `IP:172.16.1.59`）と、ESP32側のTLS検証文脈修正により、`(-9984) X509` は解消した。  
理由: DNS失敗時にフォールバックIPで接続しても、証明書照合ホスト名を `mqtt.esplab.home.arpa` として検証できるようにしたため。

- サーバ側確認:
  - `openssl s_client -connect 172.16.1.59:8883 -servername mqtt.esplab.home.arpa` で提示証明書のSANに `DNS:mqtt.esplab.home.arpa` と `IP:172.16.1.59` を確認。
  - `-CAfile` に `ESP32/src/MQTT/ca.crt` を指定した検証で `Verify return code: 0 (ok)` を確認。
- ESP32側確認:
  - `hostByName failed`（DNS未解決）は継続するが、フォールバックIP経由で `connectToMqttBroker success. state=0` を確認。
  - `mqtt init done` を確認し、起動時のMQTT初期化フロー完了を確認。
- [厳守] 暫定対応としての `SENSITIVE_MQTT_FALLBACK_IP` は新ルータ導入後に廃止する。  
  理由: 本来はDNS名運用（FQDN）を第一経路とし、フォールバック依存を残さないため。

### 11.3 新ルータ導入までの暫定運用ポリシー（1週間）
[重要] 新ルータ導入予定日（目安: 2026-03-14）までは、現行LANでの暫定運用を継続する。  
理由: 移行前に接続安定性を維持し、実装・試験を止めないため。

- [厳守] MQTT接続失敗時の切り分け順序を固定する。  
  1) Wi-Fi接続  
  2) NTP同期  
  3) DNS解決  
  4) TLS検証  
  5) MQTT認証
- [推奨] 1日1回、`mosquitto_pub/sub` とESP32ログをセットで保存し、再発を早期検知する。
- [将来対応] 新ルータ導入後は `mqtt.esplab.home.arpa -> 172.17.1.10` 固定運用へ移行し、フォールバックIPを停止する。

[推奨] PC側CLI (`mosquitto_pub/sub`) とESP32ログをセットで確認する。  
理由: ネットワーク/DNS/証明書/認証のどこで失敗しているかを早く特定できるため。

## 12. 変更履歴
- 2026-03-06: 新規作成。理由: MQTT over TLS 構成の再現手順と運用上の注意点を一元化するため。
- 2026-03-06: 検証用起動コマンド（`.\mosquitto.exe -c .\mosquitto-test.conf -v`）と、DNS失敗→IP運用→証明書調整の経緯を追記。
- 2026-03-06: `K_device` によるpayload暗号化/復号を必須方針として通信条件と検証経緯へ追記。
- 2026-03-06: CoreDNSの将来対応方針（外部DNSとの両立、公開ドメイン上書き禁止、ローカルゾーン設計）を追記。
- 2026-03-07: SAN再発行手順を更新し、`DNS:mqtt.esplab.home.arpa` と `IP:172.16.1.59` を同時に含める具体コマンド（PowerShell/Git同梱OpenSSL）を追記。
- 2026-03-07: `(-9984) X509` 解消の最終記録（サーバ証明書検証OK、ESP32接続成功ログ）と、新ルータ導入まで1週間の暫定運用ポリシーを追記。

