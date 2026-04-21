# 日常運用: LocalServer と ESP32（STA）接続クイックリファレンス

[重要] 本書は **「Wi‑Fi STA で ESP32 がローカルLANに乗り、`LocalServer` / MQTT ブローカと連携する」** ときに、**どの文書のどこを読めばよいか** を一箇所に集約する。  
理由: `コマンド仕様書.md` や `生デバイス初期設定手順書.md` に情報は分散しており、アドホック検索では見落としやすいため。

[推奨] 検索用キーワード: `LocalServer` `STA` `Station` `Wi‑Fi` `起動` `npm run dev` `npm run start` `3100` `8883` `4443` `mosquitto` `疎通` `同一LAN` `api/health` `api/devices` `dist` `curl`

---

## 1. 何を立てればよいか（最小構成）

| コンポーネント | 役割 | 典型ポート・備考 |
|----------------|------|------------------|
| **Mosquitto**（または同等 MQTT ブローカ） | ESP32 が TLS で接続する MQTT | **8883/TCP**（TLS）。手順: `コマンド仕様書.md` **§1** |
| **LocalServer** | Web UI・REST・デバイス一覧・OTA 制御・MQTT ゲートウェイ | HTTP **3100**（既定）、OTA HTTPS **4443**。手順: **§2** 本書、`LocalServer/README.md` **§2〜3**、`コマンド仕様書.md` **§4** |
| **SecretCore**（任意だが試験・鍵ワークフローでは必須） | 高リスク鍵処理・IPC | Named Pipe。手順: `生デバイス初期設定手順書.md` **§3.1**、`todo.md` セッション引継ぎ（既定 Pipe） |

[厳守] ESP32 が **STA でクラウドや別セグメントだけ** にいる構成では、**PC 上の `LocalServer` に届かない**ことがある。本リファレンスは **ESP32 と PC（LocalServer）が同一 L3 到達可能** な前提を正とする。

---

## 2. 起動順（STA 運用・同一LAN）

1. **MQTT ブローカ**を先に起動する（`mosquitto -c mosquitto.conf -v` 等）。出典: `コマンド仕様書.md` **§1**。
2. **`IoT/LocalServer` で `.env` を用意**し、`MQTT_*` / `*_HOST_NAME` / `*_HOST_IP` を **STA からも解決・到達できる値** に合わせる。出典: `LocalServer/README.md` **§2**、`MQTT_TLS環境構築まとめ.md`。
3. **`npm install` → `npm run build` → `npm run start`**（開発時は `npm run dev` 可）。出典: `コマンド仕様書.md` **§4**、`LocalServer/README.md` **§2**。
4. **疎通**: `GET http://127.0.0.1:3100/api/health`、**`GET http://127.0.0.1:3100/api/devices`**（ESP32 の `online` / `lastSeenAt` 確認）、必要に応じて `npm run test:connect`。**コピペ用の最短手順は本書 §5**。出典: `LocalServer/README.md` **§3**。
5. **SecretCore / ProductionTool が必要な作業**では、`生デバイス初期設定手順書.md` **§3.1 起動順（固定）** に従う。

---

## 3. ESP32 側（STA）の前提

- **同一 SSID セグメント**で、ESP32 の `sensitiveData` / NVS に保存された **STA SSID・パスワード**が有効であること。
- **MQTT ブローカのホスト名**（例: `mqtt.esplab.home.arpa`）が **STA から DNS 解決できる**こと。切り分け: `ネットワーク運用仕様書.md`、`問題点記録書.md`（NIC / CoreDNS 系）。
- **OTA** を使う場合は ESP32 から **`https://<OTAホスト>:4443/...` に到達**すること。出典: `コマンド仕様書.md` **§6**、`OTA仕様書.md`。

---

## 4. 詳細手順の正（本書より深い内容）

| 目的 | 正とする文書・節 |
|------|------------------|
| コマンド列・インストール・Task Scheduler | `コマンド仕様書.md` **§4**, **§5**, **§6**, **§19**（生デバイス抜粋） |
| 生デバイス 0 から ProductionTool 主運用 | `生デバイス初期設定手順書.md` 全体（**§3 起動順**） |
| TLS・証明書・mosquitto | `MQTT_TLS環境構築まとめ.md` |
| DNS・ルータ・到達性 | `ネットワーク運用仕様書.md` |
| 画面からの操作 | `LocalServer管理画面仕様書.md` |
| 試験 ID（7041 等） | `試験仕様書.md` / `試験記録書.md` |

