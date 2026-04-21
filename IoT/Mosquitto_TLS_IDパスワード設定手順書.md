# IoT Mosquitto TLS + ID/パスワード設定手順書

[重要] 本書は、`Mosquitto` を `TLS` + `ID/パスワード` で起動し、ESP32 と LocalServer から安全に接続するための最小手順をまとめる。  
理由: 設定の要点を分離し、証明書・認証・接続試験のどこで失敗したかを追いやすくするため。

[厳守] 本書には実パスワード、秘密鍵、`ca.key`、`server.key` の内容を記載しない。  
理由: 文書経由の機密漏えいを防ぐため。

[厳守] ESP32 側には CA 証明書のみを配布し、サーバー秘密鍵は絶対に配布しない。  
理由: 接続端末に秘密鍵を置くと、なりすまし防止の前提が崩れるため。

[禁止] `1883` の平文運用を本手順の正規運用にしない。  
理由: 本章の目的は TLS と認証の両立を固定することだから。

[推奨] 参照順は `ネットワーク運用仕様書.md` -> `MQTT_TLS環境構築まとめ.md` -> 本書の順とする。  
理由: まず運用方針を確認し、その後に個別手順へ降りると迷いにくいため。

## 1. 対象範囲
- Windows 上の Mosquitto Broker
- MQTT over TLS 受信ポート `8883`
- `mqtt.esplab.home.arpa` を使う ESP32 / LocalServer 接続

## 2. 前提条件
- Mosquitto がインストール済みであること
- `mosquitto_sub` / `mosquitto_pub` が利用できること
- `OpenSSL` が利用できること
- 証明書配布先の CA が ESP32 側へ同期済みであること

## 3. ディレクトリ作成
1. 作業ディレクトリを作成する。

```powershell
New-Item -ItemType Directory -Force "C:\mosquitto-data"
```

2. 証明書、パスワードファイル、ログの保存先をこの配下に集約する。

[重要] 保存先を固定すると、証明書差し替え時の参照漏れを減らせる。  
理由: `mosquitto.conf` と実体の場所がずれると、起動は成功しても認証が失敗しやすいため。

## 4. TLS 証明書の準備
### 4.1 CA を作成する
```bash
openssl genrsa -out ca.key 2048
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt
```

### 4.2 サーバー鍵を作成する
```bash
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr
```

### 4.3 SAN 付き証明書へ再発行する
[重要] SAN には少なくとも正規ホスト名と実運用 IP を入れる。  
理由: ESP32 側の TLS 検証と、PC クライアントの接続経路差異を吸収するため。

```bash
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
IP.1=172.17.1.100
EOF

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 3650 -sha256 -extfile server.ext
```

[厳守] 実運用 IP が変わった場合は `IP.1` を更新する。  
理由: `IP` と `DNS` の両方で証明書検証を通す必要があるため。

## 5. 認証ユーザーを作成する
1. 認証用ユーザーを作成する。

```powershell
cd C:\mosquitto-data
mosquitto_passwd -c passwd <MQTT_USERNAME>
```

2. パスワードは本書ではなく、別の安全な管理先で扱う。

[禁止] テスト用でも平文パスワードをファイルに残しっぱなしにしない。  
理由: 認証設定の確認後に削除し忘れると、ローカル機でも漏えい経路になるため。

## 6. `mosquitto.conf` を設定する
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

[重要] `listener 8883`、`password_file`、`allow_anonymous false` の 3 点は必須とする。  
理由: TLS のみでは認証が通らず、認証のみでは通信路保護が足りないため。

## 7. 起動と確認
### 7.1 サービス起動
```powershell
net start mosquitto
```

### 7.2 状態確認
```powershell
Get-Service mosquitto
```

### 7.3 ログ確認
```powershell
Get-Content C:\mosquitto-data\mosquitto.log -Wait
```

[推奨] 設定変更の確認時はサービス起動だけで済ませず、手動起動でログを見ながら確認する。  
理由: 証明書・認証・ポート許可のどこで止まったかを即時に判断できるため。

## 8. 接続試験
### 8.1 subscribe
```bash
mosquitto_sub -h mqtt.esplab.home.arpa -p 8883 --cafile C:\mosquitto-data\ca.crt -u <MQTT_USERNAME> -P <MQTT_PASSWORD> -t "esp32lab/notice/status/+" -v
```

### 8.2 publish
```bash
mosquitto_pub -h mqtt.esplab.home.arpa -p 8883 --cafile C:\mosquitto-data\ca.crt -u <MQTT_USERNAME> -P <MQTT_PASSWORD> -t "esp32lab/call/status/all" -m "{\"requestType\":\"status\"}"
```

### 8.3 追加確認
- `LocalServer` の `npm run test:connect` を実行し、接続先と publish 成功を記録する。
- `ESP32` 側は `connectToMqttBroker success` と `Reply` / `online` を確認する。

[厳守] 試験は `mosquitto_pub/sub` と `LocalServer`、`ESP32` の結果を同時に見る。  
理由: 片側だけ成功しても、実運用で必要な経路が揃っているとは限らないため。

## 9. よくある失敗
- `Connection refused`: `listener 8883`、Windows Firewall、サービス起動を確認する。
- `Certificate verification failed`: SAN にホスト名または IP が入っていない可能性がある。
- `Not authorized`: `password_file`、ユーザー名、パスワード、`allow_anonymous false` を確認する。
- `DNS Failed`: `mqtt.esplab.home.arpa` の CoreDNS / DHCP 配布を確認する。

## 10. 参照先
- `MQTT_TLS環境構築まとめ.md`
- `ネットワーク運用仕様書.md`
- `コマンド仕様書.md`
- `試験仕様書.md`
- `試験記録書.md`

## 11. 変更履歴
- 2026-04-21: `011-0006` 向けの独立手順書として新規作成。理由: Mosquitto の TLS 設定と ID/パスワード認証を、接続手順と分離して短く参照できるようにするため。
