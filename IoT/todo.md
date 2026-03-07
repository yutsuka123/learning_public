# IoT TODO

[重要] 未完了タスクのみを管理する。  
理由: 現在の実施対象を明確化するため。

## 未完了

### OTA・LocalServer（継続・残課題）
- [ ] [2026-03-07][厳守][継続運用] ESP32へ書き込む前に `IoT/LocalServer/scripts/bump-esp32-beta-version.ps1` を実行し、`beta.x` の `x` を必ず更新する。
- [ ] [2026-03-07][重要] LocalServer（TypeScript/Node.js）をTask Scheduler（Windows）でPC起動時自動起動する導入手順を `コマンド仕様書.md` に追記し、実機確認する。
- [ ] [2026-03-07][重要] LocalSoft（Python）でSQLite保存（既定10日、0=無期限）を実装する。
- [ ] [2026-03-07][重要] LocalSoft（Python）をTask Scheduler（Windows）でPC起動時自動起動する導入手順を `コマンド仕様書.md` に追記し、実機確認する。
- [ ] [2026-03-07][重要] LocalSoftForCloud（双方向同期）の最小I/Fを設計し、API契約を文書化する。
- [ ] [2026-03-07][推奨] LocalSoftのデータ保存期間設定（30/90/365日）と削除API仕様を `機能仕様書.md` に追記する。

### OTA セキュア化（SecretCore実装前提）
- [ ] [2026-03-07][重要] OTA開始コマンドに HMAC 署名を付与し、ESP32側で検証する実装を追加する（SecretCore Phase1完了後）。
  - 対象: `call/otaStart` コマンド
  - 理由: コピーされた LocalServer から不正 OTA が実行できないようにするため。
- [ ] [2026-03-07][重要] 重要設定変更コマンドに HMAC 署名を付与し、ESP32側で検証する実装を追加する（SecretCore Phase1完了後）。
- [ ] [2026-03-07][重要] `試験仕様書.md` に OTA HMAC 検証試験（正常/不正署名の両ケース）を追記する。

### AIツール・自動解析対策（SecretCore実装時の必須確認事項）
- [ ] [2026-03-07][厳守] SecretCore の IPC ハンドシェイクは、子プロセス spawn + stdin/stdout 渡しを優先実装する（Pipe への外部割り込み排除）。
- [ ] [2026-03-07][厳守] K_device 導出に purpose binding を必ず含める（例: `"ota:" + deviceId + ":" + keyVersion`）。用途ごとに別鍵とする。
- [ ] [2026-03-07][厳守] IPC の nonce は必ず `crypto.randomBytes(16)` で生成する。`Math.random()` の使用を絶対に禁止する。
- [ ] [2026-03-07][厳守] Argon2id パラメータを `m=131072（128MB）, t=4, p=2` 以上に設定する（現在案の m=65536 は不十分）。
- [ ] [2026-03-07][厳守] パスフレーズ強度チェックを実装する（最低16文字、記号混在推奨）。
- [ ] [2026-03-07][重要] DPAPI + 追加ローカルAES二重暗号化を実装する（同一ユーザーセッションの別プロセスからの DPAPI 直接呼び出しへの対策）。
- [ ] [2026-03-07][重要] TS 層で IPC から受け取った署名/暗号 payload は即座に送信し、変数保持時間を最短にする。
- [ ] [2026-03-07][重要] 監査ログは Phase1 からパーミッション制限（0600）+ OS ログ（Windows イベントログ / macOS syslog）への二重書き込みを実装する。
- [ ] [2026-03-07][重要] HMAC 未実装期間（SecretCore Phase1完了前）は、OTA を信頼ネットワーク内限定とする旨を `取扱説明書.md` に明記する。
- [ ] [2026-03-07][将来対応] post-quantum アルゴリズム（CRYSTALS-Kyber等）への移行計画を 2030 年以降の課題として `todo.md` に残す。

### SecretCore（Rust別層）実装
- [ ] [2026-03-07][重要] SecretCore Phase1（Windows）を実装する。
  - K_user CSPRNG 生成
  - DPAPI 保存/読出し
  - IPC サーバー（Named Pipe + X25519 DH + AES-256-GCM + nonce）
  - `initialize` / `isInitialized` / `signCommand`（OTA用）
  - zeroize によるメモリゼロ化
- [ ] [2026-03-07][重要] TS側 SecretCoreClient を実装する（IPC接続・暗号化通信・`signCommand` 呼出し）。
- [ ] [2026-03-07][重要] SecretCore Phase2（macOS）を実装する。
  - Keychain 保存/読出し（`kSecAttrAccessibleWhenUnlockedThisDeviceOnly` 必須）
  - Unix Domain Socket IPC
- [ ] [2026-03-07][重要] SecretCore Phase3（バックアップ/復元）を実装する。
  - Argon2id パスフレーズ導出 + AES-256-GCM 暗号化
  - `createBackup` / `restoreBackup` API
  - settings.html にバックアップUI追加
