# LocalServer 秘密処理別層仕様書（TS + Rust）

[重要] 本書は LocalServer の TypeScript 本体と、Rust 製秘密処理層 `SecretCore` の設計方針・IPC仕様・保護要件を定義する。  
理由: 別PCへのコードコピー、設定ファイル抜き取り、IPC傍受、メモリ観測を組み合わせた不正利用を抑止するため。

## 1. 目的と脅威モデル
### 1.1 本設計で防ぎたいこと

| 脅威 | 概要 | 対策方針 |
|------|------|----------|
| コード複製 | TS/Rustソースを別PCへコピーして偽サーバーを立てる | TPM拘束された `wrapped_secret` がなければ正規鍵を生成できない構成にする |
| 設定ファイル抜き取り | `settings.json` / `.env` / `device_db` を読まれる | 秘密値をファイルへ平文保存しない |
| IPC傍受 | Named Pipe / Unix Socket の通信を盗み見る | ローカル限定 + ACL + セッション暗号化で保護する |
| 戻り値盗み見 | SecretCore の戻り値を読み取る | raw key を返さない |
| ロジック解読 | Rust バイナリを逆解析して導出式を読む | ロジック秘匿に依存せず、TPM拘束と raw key 非返却で守る |
| 一般権限での参照 | 別ユーザー/別プロセスが SecretCore に接続する | OS ACL と子プロセス管理で接続元を制限する |

### 1.2 防ぎきれない前提
- [重要] 運用PCの管理者権限が侵害された場合は完全防御が困難である。
- [重要] 本設計の主眼は「別PCコピーによる不正利用防止」と「同一PC再インストール復旧」である。
- [補足] 同一PC侵害は運用PC管理者責任の範囲として扱う。

### 1.3 責任範囲の親定義
- [厳守] `LocalServer` / `SecretCore` / `ProductionTool` / `ESP32` の責任境界は `モジュール仕様書.md` を親定義とする。
- [厳守] 本書はそのうち `LocalServer` と `SecretCore` の境界を詳細化する文書とし、`ProductionTool` の製造責務を取り込まない。

## 2. アーキテクチャ概要
```text
┌────────────────────────────────────────────────────────────┐
│ LocalServer（TypeScript / Node.js）                        │
│ Web UI / REST API / 入力検証 / 進捗表示 / 通常監視        │
│                                                            │
│  ↕ 保護済みIPC（Named Pipe 優先。将来 Unix Socket 対応）  │
│                                                            │
│ SecretCore（Rust）                                         │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ TPMで S_random を保護                                │  │
│  │ wrapped_secret 読込/検証                              │  │
│  │ k-user 生成                                           │  │
│  │ k-device 導出                                         │  │
│  │ 高リスクワークフロー実行                              │  │
│  │ HMAC / 署名 / AES-GCM payload生成                     │  │
│  │ 監査ログ記録                                          │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
         ↕ MQTT(TLS) / HTTPS(OTA) / APモード通信
   ESP32 デバイス群
```

## 3. 鍵階層と役割
### 3.1 鍵構造
```text
TPM
 │
 └─ S_random
      │
      └─ HKDF(ikm=S_random, salt=S_app, info="k-user-v1")
           │
           └─ k-user
                │
                └─ HKDF(ikm=k-user, salt=SHA256(mac_address), info="k-device-v1")
                     │
                     └─ k-device
```

### 3.2 役割
- `S_random`: TPM により保護されるランダム秘密
- `wrapped_secret`: `S_random` を TPM で暗号化した保存物
- `k-user`: ユーザー環境のルート鍵
- `k-device`: デバイス単位の通信鍵

### 3.3 原則
- [厳守] `S_random` / `k-user` / `k-device` は平文ファイル保存しない。
- [厳守] TypeScript 本体へ raw key を返さない。
- [厳守] `wrapped_secret` のみを保存する。