---

## 5. すぐできる疎通確認（再起動後・作業前）

[重要] **PC 再起動直後や「MQTT まで繋がったはず」ときに、LocalServer と ESP32 の対応関係を短時間で確認する**ための手順を固定する。  
理由: ポート待受と API 応答を並べて見れば切り分けが速く、GUI を開かずに再現可能な証跡を残せるため。

[厳守] 本節は **到達性と一覧状態の確認** に限定する。鍵・認証情報は **文書・ログへ貼り付けない**（`セキュリティ方針_認証情報取扱い.md`）。

### 5.1 想定

- Mosquitto 等が **8883/TCP** で待受している（任意で確認可）。
- `IoT/LocalServer` に **`.env` 済み**、`SecretCore` 実行ファイルが配置済み（通常は据え置き）。初回のみ `npm install`。

### 5.2 待受のざっくり確認（任意・Windows）

PowerShell または `cmd` で次を実行する。

```text
netstat -ano | findstr "LISTENING" | findstr ":3100 :8883 "
```

- **8883** が `LISTENING` で **3100 が無い**場合: MQTT ブローカは立っているが **LocalServer 未起動**の可能性が高い（次項へ）。

### 5.3 LocalServer の起動（`dist` 無し・更新後はビルド必須）

```powershell
cd IoT\LocalServer
npm run build
npm run start
```

- [推奨] 開発中は `npm run dev`（`tsx watch`）でもよい。
- 起動ログに **`LocalServer HTTP started. httpPort=3100`** と **`MQTT connected.`** が出ることを目視確認する。
- `dist\server.js` が無い場合、`npm run build` なしでは `npm run start` が失敗する。

### 5.4 API でヘルスとデバイス一覧を確認（推奨）

別ターミナルから（`curl.exe` は Windows に同梱）。

```powershell
curl.exe -sS http://127.0.0.1:3100/api/health
curl.exe -sS http://127.0.0.1:3100/api/devices
```

**読み方（最小）**

| 応答 | 見る場所 |
|------|-----------|
| `/api/health` | `"status":"ok"` であること |
| `/api/devices` | 配列 `devices` の各要素で **`onlineState` が `online`**、**`lastSeenAt` が直近**であること。`connectionMode`（例: `mqtt`）、`deviceName`、`ipAddress` が参考になる |
| 一覧が空、または該当機が `offline` | ESP32 の **STA 同一LAN**、**MQTT 認証・ホスト名解決**、ブローカ到達を疑う（**§3**、`問題点記録書.md`） |

### 5.5 ブラウザでの確認

- `http://localhost:3100/`（管理画面・一覧）。API と同じ情報系を GUI で確認する用途。

### 5.6 PC ログオン時の自動起動

- **タスク登録による常駐**は `コマンド仕様書.md` **§5**、`LocalServer/README.md` **§6**、`install-task-scheduler.ps1` を参照する。  
理由: 手動起動（本節）と常駐運用の手順を混同しないため。

### 5.7 MQTT だけ別角度で見る場合（参考）

- ファームウェアのトピック形式は **`esp32lab/<kind>/<sub>/<deviceName>`**（実装参照: `ESP32/src/MQTT/mqtt.cpp` の `createTopicText`）。詳細は `IF仕様書.md` / `MQTTコマンド仕様書.md`。
- ブローカ購読で到達を見る場合は **TLS・認証**が `.env` / Mosquitto 設定と一致していること。

---

## 6. 仕様変更履歴

- 2026-04-17: **§5 すぐできる疎通確認** を追加。**§2** の疎通手順に `/api/devices` と §5 への誘導を追記。検索キーワードに `api/health` 等を追加。理由: 再起動後に LocalServer 未起動と実機 online を最短で切り分ける運用要望のため。
- 2026-04-18: 新規作成。理由: `LocalServer` を Wi‑Fi STA 前提で立ち上げる手順が複数文書に分散しており、索引とコマンド仕様書から一方通行で辿れる入口を追加するため。