- [ ] [2026-03-07][推奨] SecretCore Phase4（完全化）を実装する。
  - `rotateKey` / `revokeKey`
  - HMACチェーン付き監査ログ
  - K_device 導出とペアリング連携
- [ ] [2026-03-07][重要] SecretCore のビルド設定を難読化構成にする（obfstr + LLVM O3 + LTO + strip + panic=abort）。
- [ ] [2026-03-07][推奨] SecretCore の IPC 傍受試験・戻り値検査試験を `試験仕様書.md` に追記する。

### ネットワーク・インフラ
- [ ] [2026-03-07][重要] [期限: 2026-03-14] 新ルータ（GW `172.17.1.1`）導入を実施し、CoreDNS `172.17.1.100` / DHCP `172.17.1.200+` を適用する。
- [ ] [2026-03-07][重要] [期限: 2026-03-14] 新ルータ導入後、`mqtt.esplab.home.arpa -> 172.17.1.10` 解決を `nslookup` とESP32 `hostByName` の双方で確認する。
- [ ] [2026-03-07][重要] [期限: 2026-03-14] 新ルータ導入後、`SENSITIVE_MQTT_FALLBACK_IP` を無効化してMQTT TLS再接続試験を実施し、フォールバック依存を廃止する。
- [ ] [2026-03-07][重要] Mosquittoサーバ証明書をSAN再発行（`DNS:mqtt.esplab.home.arpa` + `IP:172.16.1.59`）後、ESP32でTLS再接続試験を実施し、`(-9984) X509`解消を試験記録へ反映する。
- [ ] [2026-03-07][重要] `(-9984) X509` 解消結果（ESP32ログ: `connectToMqttBroker success`）を `問題点記録書.md` と `試験記録書.md` へ反映する。
- [ ] [2026-03-07][重要] 専用Wi-Fiルーター（GW: `172.17.1.1`）導入後、`mqtt.esplab.home.arpa` を `172.17.1.10` へ固定割当てして名前解決を安定化する（DHCP固定割当て or DNS静的レコード）。
- [ ] [2026-03-07][重要] 専用Wi-Fiルーター導入時に、CoreDNSサーバーを `172.17.1.100` 固定、DHCP配布レンジを `172.17.1.200` 以降へ設定する。
- [ ] [2026-03-07][推奨] `C:\coredns\Corefile` に `mqtt.esplab.home.arpa -> 172.17.1.10` と `ntp.esplab.home.arpa` を明示定義し、反映後に `nslookup` で疎通確認する。
- [ ] [2026-03-07][推奨] 固定DNS指定廃止後の再接続試験（Wi-Fi再接続、NTP同期、MQTT TLS接続）を `試験仕様書.md` と `試験記録書.md` へ追記する。
- [ ] [2026-03-06][将来対応][重要] CoreDNSをローカルDNS候補としてPoC実施し、インターネットDNSとの両立（forward運用）を確認する。
- [ ] [2026-03-06][将来対応][厳守] CoreDNSではローカル専用ゾーンのみ管理し、公開ドメインの上書きを禁止する運用ルールを文書化する。
- [ ] [2026-03-07][重要] [将来対応] DHCP配布DNSまたはルーターDNSフォワーダ設定を見直し、ESP32側の固定DNS指定（方法①）を廃止しても `mqtt.esplab.home.arpa` が解決できる状態へ移行する。
- [ ] [2026-03-07][重要] AWS移行時の接続方式（NAT配下outbound MQTT/HTTPS、グローバルIP不要）の試験項目を `試験仕様書.md` に追加する。
- [ ] ブローカ運用ネットワークを IoT専用LAN（Private）へ分離し、FirewallのRemoteAddressをサブネット限定する。

### MQTT暗号化・鍵管理
- [ ] [2026-03-06][厳守] MQTT payloadのアプリ層暗号化/復号を `K_device` で実装する（送受信とも必須化）。
- [ ] [2026-03-03][重要] 鍵管理方式を確定する（完全ローカル自動運用 / 管理者手動承認付き）。
- [ ] `鍵管理初期セットアップ_実装たたき台.md` のPhase1～4に担当・期限を割り当てる。
- [ ] Phase1（K_master_user生成/セキュア保存）の最小実装を先行着手する（→ SecretCore Phase1と統合）。
- [ ] Phase2（初回ペアリングAPモード）のUI/CLI手順を確定する。
- [ ] 認証ロジック公開範囲と非公開範囲の判定基準を確定する。
- [ ] サーバ側で `K_device = HMAC-SHA256(K_master_user, base_mac)` 導出処理を実装する。
- [ ] `public_id` 生成処理（base_mac直接公開禁止）をESP/サーバ双方で統一する。
- [ ] MQTTトピックを `device/<public_id>/wifi/update` / `confirm` へ統一する。
- [ ] 機密設定（Wi-Fi/MQTT認証情報）の保存先を `sensitiveData.json` から NVS へ移行する（平文ファイル運用を廃止）。
- [ ] MQTT接続設定を最終運用値へ移行する（TLS有効 + 8883 + ID/Password必須 + allow_anonymous無効）。

