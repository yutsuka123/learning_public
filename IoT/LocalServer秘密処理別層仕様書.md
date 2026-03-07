# LocalServer 秘密処理別層仕様書（TS + Rust）

[重要] 本書は LocalServer の TypeScript 本体と、Rust 製秘密処理層（SecretCore）の設計方針・IPC仕様・保護要件を定義する。  
理由: 別PCへのコードコピー・ロジック解読・IPC傍受・戻り値盗み見を組み合わせて抑止するため。

---

## 1. 目的と脅威モデル

### 1.1 本設計で防ぎたいこと

| 脅威 | 概要 | 対策方針 |
|------|------|----------|
| コード複製 | TS/Rustソースを別PCへコピーして偽サーバーを立てる | K_user なしでは正規 HMAC/署名を生成できない構成にする |
| 設定ファイル抜き取り | settings.json / .env を読まれる | 秘密値をファイルへ保存しない。OSセキュアストレージへ閉じ込める |
| IPC傍受 | Named Pipe / Unix Socket のトラフィックを盗み見る | 共有鍵認証+暗号化+nonce でIPC自体を保護する |
| 戻り値盗み見 | SecretCore の戻り値を読み取る | raw key を返さない。使用済み結果のみ返す |
| ロジック解読 | Rust バイナリを逆アセンブルして秘密導出式を読む | 解析困難化・ストリップ・LLVM難読化で解析コストを上げる |
| 一般権限での参照 | 一般ユーザーが Pipe/Socket ファイルへアクセスする | OS ACL で接続元プロセス/ユーザーを制限する |

### 1.2 防ぎきれない前提（明文化）
- [重要] 運用PCの管理者権限が侵害された場合は、完全防御が困難である。
- [重要] 本設計の主眼は「別PCコピーによる不正利用」と「一般権限や第三者による参照」を防ぐことである。
- [補足] 管理者侵害は運用PC管理者の責任範囲として区分する。

---

## 2. アーキテクチャ概要

```
┌──────────────────────────────────────────────────────┐
│  LocalServer（TypeScript / Node.js）                  │
│                                                      │
│  Web UI │ REST API │ MQTT制御 │ OTA配布 │ 設定画面   │
│                                                      │
│  ↕ IPC（保護済み Named Pipe / Unix Domain Socket）   │
│                                                      │
│  SecretCore（Rust バイナリ）                          │
│  ┌──────────────────────────────────────────────┐    │
│  │ K_user 保管（OS保護領域）                     │    │
│  │ K_device 導出                                │    │
│  │ HMAC / 署名生成                              │    │
│  │ バックアップ暗号化/復元                       │    │
│  │ 鍵バージョン管理                             │    │
│  └──────────────────────────────────────────────┘    │
│                                                      │
│  OSセキュアストレージ（DPAPI / Keychain）              │
└──────────────────────────────────────────────────────┘
         ↕ MQTT(TLS) / HTTPS(OTA)
   ESP32 デバイス群（デバイス側でHMAC/署名検証）
```

---

## 3. TS本体と SecretCore の責務分担

### 3.1 TypeScript 本体（LocalServer）担当

- Web UI（デバイス一覧、OTA操作、設定画面）
- REST API（`/api/devices`、`/api/commands/*`、`/api/settings`）
- WebSocket 配信（リアルタイム更新）
- MQTT 接続・購読・発行（制御メッセージのオーケストレーション）
- OTA ファームウェア配布（HTTPS）
- 設定管理（settings.json: 公開設定のみ）
- 監査ログ表示
- `SecretCore` への IPC 依頼送信と結果受信

### 3.2 SecretCore（Rust）担当

- `K_user` の CSPRNG 生成（初回セットアップ時）
- `K_user` の OS セキュアストレージへの保存/読出し
  - Windows: DPAPI（Windows Data Protection API）
  - macOS: Keychain Services
- `K_device` 導出（都度、永続保存しない）
- HMAC / 署名 生成
- OTA 開始コマンドの署名
- 重要設定変更コマンドの署名
- バックアップファイルの暗号化/復元
- 鍵バージョン管理
- 操作別の監査ログ記録（ファイル書込み）

---

## 4. IPC設計

