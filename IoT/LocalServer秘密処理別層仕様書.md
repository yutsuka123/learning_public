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

## 2. アーキテクチャ概要
```text
┌────────────────────────────────────────────────────────────┐
│ LocalServer（TypeScript / Node.js）                        │
│ Web UI / REST API / MQTT制御 / OTA配布 / 設定画面         │
│                                                            │
│  ↕ 保護済みIPC（Named Pipe 優先。将来 Unix Socket 対応）  │
│                                                            │
│ SecretCore（Rust）                                         │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ TPMで S_random を保護                                │  │
│  │ wrapped_secret 読込/検証                              │  │
│  │ k-user 生成                                           │  │
│  │ k-device 導出                                         │  │
│  │ HMAC / 署名 / AES-GCM payload生成                     │  │
│  │ 監査ログ記録                                          │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
         ↕ MQTT(TLS) / HTTPS(OTA)
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
### 4.1 TypeScript 本体（LocalServer）担当
- Web UI（デバイス一覧、OTA操作、設定画面）
- REST API
- WebSocket 配信
- MQTT 接続・購読・発行
- OTA ファームウェア配布（HTTPS）
- 公開設定管理
- SecretCore への IPC 依頼送信と結果受信

### 4.2 SecretCore（Rust）担当
- `S_random` の生成
- TPM による `wrapped_secret` 生成/復号
- `k-user` 生成
- `k-device` 導出
- HMAC / 署名生成
- AES-GCM payload 生成/検証補助
- `wrapped_secret` / `device_db` バックアップ支援
- 監査ログ記録

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
PipeName: \\.\pipe\IoT_SecretCore_<machineId>
DACL:
  - LocalServer 実行ユーザーSIDのみ ReadWrite 許可
  - Everyone 拒否
  - NETWORK SERVICE 拒否
```

- [厳守] `machineId` を Pipe 名へ含める。
- [厳守] SecretCore は LocalServer の子プロセスとして起動する。

### 6.3 IPC通信保護
- [厳守] IPC チャネルは AES-256-GCM + nonce + requestId で保護する。
- [厳守] `nonce` は再利用禁止とする。
- [厳守] `requestId` は `crypto.randomUUID()` 等の暗号学的乱数で生成する。
- [厳守] `timestamp` は ±30秒以内のみ受理する。

## 7. SecretCore の公開 API（限定）
### 7.1 許可 API
| 操作名 | 概要 | 返却値 |
|--------|------|--------|
| `initializeKeyHierarchy` | `S_random` 生成、TPMラップ、`wrapped_secret` 保存 | 成功/失敗 + schemaVersion |
| `isInitialized` | `wrapped_secret` の有無と利用可否確認 | bool |
| `signCommand` | 指定コマンドの HMAC / 署名を生成 | 署名済み結果 |
| `deriveEncryptedPayload` | `k-device` を使った暗号 payload を生成 | AES-GCM 暗号済み payload |
| `verifyInboundHmac` | 受信HMAC検証 | bool |
| `exportWrappedSecretBackup` | `wrapped_secret` のバックアップ準備 | ファイル出力結果 |
| `restoreWrappedSecretBackup` | `wrapped_secret` の復元 | 成功/失敗 |
| `getAuditLog` | 監査ログ取得 | 秘密値を含まない配列 |

### 7.2 禁止 API
- [禁止] `getUserKey()`
- [禁止] `getDeviceKey()`
- [禁止] `exportPlainSecret()`
- [禁止] 任意バイト列を自由に署名させる汎用 API

### 7.3 署名対象の固定
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
- `device_db`

### 8.2 バックアップ方針
- [厳守] `k-user` / `k-device` / `S_random` 平文はバックアップしない。
- [重要] `wrapped_secret` は別PCで復号できない。
- [推奨] `wrapped_secret` と `device_db` は別媒体へ退避する。