## 4. TS本体と SecretCore の責務分担
### 4.0 現在の到達点と残課題
- [重要][2026-03-15] 現在の実装では、`SecretCore` は鍵管理、暗号化/復号、バックアップ、fingerprint 取得に加え、Stage1 の MQTT command publish、Stage2 の notice subscribe / 受信イベントキュー、Stage3 の `k-device` 復号と最小DTO正規化、Stage4 の `deviceState` 完成スナップショット統合、Stage5 の `offline timeout` 判定、Stage6 の `status recovery wait`、Stage7 の OTA workflow 開始/監視/状態取得を担当する。
- [重要][2026-03-15] 現在の実装では、`LocalServer` は Rust から受けた `deviceState` を保持し、WebSocket/UI 更新、OTA HTTPS 配布、REST 公開を担当している。
- [重要][2026-03-15] 上記は最終要件「高リスク通信を含む通信まで Rust 主導」との間に差分があるため、`003-0012` は [部分完了] として扱う。
- [厳守][2026-03-15] `003-0013` 対応として、TypeScript から SecretCore を呼ぶ窓口は `SecretCoreFacade` のみとし、`SecretCoreIpcClient` の直接利用を禁止する。
- [進捗][2026-03-15] `003-0012` の次段階として、`server.ts` は `deviceTransport` IF に依存し、`mqttGateway` はその TS 実装として扱う構成へ移行開始した。
- [進捗][2026-03-15] `mqttGateway` の command publish は `SecretCoreFacade -> mqtt_publish` を経由する構成へ切替済み。
- [進捗][2026-03-15] `mqttGateway` の notice subscribe は `SecretCoreFacade -> mqtt_start_receiver / mqtt_drain_events` を経由する構成へ移行開始した。
- [進捗][2026-03-15] `mqttGateway` の Rust モードでは raw topic/payload ではなく、`statusUpdated` / `trhUpdated` / `otaProgressUpdated` / `secureEchoUpdated` の DTO を受け取る構成へ更新した。
- [進捗][2026-03-15] `mqttGateway` の Rust モードでは `deviceState` 完成スナップショットも受け取り、TS 側は `registry.upsertDeviceState()` で保持する構成へ更新した。
- [進捗][2026-03-15] `mqttGateway` の Rust モードでは `deviceStateUpdated` による `offline timeout` 更新も受け取り、TS 側ローカル timeout 判定を無効化した。
- [進捗][2026-03-15] `waitForStatusRecoveryByDevice()` は Rust モード時 `SecretCoreFacade -> wait_for_status_recovery` を使い、TS 側 polling ループを通さずに online 復帰待機を行う構成へ更新した。
- [進捗][2026-03-15] `/api/workflows/signed-ota/start` と `GET /api/workflows/:workflowId` は Rust workflow を呼ぶ薄い窓口として追加し、TS 側は OTA 開始後の逐次手順を保持しない構成へ移行開始した。
- [進捗][2026-03-16] `LocalServer/public/index.html` は `/api/workflows/signed-ota/start` と `GET /api/workflows/{workflowId}` を用いて、単一対象機の OTA workflow 状態を一覧表示・定期取得する構成へ更新した。
- [重要][2026-03-15] 起動時は `SecretCore` readiness を確認後に `gateway.connect()` する。理由: pipe 生成前に strict 復号要求が先行し、起動直後の受信失敗ノイズが出るため。