### 4.1 IPC方式

| OS | IPC種別 | 理由 |
|----|---------|------|
| Windows | Named Pipe | DACL でプロセス/ユーザー単位の接続制限が可能。ネットワーク不使用 |
| macOS | Unix Domain Socket | ファイルシステム権限で接続元を制限可能。ネットワーク不使用 |

- [禁止] TCP/HTTP で SecretCore を外部公開または localhost ポートへ待受させること。
- [禁止] 同一PCであっても WAN/LAN を介した通信経路を使うこと。
- 理由: 誤設定・ポートスキャン・同一PC内他プロセスからの到達可能性を排除するため。

### 4.2 IPC接続制限（ACL）

**Windows（Named Pipe）**
```
PipeName: \\.\pipe\IoT_SecretCore_<machineId>
DACL設定:
  - LocalServer Node.exeの実行ユーザーSIDのみ ReadWrite 許可
  - Everyone の接続拒否
  - NETWORK サービスの接続拒否
```

**macOS（Unix Domain Socket）**
```
SocketPath: /var/run/iot_secretcore_<machineId>.sock
パーミッション: 0600（所有者のみ読み書き）
所有者: LocalServer の実行ユーザー
```

- [厳守] SecretCore プロセス起動時に `machineId`（端末固有の識別子）を Pipe/Socket 名へ含める。  
  理由: 別端末コピー後に同名 Pipe を作れないようにするため（相対的な区別化）。

### 4.3 IPC通信の暗号化・認証

- [厳守] IPC チャネル自体も保護する。Named Pipe / Unix Socket の通信内容を暗号化する。
- 方式: **共有セッション鍵による AES-256-GCM 暗号化 + HMAC 認証 + nonce**

#### IPC 保護フロー

```
1. SecretCore 起動時:
   - セッション用一時鍵ペア（X25519）を生成
   - Public key を Pipe/Socket 経由で LocalServer へ通知

2. LocalServer 接続時:
   - 自分の一時 Public key を提示
   - DH 鍵交換で共有シークレット導出
   - 両者で AES-256-GCM セッション鍵を持つ

3. IPC リクエスト:
   {
     requestId: UUID v4,
     timestamp: Unix時刻(ms),
     nonce: ランダム16bytes(hex),
     operation: "signCommand" | "derivePayload" | ...,
     payload: AES-256-GCM 暗号化済み引数
   }

4. IPC レスポンス:
   {
     requestId: 対応するリクエストID,
     timestamp: Unix時刻(ms),
     nonce: レスポンスnonce,
     result: AES-256-GCM 暗号化済み結果（raw key は含まない）,
     hmac: リクエストID+timestamp+nonce+result の HMAC
   }
```

- [厳守] `nonce` は再利用禁止。同一 nonce を受信した場合は拒否する。
- [厳守] `timestamp` は ±30秒以内のみ受理し、リプレイ攻撃を防ぐ。
- [厳守] `requestId` はリクエスト/レスポンスペアの照合に使い、応答順序攻撃を防ぐ。
- [厳守] `hmac` 検証失敗時は即座に接続を切断し、監査ログへ記録する。

---

## 5. SecretCore の公開 API（限定）

### 5.1 許可 API

| 操作名 | 概要 | 返却値 |
|--------|------|--------|
| `initialize` | 初回セットアップ時に K_user を生成・保存する | 成功/失敗 + keyVersion |
| `isInitialized` | SecretCore が初期化済みか確認する | bool |
| `signCommand` | 指定コマンドの HMAC 署名を生成する | 署名済みコマンドJSON（K_user/K_device は非返却） |
| `deriveEncryptedPayload` | デバイスへ送る暗号 payload を生成する | AES-GCM 暗号済み payload |
| `verifyInboundHmac` | ESP32から受信したHMACを検証する | bool（K_user は非返却） |
| `createBackup` | K_user を passphrase で暗号化してバックアップする | 暗号化ファイルパス |
| `restoreBackup` | バックアップファイルと passphrase で K_user を復元する | 成功/失敗 |
| `rotateKey` | K_user をローテーションし、keyVersion を更新する | 新 keyVersion |
| `revokeKey` | 鍵を失効させる | 成功/失敗 |
| `getAuditLog` | 操作監査ログ（末尾N件）を返す | 監査ログ配列（秘密値は非含有） |