### 8.3 復元手順
1. `wrapped_secret` を元の専用配置先へ復元する。
2. `device_db` を復元する。
3. SecretCore が TPM で `wrapped_secret` を復号する。
4. `k-user` を再生成する。
5. `k-device` を都度再生成する。

### 8.4 復元制約
- [厳守] 同一PC + 同一TPM でのみ復元可能とする。
- [重要] TPM初期化 / TPM交換 / マザーボード交換時は再発行が必要である。

## 9. 監査ログ仕様
### 9.1 記録対象
| 操作 | ログ内容 |
|------|---------|
| `initializeKeyHierarchy` | 日時、schemaVersion、成否 |
| `TPM unwrap` | 日時、operation、成否 |
| `signCommand` | 日時、targetDevice、keyVersion |
| `deriveEncryptedPayload` | 日時、targetDevice、operation |
| `wrapped_secret` バックアップ | 日時、成否 |
| `wrapped_secret` 復元 | 日時、成否 |
| IPC 認証失敗 | 日時、詳細 |

- [厳守] 監査ログに raw key を含めない。
- [推奨] 監査ログファイル権限は SecretCore 実行ユーザーのみに制限する。

## 10. 解析耐性と限界
### 10.1 基本方針
- [重要] Kerckhoffs 原則に従い、ロジック秘匿ではなく `wrapped_secret` + TPM 拘束で守る。
- [推奨] Rust バイナリは Release + strip + LTO + zeroize を適用する。

### 10.2 AI/自動解析時の考え方
- ロジックが読まれても `wrapped_secret` を別PCで使えなければ不正利用しにくい。
- raw key を IPC 越しに返さないため、コード解析だけで実鍵取得はできない。
- 同一PCでのメモリダンプや管理者侵害は完全には防げない。

### 10.3 現時点の主要注意点
- [厳守] IPC ハンドシェイクは子プロセス spawn + stdin/stdout 連携を優先する。
- [厳守] TS 層で署名結果や暗号 payload を保持する時間を最短にする。
- [厳守] 乱数は Node.js `crypto` と Rust `OsRng` を使用する。
- [重要] `wrapped_secret` 破損時は復旧できないため、バックアップ運用を導入時に必須説明する。

## 11. 実装フェーズ（推奨順）
### Phase 1: 基盤
1. SecretCore 骨格（Rust）
2. `initializeKeyHierarchy` / `isInitialized`
3. TPM ラップ/アンラップ
4. Named Pipe + 暗号化 IPC
5. `signCommand`

### Phase 2: 暗号通信連携
1. `deriveEncryptedPayload`
2. ESP32 側 HMAC / payload 検証
3. `device_db` 連携

### Phase 3: 復旧運用
1. `wrapped_secret` バックアップ/復元
2. `device_db` バックアップ/復元
3. 監査ログ UI

### Phase 4: 完全化
1. 鍵再発行
2. 障害時再登録フロー
3. TPM初期化検知
4. 将来 macOS 対応の整理

## 12. 関連文書
- `鍵管理および初期セットアップ設計仕様書.md`
- `鍵管理初期セットアップ_実装たたき台.md`
- `セキュリティ方針_認証情報取扱い.md`
- `使用技術仕様書.md`

## 13. 変更履歴
- 2026-03-08: 鍵保護方式を `DPAPI/Keychain 直保存` から `TPM + wrapped_secret + HKDF` 方式へ全面改訂。理由: 別PCコピー防止と同一PC再インストール復旧の前提を SecretCore 設計へ統合するため。
- 2026-03-07: 初版作成。TS + Rust 構成、IPC傍受対策（DH鍵交換 + AES-GCM + nonce）、戻り値保護（raw key 非返却）、Rust難読化・ゼロ化方針、DPAPI/Keychain端末拘束方針、監査ログ仕様を定義。理由: LocalServerの別PC複製・ロジック解読・IPC傍受に対する防御境界を具体設計に落とし込むため。