### 4.1 TypeScript 本体（LocalServer）担当
- Web UI（デバイス一覧、OTA操作、設定画面）
- REST API
- WebSocket 配信
- MQTT 受信DTO/`deviceState` スナップショットの保持・UI反映
- MQTT command publish の要求生成と `SecretCoreFacade` への委譲
- MQTT notice 受信DTOの drain と `DeviceRegistry` / UI 反映
- Rust モード時の `deviceStateUpdated` 受信と画面反映
- Rust モード時の `wait_for_status_recovery` 呼び出し
- Rust 側 workflow 開始API / 状態取得API の公開
- Rust 側 workflow 状態の定期取得と一覧UI反映（進捗表示のみ）
- OTA 配布エンドポイント提供（HTTPS） [旧仕様][2026-03-15 時点]
- 公開設定管理
- `SecretCoreFacade` 経由での IPC 依頼送信と結果受信
- AP 共通トップ画面、ProductionTool 追加認証前の通常画面制御
- 高リスクワークフロー実行前の入力検証、進行状態管理、監査表示
- `public_id`、`keyVersion`、再ペアリング状態などのメタ情報管理
- [進捗][2026-03-16] `POST /api/workflows/pairing/start`
- [将来対応] `POST /api/workflows/key-rotation/start`
- [将来対応] `POST /api/workflows/production/start`
- `POST /api/workflows/signed-ota/start`
- `GET /api/workflows/{workflowId}`
- [厳守] 上記 REST API は workflow 開始要求と状態取得のみを公開し、秘密処理の内部 helper を外部公開しない。
- [重要][2026-03-16] `pairing/start` は TS 側の REST 窓口と事前検証、および Rust 側の workflow 骨格（`queued -> running -> waiting_device -> verifying -> failed(placeholder)`）まで実装済みである。
- [重要][2026-03-16] `SecretCore` の `runPairingSession()` は現時点で `createPairingBundle` 相当の内部生成と、AP モード `/api/auth/login` / `GET /api/settings/network` による到達・認証 precheck まで実装済みである。
- [重要][2026-03-16] さらに ESP32 AP 側 `GET /api/pairing/state` placeholder を読み、`targetDeviceId` / `state` / `keyDevicePresent` を Rust 側 workflow 詳細へ反映できる。
- [重要][2026-03-16] さらに ESP32 AP 側 `POST /api/pairing/session` placeholder へ `sessionId` / `bundleId` / `targetDeviceId` / `keyVersion` を登録し、Rust 側 workflow を `verifying` 手前まで進められる。
- [重要][2026-03-16] さらに ESP32 AP 側 `POST /api/pairing/bundle-summary` placeholder へ `publicId` / `nonce` / `signature` / `requestedSettingsSha256` を登録し、秘密本体未送達のまま AP 側 `bundle_staged` 状態まで進められる。
- [重要][2026-03-16] さらに ESP32 AP 側 `POST /api/pairing/transport-session` placeholder へ `requestedKeyAgreement` / `requestedBundleProtection` を登録し、secure transport 本体未実装のまま AP 側 `transport_prepared` 状態まで進められる。
- [重要][2026-03-16] さらに ESP32 AP 側 `POST /api/pairing/transport-handshake` で P-256 ECDH handshake を実行し、Rust / ESP32 双方が transport session key をセッション内メモリだけに保持できる。
- [重要][2026-03-16] encrypted bundle 本体送達 / ESP32 側復号 / NVS 完了判定は継続実装中である。
- [重要][2026-03-16] end-to-end で完了確認済みの公開 API は `POST /api/workflows/signed-ota/start` と `GET /api/workflows/{workflowId}` のみであり、`pairing` / `key-rotation` / `production` は段階的に到達させる。

### 4.2 SecretCore（Rust）担当
- `S_random` の生成
- TPM による `wrapped_secret` 生成/復号
- `k-user` 生成
- `k-device` 導出
- HMAC / 署名生成
- AES-GCM payload 生成/検証補助
- `k-user` のパスワード暗号化バックアップ出力/復元
- MQTT command publish（Stage1）
- MQTT notice subscribe / 受信イベントキュー（Stage2）
- MQTT notice payload の `k-device` 復号と最小DTO正規化（Stage3）
- `deviceState` 完成スナップショット統合（Stage4）
- `offline timeout` 判定と `deviceStateUpdated` 生成（Stage5）
- `status recovery wait` と online 復帰結果返却（Stage6）
- OTA workflow `run_signed_ota_command` / `get_workflow_status`（Stage7）
- workflow 内部での `createPairingBundle` 生成
- AP モード ECDH セッション鍵生成
- 高リスク処理の対ESP32通信開始、進捗管理、完了判定
- `runPairingSession()`
- `runKeyRotationSession()`
- `runProductionSecureFlow()`
- `runSignedOtaCommand()`
- `wrapped_secret` / `device_db` バックアップ支援
- 監査ログ記録

### 4.3 境界ルール
- [厳守] `LocalServer` は raw `k-user`、raw `k-device`、ECDH 共有秘密、`k-pairing-session` を保持しない。
- [厳守] `LocalServer` は高リスク処理の逐次通信手順を保持せず、開始要求と進捗表示のみを行う。
- [厳守] `SecretCore` は通常運用 UI、MQTT 状態表示、SQLite 保存、製造画面制御を担当しない。
- [厳守] `ProductionTool` に属する eFuse 最終有効化、製造ログ主記録、工場内セキュア化主体は本書の対象外とする。
- [厳守] `SecretCoreIpcClient` は low-level 実装とし、業務コードから直接 `sendRequest()` を呼ばない。必ず `SecretCoreFacade` を経由する。
- [厳守] `SecretCoreFacade` の戻り値は fingerprint、status、workflow 状態、暗号化済み payload などの最小DTOに限定する。
- [禁止] Web UI / REST handler が low-level IPC コマンド名や payload 形式を直接知る構成。

### 4.4 ワークフロー進捗状態
- [厳守] 高リスクワークフローの進捗状態は `queued` / `running` / `waiting_device` / `verifying` / `completed` / `failed` を正規値とする。
- [厳守] `LocalServer` へ返す情報は上記進捗状態、workflowId、対象個体、エラー要約、最終結果に限定する。