### 5.2 禁止 API（絶対に実装しない）

- [禁止] `getMasterKey()` / `getKUser()`
- [禁止] `getDeviceKey()` / `getKDevice()`
- [禁止] `exportPlainKey()`
- [禁止] 任意バイト列のHMAC（operation/purpose を固定せず何でも署名できる API）
- 理由: これらを実装すると TS 層が生の秘密を受け取ることになり、分離の意味がなくなるため。

### 5.3 `signCommand` の署名対象（固定）

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

- [厳守] 署名対象の構成を固定し、任意フィールド追加を禁止する。
- 理由: TS 側で署名対象を自由に組み立てられる状態は、別操作への流用を招くため。

---

## 6. SecretCore（Rust）解析耐性方針

### 6.1 解析困難化手法

| 手法 | 適用 |
|------|------|
| Release ビルド + strip | デバッグシンボル除去（`cargo build --release` + `strip`） |
| LLVM 難読化 | `obfuscator-llvm` または `rustc` の `-C opt-level=3` + LTO |
| panic メッセージ削除 | `panic = "abort"` + `panic_immediate_abort` |
| 文字列難読化 | `obfstr` クレートで定数文字列をコンパイル時に暗号化 |
| スタック変数のゼロ化 | `zeroize` クレートで秘密値を使用後に即座にゼロ埋め |
| シンボル削除 | `strip = "symbols"` in Cargo.toml |
| ビルド時インライン展開 | 鍵導出関係の関数を `#[inline(always)]` で外から追いにくくする |

### 6.2 難読化の限界（明文化）
- [重要] 十分な時間とリソースがある攻撃者には、バイナリ解析で導出式が読まれる可能性がある。
- [重要] 難読化の目的は「解析コストを上げる」であり「完全秘匿」ではない。
- [推奨] 鍵導出式・HMAC構成・nonce生成方式は公開しても安全な設計にしておく（Kerckhoffs 原則）。
  - 理由: 安全性の根拠を「ロジック秘匿」ではなく「K_user 秘匿」に置くため。

### 6.3 重要なゼロ化対象
- K_user の使用後（HMAC計算後、DPAPI/Keychain書込後）
- K_device の使用後（暗号化後、HMAC計算後）
- パスフレーズ（バックアップ/復元後）
- IPC セッション鍵（セッション終了時）

---

## 7. Windows / macOS OS保護領域の利用方針

### 7.1 Windows（DPAPI / Credential Manager）

```
保存方法:
  1. CryptProtectData API（DPAPI）で K_user の 32 bytes をローカルユーザー拘束で暗号化
  2. 暗号化済みバイナリを Credential Manager もしくは専用ファイルへ保存
  3. ファイルパスは APPDATA 直下の専用サブディレクトリ（権限 700）

読み出し:
  SecretCore が CryptUnprotectData で復号。
  TS本体へは平文を返さない。

ポイント:
  DPAPI は「そのWindowsユーザーセッション内でしか復号できない」ため、
  別PCへファイルごとコピーしても復号不可。
```

### 7.2 macOS（Keychain Services）

```
保存方法:
  Keychain に kSecClassGenericPassword として保存
  kSecAttrService: "io.esplab.localserver"
  kSecAttrAccount: "k_master_user_<keyVersion>"
  kSecAttrAccessible: kSecAttrAccessibleWhenUnlockedThisDeviceOnly
  ※ ThisDeviceOnly は「この端末のハードウェアで拘束」のため別PCへコピー不可

読み出し:
  SecretCore が SecItemCopyMatching で取得。
  TS本体へは平文を返さない。
```

- [厳守] `kSecAttrAccessibleWhenUnlockedThisDeviceOnly` を必ず使用する。
  - `kSecAttrAccessibleAlways` や iCloud 同期を使わない。
  - 理由: 端末拘束でないと別デバイスへ秘密が移動するため。

---

## 8. バックアップ/復元設計

### 8.1 バックアップ形式