### ESP32 セキュア化
- [ ] 第3段階のeFuse適用チェックリストを作成する。
- [ ] eFuse・セキュアブート・ROM暗号化準備（Day43相当）を実施する。
- [ ] ESP32側でAES-256-GCM復号処理（nonce/tag検証付き）を実装する。
- [ ] 初回ペアリングAPモード（物理操作必須、短時間有効）を実装する。
- [ ] ペアリング手順（base_mac取得 -> K_device導出 -> デバイス保存）を実装する。
- [ ] AP復旧モード（WPA2/固有パスワード/Web UI）を実装する。
- [ ] Wi-Fi誤設定試験（自動ロールバック）を試験仕様書へ追加する。
- [ ] ESP側でWi-Fi設定更新ステートマシン（未確定保存/接続試行/CONFIRM/ロールバック）を実装する。
- [ ] 時刻サーバー設定（`timeServerUrl`/`timeServerPort`/`timeServerTls`）の取得元を `sensitiveData.h` 優先から NVS 優先へ移行する。

### 文書・設計管理
- [ ] `日程表_ローカル運用まで_50日.md` を毎作業日更新し、Dayごとの状態と実績メモを記録する。
- [ ] [2026-03-03][将来対応] クラウド移行先Broker候補（AWS IoT Core / EMQX Cloud / HiveMQ Cloud等）を比較し優先候補を決定する。
- [ ] Cloud第4段階の候補（AWS/Google）比較表を作成する。
- [ ] `K_master_user` の暗号化バックアップ/復元機能を設計・実装する（→ SecretCore Phase3と統合）。
- [ ] バックアップ未保持時の再ペアリング手順を取扱説明書へ追記する。
- [ ] サポートパッケージ生成機能（鍵を含めない）を設計する。
- [ ] Local GUIの画面仕様（TLS接続状態、認証状態、接続エラー原因）を文書化する。
- [ ] mosquittoのTLS + ID/パスワード設定手順書を作成する。
- [ ] ESP側MQTT TLS接続失敗時の再試行戦略を仕様化する。

## 運用ルール
- [厳守] 完了タスクは `todo_oldYYYYMMDD.md` へ退避し、本書から削除する。

## 変更履歴
- 2026-03-07: OTA実機試験完了（beta.5 OTA成功、done通知確認）を受けて、OTA関連タスクを `todo_old20260307.md` へ退避。
- 2026-03-07: SecretCore（Rust別層）Phase1～4の実装タスクを追加。IPC傍受対策・raw key非返却・難読化・DPAPI/Keychain方針。理由: TS+Rust分離アーキテクチャを確定したため。
- 2026-03-07: OTAセキュア化タスク（OTA開始コマンドHMAC署名/ESP32検証）を新規追加。理由: SecretCore完成後の必須セキュリティ強化のため。
- 2026-03-07: 全タスクをカテゴリ別に再整理（OTA残課題/SecretCore/ネットワーク/鍵管理/ESP32セキュア化/文書管理）。
- 2026-03-07: ESP32書込み前に `beta.x` を更新する継続運用タスクを追加（`bump-esp32-beta-version.ps1` 運用）。
- 2026-03-07: OTA詳細仕様化に合わせ、進捗通知・リトライ制御・SHA256検証の実装タスクを追加。
- 2026-03-07: LocalServer初期実装反映に伴い、実機E2E試験・ESP32受信検証・LocalSoft/LocalSoftForCloud実装タスクを追加。
- 2026-03-07: `(-9984) X509` 解消結果の記録タスクと、新ルータ導入（期限: 2026-03-14）向けの実施/確認/フォールバック廃止タスクを追加。
- 2026-03-07: 新ルータ導入計画の具体値（GW `172.17.1.1` / DNS `172.17.1.100` / DHCP `172.17.1.200+`）と、導入前の暫定フォールバック運用タスクを追加。
- 2026-03-07: `(-9984) X509` 対策として、SAN再発行後のTLS再接続試験タスクを追加。
- 2026-03-07: 専用Wi-Fiルーター導入計画に合わせ、`mqtt.esplab.home.arpa -> 172.17.1.10` 固定割当てタスクを追加。
- 2026-03-07: 暫定運用として「方法① ESP32だけDNS指定（172.16.1.59）」を採用したため、固定依存を廃止する将来タスクと再試験タスクを追加。
- 2026-03-07: ネットワーク運用仕様の検討結果に合わせ、ログイン前自動起動（Windows/macOS）とAWS移行時接続方式、保存期間/削除APIの仕様化タスクを追加。
- 2026-03-06: DNSタスクをCoreDNS前提へ具体化し、外部DNSとの両立確認と公開ドメイン上書き禁止ルールを追加。
- 2026-03-06: `K_device` によるMQTT payload暗号化/復号の必須タスクを追加。
- 2026-03-06: ローカルDNSサーバー導入検討タスクを追加。
- 2026-03-03: ローカルサーバー設計検討の未確定事項（署名検証状況、鍵管理方式、UIスコープ、開発順序、クラウドBroker候補）をTODOへ追加。
- 2026-02-24: 新規作成。第3段階中心のタスクを初期登録。