## 5. TPM 管理設計
### 5.1 初回インストール
1. `S_random` を CSPRNG で生成する。
2. `wrapped_secret = TPM_Encrypt(S_random)` を作成する。
3. `wrapped_secret` を専用ファイルへ保存する。

### 5.2 起動時
1. `wrapped_secret` を読み込む。
2. `S_random = TPM_Decrypt(wrapped_secret)` を実行する。
3. `k-user` を HKDF で生成する。
4. 通信対象 `base_mac` に対して `k-device` を都度導出する。

### 5.3 保存方針
- [厳守] `wrapped_secret` 保存先は一般設定ファイルと分離する。
- [厳守] `wrapped_secret` 改ざん検知のため、ヘッダに `version` と `createdAt` を持たせる。
- [禁止] `S_random` を復号後にディスクへ書き戻さない。

## 6. IPC 設計
### 6.1 IPC方式
| OS | IPC種別 | 方針 |
|----|---------|------|
| Windows | Named Pipe | 現行実装対象。TPM前提の主要対象OS |
| macOS | Unix Domain Socket | [将来対応] TPM相当保護方式確定後に対応 |

- [禁止] TCP / HTTP で SecretCore を待受させない。
- [禁止] LAN / WAN を介した通信経路を利用しない。

### 6.2 接続制限
**Windows（Named Pipe）**
```text
PipeName: \\.\pipe\iot-secret-core-<machineId>-<sessionId>
DACL:
  - LocalServer 実行ユーザーSIDのみ ReadWrite 許可
  - Everyone 拒否
  - NETWORK SERVICE 拒否
```

- [厳守] `machineId` を Pipe 名へ含める。
- [厳守] `sessionId` は LocalServer 起動ごとにランダム生成し、固定化しない。
- [厳守] SecretCore は LocalServer の子プロセスとして起動する。
- [重要] 現在実装では、LocalServer 親プロセスがランダムな Pipe 名と 32byte セッション鍵を生成し、環境変数で SecretCore 子プロセスへ渡す。

### 6.3 IPC通信保護
- [厳守] IPC チャネルは AES-256-GCM + nonce + requestId で保護する。
- [厳守] `nonce` は再利用禁止とする。
- [厳守] `requestId` は `crypto.randomUUID()` 等の暗号学的乱数で生成する。
- [厳守] `timestamp` は ±30秒以内のみ受理する。
- [厳守] SecretCore は受信済み `requestId` を短時間キャッシュし、同一 `requestId` の再送を拒否する。
- [重要] 現在実装では、AAD を `v=1|rid=<requestId>|ts=<timestamp>` / `v=1|rrid=<requestId>|ts=<timestamp>` の固定文字列で構成し、Node/Rust 間の順序差による検証不一致を防ぐ。
- [旧仕様] `SECRET_CORE_IPC_SESSION_KEY_B64` 未指定の手動起動時のみ、互換の平文IPCを許可する。理由: 既存保守スクリプトとの互換維持のため。ただし通常運用では使用しない。