```
backup_v1.iolb:
  header:
    magicBytes: 0x494F4C42 ("IOLB")
    version: 1
    keyVersion: <uint32>
    createdAt: <unix時刻ms>
    machineIdHash: SHA256(machineId) の先頭8bytes（任意）
  body:
    salt: ランダム32bytes
    nonce: ランダム12bytes
    ciphertext: AES-256-GCM暗号化した K_user（32bytes）
    tag: GCM認証タグ
```

- 暗号鍵 = `Argon2id(passphrase, salt, m=65536, t=3, p=1)` で 32bytes 導出
- [厳守] passphrase はメモリ内でのみ使い、使用後即座にゼロ化する。
- [厳守] バックアップファイルには K_user の raw value を含めない（暗号化のみ）。
- [推奨] バックアップファイルは別媒体（USBメモリ等）に保管し、LocalServerフォルダへ置かない。

### 8.2 復元手順

1. SecretCore の `restoreBackup` を呼ぶ
2. バックアップファイルパス + passphrase を渡す（IPC暗号化通信で）
3. SecretCore がパスフレーズで復号 → K_user を取り出す
4. OS 保護領域へ再保存
5. 成功/失敗のみ TS へ返す（K_user は渡さない）

---

## 9. 監査ログ仕様

### 9.1 記録対象操作

| 操作 | ログ内容 |
|------|---------|
| K_user 生成 | 日時、keyVersion、OS種別 |
| K_user 読出し | 日時、リクエスト元PID、operation |
| バックアップ作成 | 日時、keyVersion |
| バックアップ復元 | 日時、成否 |
| signCommand | 日時、operation、targetDevice、keyVersion |
| rotateKey | 日時、旧keyVersion、新keyVersion |
| revokeKey | 日時、keyVersion |
| IPC 認証失敗 | 日時、詳細（nonce重複/タイムスタンプ逸脱/HMAC不一致） |
| IPC 接続拒否 | 日時、接続元情報 |

- [厳守] 監査ログに K_user / K_device の raw value を含めない。
- [推奨] 監査ログファイルは SecretCore が直接書き込む。TS 本体は参照のみ。
- [推奨] ログファイル自体にも改ざん検知用の HMAC チェーンを付ける。

---

## 10. SecretCore プロセス起動/停止方針

- [重要] SecretCore は LocalServer 起動時に子プロセスとして起動し、LocalServer 停止時に連動停止する。
- [厳守] SecretCore は常時バックグラウンド待受せず、LocalServer プロセスの生存に連動する。
  - 理由: 単独で常駐すると攻撃対象になりやすいため。
- [推奨] SecretCore は起動時に `machineId` を確認し、実行端末と Pipe/Socket 名の整合を検証する。
- [推奨] TS 本体は SecretCore の PID を記録し、想定外の終了を検知した場合は操作を停止する。

---

## 11. Cargo.toml 設計方針（Rust）

```toml
[profile.release]
opt-level = 3
lto = true
codegen-units = 1
panic = "abort"
strip = "symbols"
debug = false

[dependencies]
zeroize = { version = "1", features = ["derive"] }
aes-gcm = "0.10"
hmac = "0.12"
sha2 = "0.10"
argon2 = "0.5"
x25519-dalek = "2"
rand = "0.8"
serde = { version = "1", features = ["derive"] }
serde_json = "1"
obfstr = "0.4"

[target.'cfg(windows)'.dependencies]
windows = { version = "0.57", features = ["Win32_Security_Cryptography"] }

[target.'cfg(target_os = "macos")'.dependencies]
security-framework = "2"
```

---

## 12. 実装フェーズ（推奨順）

### Phase 1: 基盤（優先）
1. SecretCore 骨格（Rust）
   - IPC サーバー（Pipe/Socket + 暗号化）
   - `initialize` / `isInitialized`
   - K_user 生成・DPAPI 保存（Windows）
   - `signCommand`（OTA用）

2. TS 側 SecretCoreClient
   - IPC 接続・暗号化通信
   - `initialize` / `signCommand` 呼出し

### Phase 2: macOS対応 + バックアップ
1. Keychain 保存実装（macOS）
2. `createBackup` / `restoreBackup`
3. バックアップ UI（settings.html）