## 7. SecretCore の公開 API（限定）
### 7.1 許可 API
| 操作名 | 概要 | 返却値 |
|--------|------|--------|
| `initializeKeyHierarchy` | `S_random` 生成、TPMラップ、`wrapped_secret` 保存 | 成功/失敗 + schemaVersion |
| `isInitialized` | `wrapped_secret` の有無と利用可否確認 | bool |
| `runPairingSession` | AP モード初回投入/再ペアリングを開始し、完了判定まで実行 | workflowId + 初期状態 |
| `runKeyRotationSession` | `k-user` 再発行後の鍵切替ワークフローを開始し、完了判定まで実行 | workflowId + 初期状態 |
| `runProductionSecureFlow` | ProductionTool 高リスク処理を開始し、完了判定まで実行 | workflowId + 初期状態 |
| `runSignedOtaCommand` | 署名付き OTA 開始ワークフローを開始し、完了判定まで実行 | workflowId + 初期状態 |
| `getWorkflowStatus` | workflow の進捗状態と結果を取得 | 状態、結果、エラー要約 |
| `issue_k_user` | k-user 発行状態を保証し fingerprint を返す | `isIssued` `keyFingerprint` `source` |
| `get_k_user_status` | k-user 状態取得 | `isIssued` `issuedAt` `keyFingerprint` `deviceKeyCount` `source` |
| `verifyInboundHmac` | 受信HMAC検証 | bool |
| `encrypt` / `decrypt` | `k-device` による暗号化/復号 | 暗号化済み payload / 復号文字列 |
| `export_k_user` / `import_k_user_backup` | `k-user` の暗号化バックアップ出力/復元 | ファイル出力結果 / 復元結果 |
| `mqtt_publish` | MQTT broker へ QoS付き publish を実行する（Stage1: command publish用） | `published` `topic` `qos` `payloadLength` |
| `mqtt_start_receiver` / `mqtt_drain_events` | MQTT notice subscribe 常駐ループ起動と受信イベント取り出し（Stage2） | 起動結果 / 受信イベント配列 |
| `mqtt_drain_events` Stage3 | `status/trh/otaProgress/secureEcho` の最小DTOを返す | DTO配列 |
| `mqtt_drain_events` Stage4 | `deviceState` 完成スナップショット付きで返す | DTO + deviceState |
| `mqtt_drain_events` Stage5 | `offline timeout` 検出時に `deviceStateUpdated` を返す | DTO + deviceState |
| `wait_for_status_recovery` Stage6 | Rust 側 `deviceState` を参照して online 復帰を待機する | `deviceName/publicId/firmwareVersion/configVersion` |
| `run_signed_ota_command` Stage7 | 単一対象機の OTA workflow を開始し、publish〜進捗監視〜完了判定を Rust 側で実行する | `workflowId/state/...` |
| `get_workflow_status` Stage7 | workflow 状態を取得する | `workflowId/state/result/errorSummary/...` |
| `exportWrappedSecretBackup` | `wrapped_secret` のバックアップ準備 | ファイル出力結果 |
| `restoreWrappedSecretBackup` | `wrapped_secret` の復元 | 成功/失敗 |
| `getAuditLog` | 監査ログ取得 | 秘密値を含まない配列 |

### 7.1.1 LocalServer REST 公開経路
- `POST /api/workflows/pairing/start` -> `runPairingSession`
- `POST /api/workflows/key-rotation/start` -> `runKeyRotationSession`
- `POST /api/workflows/production/start` -> `runProductionSecureFlow`
- `POST /api/workflows/signed-ota/start` -> `runSignedOtaCommand`
- `GET /api/workflows/{workflowId}` -> `getWorkflowStatus`
- [厳守] REST 応答は `workflowId`、`state`、`result`、`errorSummary`、監査表示向け最小情報に限定する。
- [厳守] `LocalServer` は `SecretCore` の内部 helper を REST 公開しない。

### 7.1.2 `ProductionTool` 基本機能フェーズの境界
- [重要] `ProductionTool` 基本機能安定化フェーズ（`004-0008`〜`004-0010`）では、`SecretCore` の共通部を再利用してよい。
- [厳守] 上記の「共通部を再利用」とは、`LocalServer` 側で整備した `SecretCore` 共通ライブラリや関連モジュールを `ProductionTool` でも利用する意味であり、`LocalServer` と `ProductionTool` を単一ソフトへ統合する意味ではない。
- [厳守] `ProductionTool` は `LocalServer` と別ソフト、別実行物、別インストーラとして独立動作させる。
- [厳守] ただし、この段階で許可するのは起動時初期化確認、追加認証後の状態取得、対象機確認、dry-run、監査ログ取得までとする。
- [厳守] 不可逆処理本体は `runProductionSecureFlow()` の最終実行として後続フェーズへ分離し、基本機能フェーズで完了扱いにしない。
- [厳守] `ProductionTool` は `LocalServer` の通常 REST を流用して高リスク処理へ入らず、専用入口またはローカル専用 IF を持つ。
- [厳守] `ProductionTool` の戻り値も `workflowId`、`state`、`result`、`errorSummary`、監査表示向け最小情報に限定し、raw key、中間秘密、eFuse 実値を UI へ返さない。

### 7.2 禁止 API
- [禁止] `getUserKey()`
- [禁止] `getDeviceKey()`
- [禁止] `exportPlainSecret()`
- [禁止] `getPairingSessionKey()`
- [禁止] 任意バイト列を自由に署名させる汎用 API
- [禁止] TS が `createPairingBundle`、署名、対ESP32送達、検証、完了判定を個別APIの組み合わせで逐次制御する構成
- [禁止] `POST /api/pairing/bundle` 等の `createPairingBundle` 直公開 REST API
- [禁止] `POST /api/crypto/sign` `POST /api/crypto/encrypt` など低レベル秘密処理を外部公開する REST API