### Phase 3: 完全化
1. `rotateKey` / `revokeKey`
2. 監査ログ UI
3. HMACチェーン付き監査ログ
4. K_device 導出・ペアリング連携

---

---

## 13. AIツール・自動解析への対策と設計上の注意点（2026-03-07 追記）

[重要] 本章は現在の設計に存在する脆弱箇所・AI解析耐性の限界・代替案を記録する。  
理由: AIを用いた自動バイナリ解析・コード生成攻撃・サイドチャネル解析が現実的な脅威になっているため。

---

### 13.1 現在の設計で脆弱な箇所と対策

#### 【脆弱点①】IPC ハンドシェイク（X25519公開鍵）が認証なしで交換される

**問題:**
```
SecretCore 起動直後に X25519 Public Key を Pipe に書き出し、
LocalServer はそれを「無条件に信頼」する設計になっている。
→ 攻撃者が先に偽の SecretCore を起動し、偽公開鍵をPipeへ書き込めば、
  LocalServerが偽 SecretCore と鍵交換してしまう（Man-in-the-Middle）。
```

**対策（推奨）:**
- [推奨] SecretCore を LocalServer の子プロセスとして `spawn` し、
  Public Key を **標準入出力（stdin/stdout）** 経由で渡す。
  → Pipeへの外部プロセスの割り込みを排除できる。
- [代替案] SecretCore 初回起動時にシステム固有値（machineId + ユーザーSID）から
  静的ブートストラップ秘密を導出し、ハンドシェイクを認証済みにする。
- [注意] Named Pipe への事前割り込みは Windows では DACL で制限できるが、
  DACL設定前の競合状態（TOCTOU）がわずかに存在する。起動シーケンスを単純化する設計が安全。

---

#### 【脆弱点②】AI（LLM）によるバイナリ逆アセンブル + 鍵導出ロジック再現

**問題:**
```
obfstr + LLVM難読化は「人間の読解コスト」を上げるが、
AIを使った自動逆アセンブル→疑似コード生成→導出式再現に対しては有効性が低下している。
特に HMAC-SHA256 + 固定ストリングの組み合わせは、
バイナリ検索でアルゴリズムパターンが特定されやすい。
```

**対策（推奨）:**
- [重要] **設計をKerckhoffs原則で守る**: ロジックが読まれても K_user がなければ鍵導出できない。
  → 難読化に頼らず「K_user 秘密性」を唯一の安全根拠とする。
- [推奨] K_device 導出に **purpose binding（用途固定）** を必ず入れる:
  ```
  K_device_ota = HMAC-SHA256(K_user, "ota:" + deviceId + ":" + keyVersion)
  K_device_wifi = HMAC-SHA256(K_user, "wifi:" + deviceId + ":" + keyVersion)
  ```
  → 導出式が解読されても「OTA用鍵でWi-Fi設定を複合できない」分離が維持される。
- [推奨] 導出式の purpose prefix を **長めの固定文字列**（32文字以上）にし、
  短い文字列比較での特定を難しくする。

---

#### 【脆弱点③】Node.js プロセスのメモリダンプ攻撃

**問題:**
```
SecretCore から TS 層へ返却する「署名済みコマンドJSON」や「AES暗号payload」は
Node.js プロセスのヒープに一時的に存在する。
管理者権限のあるツール（winpmem, procdump, lldb など）でメモリダンプを取れば
平文で参照できる可能性がある。
```

**対策（推奨）:**
- [推奨] TS層でIPCから受け取った暗号payload/署名は **即座にMQTT送信し、変数参照を解除** する。
  長時間メモリに保持しない。
- [推奨] SecretCore 側でも `zeroize` を徹底し、署名計算の中間値をスタック上に残さない。
- [注意] Node.js には `Buffer.fill(0)` はあるが、GCタイミングで「ゼロ化前に回収」される場合がある。
  機密度の高いデータは TypeScript 層で保持する時間を最短にすること。
- [代替案より現実的な対策] 「何を署名しているか」を ESP32側で厳格検証することで、
  たとえ署名が傍受・再利用されても別コマンドへの転用を防ぐ。

---

#### 【脆弱点④】DPAPI の「ユーザーセッション」前提の穴

**問題:**
```
Windows DPAPI は「同じユーザーセッションなら復号できる」仕様。
→ 同一 PC・同一ユーザーでログインしている別プロセス（マルウェア等）が
  DPAPI 復号を呼べる可能性がある。
```

**対策（推奨）:**
- [推奨] DPAPI に加えて、SecretCore 自身で **追加ローカルパスワード（インストール時生成）** で
  二重暗号化する:
  ```
  保存データ = DPAPI_encrypt( AES-256-GCM_encrypt(K_user, localInstallSecret) )
  ```
  `localInstallSecret` は SecretCore バイナリと同ディレクトリの権限制限ファイルに保存。
  → 同一ユーザーセッションの別プロセスがDPAPIを呼んでも内部AES層が残る。
- [注意] `localInstallSecret` はGitや設定管理に含めない。インストール時にCSPRNGで生成する。

---

#### 【脆弱点⑤】バックアップパスフレーズの強度

**問題:**
```
Argon2id を使っているが、ユーザーが短い・単純なパスフレーズを設定した場合、
オフライン辞書攻撃でバックアップファイルが解読されるリスクがある。
```

**対策（推奨）:**
- [厳守] パスフレーズの最低強度チェックを実装する（最低16文字、記号混在推奨）。
- [推奨] Argon2id パラメータを `m=131072（128MB）, t=4, p=2` 以上に引き上げる。
  現在の `m=65536` は2026年時点でやや低い。
- [推奨] バックアップファイルに試行回数制限（ロックアウト）は組み込めないが、
  **バックアップファイルの保管場所を暗号化ディスク（BitLocker/FileVault）にする**
  ことを `取扱説明書.md` に明記する。
- [代替案] パスフレーズ方式ではなく **ハードウェアキー（YubiKey HMAC-SHA1）を復元要素**
  にする設計も将来検討する。

---

#### 【脆弱点⑥】ESP32側の HMAC 検証が将来未実装になるリスク

**問題:**
```
現在の OTA は SHA256 検証のみ。HMAC 署名検証は「SecretCore Phase1完了後」待ちとなっており、
その間は LocalServer から任意の OTA コマンドを受け付けてしまう暫定状態が続く。
```

**対策（推奨）:**
- [重要] HMAC 未実装期間は「信頼できるネットワーク内のみで OTA を行う」運用制限を `取扱説明書.md` に明記する。
- [厳守] SecretCore Phase1 完了後は HMAC 検証を ESP32 側に**必ず実装してから**、
  OTA を外部ネットワーク経由で行う段階へ進む。
- [禁止] 「HMAC 未検証のまま外部公開 OTA を行う」運用。

---

#### 【脆弱点⑦】IPC の requestId / nonce の乱数品質

**問題:**
```
requestId（UUID v4）は Node.js の `crypto.randomUUID()` または `uuid` パッケージで生成するが、
実装によっては Math.random() ベースの非暗号学的乱数を使う場合がある。
nonce も同様に、品質の低い乱数を使うと nonce 再利用・予測のリスクがある。
```

**対策（厳守）:**
- [厳守] requestId は `crypto.randomUUID()` （Node.js 内蔵の暗号学的乱数）を使う。`uuid` パッケージの v4 も可。
- [厳守] nonce は `crypto.randomBytes(16)` で生成する。`Math.random()` は**絶対に使わない**。
- [厳守] Rust 側の nonce / salt は `rand::rngs::OsRng` を使う。`thread_rng()` は CSPRNG だが
  OS乱数ソースに依存しないため、OS起動直後の乱数品質低下リスクがある環境では `OsRng` を優先する。

---

#### 【脆弱点⑧】監査ログの改ざん検知が推奨止まり

**問題:**
```
HMACチェーン付き監査ログは「推奨」になっているが、
ログが改ざんされると不正操作の痕跡が消せてしまう。
```

**対策（推奨 → 重要へ格上げ）:**
- [重要] 監査ログのHMACチェーンは Phase3 で実装するが、
  **Phase1 からログファイルのパーミッションを 0600（SecretCore実行ユーザーのみ）に制限する**。