### 7.3 ワークフロー共通制約
- [厳守] `LocalServer` は必須入力を検証したうえで workflow を開始する。
- [厳守] workflow 開始後の対ESP32通信、検証、再試行、完了判定は `SecretCore` が責任を持つ。
- [厳守] workflow 経由の返却値に raw `k-device`、`k-pairing-session`、中間署名素材を含めない。
- [厳守] `ProductionTool` 基本機能フェーズの dry-run でも、上記と同じく raw `k-device`、`k-pairing-session`、中間署名素材、eFuse 実値を返却しない。

### 7.4 `runPairingSession` 制約
- [厳守] 入力は少なくとも `targetDeviceId` `sessionId` `keyVersion` `requestedSettings` を含む。
- [厳守] `requestedSettings` の必須項目（Wi-Fi / MQTT / OTA / 認証情報）は `LocalServer` 側で事前検証し、不足時は workflow を開始しない。
- [厳守] bundle は固定公開鍵検証と AP モード ECDH の都度セッション鍵で保護する。

### 7.5 署名対象の固定
```json
{
  "command": "<コマンド名>",
  "target": "<デバイスID>",
  "timestamp": "<Unix時刻ms>",
  "nonce": "<ランダム値>",
  "keyVersion": "<鍵バージョン>",
  "firmwareVersion": "<OTA時のみ>",
  "sha256": "<OTA時のみ>"
}
```

- [厳守] 署名対象フィールドは固定とする。

## 8. バックアップ/復元設計
### 8.1 バックアップ対象
- `wrapped_secret`
- `k-user-backup.enc`（`k-user` 復旧用の暗号化バックアップファイル）
- `device_db`

### 8.2 バックアップ方針
- [厳守] `k-user` / `k-device` / `S_random` 平文はバックアップしない。
- [重要] `wrapped_secret` 単体は別PCで復号できない。
- [重要] ただし `k-user-backup.enc` とパスワードによる復旧経路を許容する。
- [推奨] `wrapped_secret` と `device_db` は別媒体へ退避する。

### 8.3 復元手順
1. `wrapped_secret` を元の専用配置先へ復元する。
2. 必要に応じて `k-user-backup.enc` を復元する。
3. `device_db` を復元する。
4. SecretCore が TPM で `wrapped_secret` を復号する、または `k-user-backup.enc` をパスワードで復号する。
5. `k-user` を再生成または復旧する。
6. `k-device` を都度再生成する。

### 8.4 復元制約
- [厳守] `wrapped_secret` 経路は同一PC + 同一TPM でのみ復元可能とする。
- [重要] 暗号化バックアップファイル経路は、適切なパスワードと `SecretCore` 実装により別PC復旧を許容する。
- [重要] TPM初期化 / TPM交換 / マザーボード交換時は再発行が必要である。

## 9. 監査ログ仕様
### 9.1 記録対象
| 操作 | ログ内容 |
|------|---------|
| `initializeKeyHierarchy` | 日時、schemaVersion、成否 |
| `TPM unwrap` | 日時、operation、成否 |
| `runSignedOtaCommand` | 日時、targetDevice、keyVersion、workflowId、成否 |
| `runPairingSession` | 日時、targetDevice、keyVersion、workflowId、bundleId、成否 |
| `runKeyRotationSession` | 日時、targetDevice、keyVersion、workflowId、成否 |
| `runProductionSecureFlow` | 日時、targetDevice、workflowId、成否 |
| `wrapped_secret` バックアップ | 日時、成否 |
| `wrapped_secret` 復元 | 日時、成否 |
| IPC 認証失敗 | 日時、詳細 |

- [厳守] 監査ログに raw key を含めない。
- [推奨] 監査ログファイル権限は SecretCore 実行ユーザーのみに制限する。

## 10. 解析耐性と限界
### 10.1 基本方針
- [重要] Kerckhoffs 原則に従い、ロジック秘匿ではなく保護された秘密（`wrapped_secret` + TPM 拘束、または `k-user-backup.enc` + パスワード）で守る。
- [推奨] Rust バイナリは Release + strip + LTO + zeroize を適用する。

### 10.2 AI/自動解析時の考え方
- ロジックが読まれても保護された秘密を復号できなければ不正利用しにくい。
- raw key を IPC 越しに返さないため、コード解析だけで実鍵取得はできない。
- 同一PCでのメモリダンプや管理者侵害は完全には防げない。

### 10.3 現時点の主要注意点
- [厳守] IPC ハンドシェイクは子プロセス spawn + stdin/stdout 連携を優先する。
- [厳守] TS 層で署名結果や暗号 payload を保持する時間を最短にする。
- [厳守] 乱数は Node.js `crypto` と Rust `OsRng` を使用する。
- [重要] `wrapped_secret` 破損時は `k-user-backup.enc` 経路で復旧するため、暗号化バックアップとパスワード管理を導入時に必須説明する。

## 11. 実装フェーズ（推奨順）
### Phase 1: 基盤
1. SecretCore 骨格（Rust）
2. `initializeKeyHierarchy` / `isInitialized`
3. TPM ラップ/アンラップ
4. Named Pipe + 暗号化 IPC
5. `signCommand`

### Phase 2: 暗号通信連携
1. `runSignedOtaCommand`
2. ESP32 側 HMAC / payload 検証
3. `device_db` 連携
4. `runPairingSession`
5. AP モード ECDH セッション鍵保護

### Phase 3: 復旧運用
1. `wrapped_secret` バックアップ/復元
2. `device_db` バックアップ/復元
3. 監査ログ UI

### Phase 4: 完全化
1. `runKeyRotationSession`
2. `runProductionSecureFlow`
3. 障害時再登録フロー
4. TPM初期化検知
5. 将来 macOS 対応の整理

## 12. 関連文書
- `鍵管理および初期セットアップ設計仕様書.md`
- `鍵管理初期セットアップ_実装たたき台.md`
- `セキュリティ方針_認証情報取扱い.md`
- `使用技術仕様書.md`
- `モジュール仕様書.md`