- [推奨] ログをローカルファイルに加えて、**Windows イベントログ / macOS syslog へも書き込む**。
  → ローカルファイルが削除されても OS レベルのログが残る。
- [将来対応] クラウド移行時はログを外部 SIEM へ転送する。

---

#### 【脆弱点⑨】machineId の衝突・偽造リスク

**問題:**
```
machineId を Named Pipe / Socket 名に使っているが、
machineId の導出方法によっては「仮想環境でmachineIdを偽装」できる場合がある。
```

**対策（推奨）:**
- [推奨] machineId は `ユーザーSID + マザーボードUUID + インストール時生成UUID` の複合値の
  SHA256ハッシュとし、単一値への依存を避ける。
- [注意] machineId の主目的は「Pipe/Socket 名の一意化」であり「認証」ではない。
  実際の認証は IPC チャネルの DH鍵交換 + HMAC に依存する。

---

### 13.2 AI解析耐性についての総評

```
現在の設計で「AI による自動解析に対して有効な防御」の要点:

  ✅ 有効:
    - K_user を OS 保護領域に閉じ込め、バイナリに含めない
    - K_device を永続保存しない（都度導出）
    - raw key を IPC 越しに返さない（AIがコード解析しても鍵の値を得られない）
    - Kerckhoffs 原則準拠（ロジック公開でも K_user がなければ無意味）

  ⚠️ 限界（AI でも防ぎにくい）:
    - DPAPI 復号は同一ユーザーセッションの「正規プロセス」として実行すれば取得できる
      → AI がマルウェアコードを生成し、同一ユーザーとして実行された場合は防げない
    - Node.js ヒープのメモリダンプ
      → OS の Anti-Malware / EDR に依存する部分が残る

  ❌ 対策困難（受け入れ前提）:
    - 管理者権限を奪った攻撃者が同一PC上でデバッガをアタッチする
    - 量子コンピュータによる AES/HMAC への将来的な攻撃（2030年以降の懸念）
      → 将来 post-quantum アルゴリズム（CRYSTALS-Kyber等）への移行を検討する
```

---

### 13.3 今後の実装で注意すべき点チェックリスト

- [ ] IPC ハンドシェイクは子プロセス化 + stdin/stdout 渡しを優先検討する
- [ ] K_device 導出の purpose binding を必ず入れる（用途ごとに別鍵）
- [ ] TS 層で機密データを保持する時間を最短にする（受け取り即送信）
- [ ] nonce は必ず `crypto.randomBytes(16)` で生成する（Math.random 禁止）
- [ ] Argon2id パラメータを `m=131072` 以上に引き上げる
- [ ] パスフレーズ強度チェックを実装する
- [ ] DPAPI + 追加ローカルAES二重暗号化を検討する
- [ ] 監査ログは Phase1 からパーミッション制限 + OS ログへの二重書きを行う
- [ ] HMAC 未検証期間の OTA は信頼ネットワーク内限定を取扱説明書に明記する
- [ ] 将来の post-quantum 移行計画を todo に追記する

---

## 14. 関連文書
- `セキュリティ方針_認証情報取扱い.md`
- `鍵管理および初期セットアップ設計仕様書.md`
- `使用技術仕様書.md`
- `鍵管理初期セットアップ_実装たたき台.md`

## 14. 変更履歴
- 2026-03-07: AI解析耐性・脆弱点分析・代替案を第13章として追加。脆弱点9項目（IPC MITM・AIバイナリ解析・Node.jsメモリダンプ・DPAPI同一ユーザー穴・Argon2idパラメータ・HMAC未実装暫定期・乱数品質・監査ログ改ざん・machineId偽造）とチェックリストを記載。理由: AIを使った自動解析・コード生成攻撃が現実的な脅威となっているため、設計上の弱点を先行的に文書化するため。
- 2026-03-07: 初版作成。TS + Rust 構成、IPC傍受対策（DH鍵交換 + AES-GCM + nonce）、戻り値保護（raw key 非返却）、Rust難読化・ゼロ化方針、DPAPI/Keychain端末拘束方針、監査ログ仕様を定義。理由: LocalServerの別PC複製・ロジック解読・IPC傍受に対する防御境界を具体設計に落とし込むため。