## 13. 変更履歴
- 2026-03-16: `ProductionTool` へ名称統一し、`SecretCore` 共通化は `LocalServer` 側共通部の再利用を意味し、ソフト自体は分離・独立動作させる前提を追記。理由: `ProductionTool` の名称統一と、共通化/分離の解釈ずれを防ぐため。
- 2026-03-16: `7.1.2 ProductionTool 基本機能フェーズの境界` を追加し、`SecretCore` 共通部の再利用範囲と、不可逆処理本体をまだ分離する前提を追記。理由: `ProductionTool画面仕様書.md` と `IF仕様書.md` で追加した起動・追加認証・dry-run 導線を、別層仕様でも安全側に固定するため。
- 2026-03-16: ESP32 AP 側 `POST /api/pairing/transport-handshake` と、Rust 側 workflow の P-256 ECDH handshake を追記。理由: `runPairingSession()` の責務移行が transport negotiation placeholder から実ハンドシェイク段階へ進んだことを別層仕様へ正確に反映するため。
- 2026-03-16: ESP32 AP 側 `POST /api/pairing/transport-session` placeholder と、Rust 側 workflow の secure transport negotiation を追記。理由: `runPairingSession()` の責務移行が「bundle summary staging」からさらに一段進んだことを別層仕様へ正確に反映するため。
- 2026-03-16: ESP32 AP 側 `POST /api/pairing/bundle-summary` placeholder と、Rust 側 workflow の bundle summary staging を追記。理由: `runPairingSession()` の責務移行が「session metadata 受理」からさらに一段進んだことを別層仕様へ正確に反映するため。
- 2026-03-16: ESP32 AP 側 `POST /api/pairing/session` placeholder と、Rust 側 workflow の session metadata 登録後 `verifying` 手前までの到達点を追記。理由: `runPairingSession()` の責務移行がどこまで進んだかを別層仕様へ正確に反映するため。
- 2026-03-16: ESP32 AP 側 `GET /api/pairing/state` placeholder と、Rust 側 precheck からの参照を追記。理由: `waiting_device` 以降の監視導線がどこまで整ったかを別層仕様で追えるようにするため。
- 2026-03-16: Pairing workflow が AP モード `/api/auth/login` / `GET /api/settings/network` による到達・認証 precheck を Rust 側で実行する段階へ進んだことを追記。理由: 通信開始責務の Rust 移行がどこまで進んだかを別層仕様で追跡できるようにするため。
- 2026-03-16: `runPairingSession()` が `createPairingBundle` 相当の内部 helper 生成と `waiting_device` までの遷移を持つ段階へ進んだことを追記。理由: Pairing workflow の現在地を「受理のみ」ではなく「内部成果物生成済み」として正確に残すため。
- 2026-03-16: `SecretCore` に `run_pairing_session` placeholder workflow 骨格を追加したことに合わせ、`pairing/start` の現段階を「TS 窓口 + Rust 骨格実装済み」へ更新。理由: unknown-command 解消後の現在地を責任分担仕様へ正確に反映するため。
- 2026-03-16: `POST /api/workflows/pairing/start` を [進捗] へ更新し、TS 側 REST 窓口と事前検証実装済み、`SecretCore` 本体継続中の段階であることを追記。理由: 実装済みコードと責任分担仕様の説明が食い違わないようにするため。
- 2026-03-15: Stage7 の OTA workflow（`run_signed_ota_command` / `get_workflow_status`）と TS 側薄い窓口化を追記。理由: `003-0012` の高リスク通信workflow移行開始を文書へ同期するため。
- 2026-03-15: Stage6 の `wait_for_status_recovery` IPC と TS wait ループ委譲を追記。理由: `003-0012` の完了判定監視責務移行を文書へ同期するため。
- 2026-03-15: Stage5 の Rust `offline timeout` 判定と `deviceStateUpdated` を追記。理由: `003-0012` の状態遷移責務移行を文書へ同期するため。
- 2026-03-15: Stage4 の Rust `deviceState` 完成スナップショット統合を追記。理由: `003-0012` の状態統合責務移行を文書へ同期するため。
- 2026-03-15: Stage3 の Rust `k-device` 復号と最小DTO正規化を追記。理由: `003-0012` の受信payload処理移行開始を文書へ同期するため。
- 2026-03-15: Stage2 の Rust notice subscribe / 受信イベントキュー（`mqtt_start_receiver` / `mqtt_drain_events`）を追記。理由: `003-0012` の subscribe Rust化開始を文書へ同期するため。
- 2026-03-15: `mqtt_publish` による Stage1 の Rust MQTT command publish と、起動時 `SecretCore` readiness 待機を追記。理由: `003-0012` の最初の通信Rust化を文書へ同期するため。
- 2026-03-15: IPC 保護の現行実装（ランダム Pipe 名、32byte セッション鍵、AES-256-GCM、requestId リプレイ拒否、互換モード条件）を追記。理由: `003-0014` の実装実態を文書へ同期するため。
- 2026-03-15: `SecretCoreFacade` による境界IF固定、`003-0012` の [部分完了] 扱い、現時点で MQTT/OTA 通信スタックは TS 側担当である整理を追記。理由: 実装実態と最終要件の差分を明示しつつ、UI とセキュア部の境界を固定するため。
- 2026-03-13: 親定義参照から `LocalSoft` を除外。理由: `LocalSoft` 廃止方針に合わせ、別層仕様の責任境界を現行構成へ同期するため。
- 2026-03-10: `k-user-backup.enc` とパスワードによる別PC復旧経路を追加し、`wrapped_secret` 単独前提の復元制約を更新。理由: 冗長化・多重接続方針に合わせて復旧性を確保するため。
- 2026-03-09: `LocalServer` の workflow 公開 REST 経路（`/api/workflows/...`）と、`createPairingBundle` 直公開禁止を追加。理由: 外部公開 API と `SecretCore` 内部 helper の境界を実装前に明確化するため。
- 2026-03-09: `runPairingSession()` / `runKeyRotationSession()` / `runProductionSecureFlow()` / `runSignedOtaCommand()` と workflow 進捗状態を追加し、TS は開始要求と進捗表示中心へ更新。理由: 高リスク処理を Rust 側で通信開始から完了判定まで責任を持つ構成へ改めるため。
- 2026-03-09: `LocalServer` / `SecretCore` の責任範囲を `モジュール仕様書.md` に従って明確化し、`createPairingBundle` と AP モード ECDH 前提を追加。理由: 通常運用層と秘密処理層の境界を現行設計へ揃えるため。
- 2026-03-08: 鍵保護方式を `DPAPI/Keychain 直保存` から `TPM + wrapped_secret + HKDF` 方式へ全面改訂。理由: 別PCコピー防止と同一PC再インストール復旧の前提を SecretCore 設計へ統合するため。
- 2026-03-07: 初版作成。TS + Rust 構成、IPC傍受対策（DH鍵交換 + AES-GCM + nonce）、戻り値保護（raw key 非返却）、Rust難読化・ゼロ化方針、DPAPI/Keychain端末拘束方針、監査ログ仕様を定義。理由: LocalServerの別PC複製・ロジック解読・IPC傍受に対する防御境界を具体設計に落とし込むため。
