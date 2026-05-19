// key_manager.rs - SecretCore の鍵管理モジュール。
//
// [重要] k-user は2つの経路で取得可能:
//   (a) s_random → HKDF 導出（新規発行時）
//   (b) wrapped_k_user.bin からDPAPI復号（TS版からのインポート時）
// [厳守] wrapped_k_user.bin が存在する場合は (b) を優先する。
// [厳守] k-user の平文をファイル保存しない（DPAPI暗号化のみ）。
// 変更日: 2026-05-11 wrapped_secret に version / createdAt / integrity を追加。理由: 008-0013 対応のため。

use crate::dpapi;
use aes_gcm::aead::{AeadInPlace, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use base64::Engine;
use chrono::DateTime;
use hkdf::Hkdf;
use hmac::{Hmac, Mac};
use rand::Rng;
use scrypt::{Params as ScryptParams, scrypt};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::fs;
use std::path::Path;
use std::sync::Mutex;

const WRAPPED_SECRET_PATH: &str = "../LocalServer/data/wrapped_secret.bin";
const WRAPPED_K_USER_PATH: &str = "../LocalServer/data/wrapped_k_user.bin";
const DEFAULT_K_USER_BACKUP_PATH: &str = "../LocalServer/data/k_user_backup.enc.json";
const BACKUP_FILE_VERSION: u32 = 1;
const BACKUP_KDF_N_LOG2: u8 = 15;
const BACKUP_KDF_R: u32 = 8;
const BACKUP_KDF_P: u32 = 1;
const BACKUP_KDF_SALT_BYTES: usize = 16;
const WRAPPED_SECRET_FILE_VERSION: u32 = 1;
const WRAPPED_SECRET_ALG: &str = "DPAPI";

#[derive(Debug, Serialize, Deserialize)]
struct KUserBackupKdfParams {
    n_log2: u8,
    r: u32,
    p: u32,
    key_len: usize,
    salt_base64: String,
}

#[derive(Debug, Serialize, Deserialize)]
struct KUserBackupEncrypted {
    iv_base64: String,
    cipher_base64: String,
    tag_base64: String,
}

#[derive(Debug, Serialize, Deserialize)]
struct KUserBackupEnvelope {
    version: u32,
    alg: String,
    kdf: String,
    kdf_params: KUserBackupKdfParams,
    encrypted: KUserBackupEncrypted,
    key_fingerprint: String,
    exported_at: String,
}

#[derive(Debug, Serialize, Deserialize)]
struct WrappedSecretEnvelope {
    version: u32,
    created_at: String,
    alg: String,
    payload_base64: String,
    payload_sha256_base64: String,
}

/// 鍵管理構造体。s_random による HKDF 導出とインポート済み k-user の双方に対応する。
pub struct KeyManager {
    s_random: [u8; 32],
    /// インポート済み k-user（wrapped_k_user.bin から読み込み、またはランタイムでインポート）。
    /// Mutex で保護し、import_k_user() でのランタイム更新に対応する。
    imported_k_user: Mutex<Option<[u8; 32]>>,
}

impl KeyManager {
    /// KeyManager を初期化する。
    /// wrapped_secret.bin が存在すれば s_random を復元し、なければ新規生成する。
    /// wrapped_k_user.bin が存在すれば、インポート済み k-user として読み込む。
    pub fn new() -> Result<Self, String> {
        let s_random = Self::load_or_create_s_random()?;
        let imported_k_user = Self::try_load_imported_k_user();
        Ok(Self {
            s_random,
            imported_k_user: Mutex::new(imported_k_user),
        })
    }

    /// s_random をロードまたは新規生成する。
    fn load_or_create_s_random() -> Result<[u8; 32], String> {
        let path = Path::new(WRAPPED_SECRET_PATH);
        if path.exists() {
            let encrypted = fs::read(path).map_err(|e| format!("Read error: {}", e))?;
            let decrypted = Self::decode_wrapped_secret_blob(&encrypted)?;
            if decrypted.len() != 32 {
                return Err("Invalid S_random length".to_string());
            }
            let mut s_random = [0u8; 32];
            s_random.copy_from_slice(&decrypted);
            Ok(s_random)
        } else {
            let mut s_random = [0u8; 32];
            rand::rng().fill_bytes(&mut s_random);
            let encrypted = Self::create_wrapped_secret_blob(&s_random)?;
            if let Some(parent) = path.parent() {
                let _ = fs::create_dir_all(parent);
            }
            fs::write(path, encrypted).map_err(|e| format!("Write error: {}", e))?;
            Ok(s_random)
        }
    }

    /// wrapped_k_user.bin からインポート済み k-user を読み込む（存在しなければ None）。
    fn try_load_imported_k_user() -> Option<[u8; 32]> {
        let path = Path::new(WRAPPED_K_USER_PATH);
        if !path.exists() {
            return None;
        }
        let encrypted = fs::read(path).ok()?;
        let decrypted = Self::decode_wrapped_secret_blob(&encrypted).ok()?;
        if decrypted.len() != 32 {
            eprintln!("Warning: wrapped_k_user.bin has invalid length {}. Ignoring.", decrypted.len());
            return None;
        }
        let mut k_user = [0u8; 32];
        k_user.copy_from_slice(&decrypted);
        Some(k_user)
    }

    /// k-user を返す。
    /// [重要] wrapped_k_user.bin（インポート済み）があればそれを優先。
    /// なければ s_random → HKDF で導出する。
    pub fn get_k_user(&self) -> [u8; 32] {
        let guard = self.imported_k_user.lock().unwrap();
        if let Some(k_user) = *guard {
            return k_user;
        }
        drop(guard);
        let hk = Hkdf::<Sha256>::new(Some(b"LocalServer"), &self.s_random);
        let mut k_user = [0u8; 32];
        hk.expand(b"k-user-v1", &mut k_user).expect("HKDF failed");
        k_user
    }

    /// 新しい k-user をランダム生成し、DPAPI 保護で永続化する。
    ///
    /// [重要] 既存値が存在しても上書きし、以後の k-device 導出は新しい k-user 系統に切り替わる。
    /// [厳守] 平文 k-user は戻り値へ含めず、fingerprint のみ返す。
    pub fn issue_new_k_user(&self) -> Result<String, String> {
        let mut k_user_bytes = [0u8; 32];
        rand::rng().fill_bytes(&mut k_user_bytes);
        self.import_k_user(&k_user_bytes)?;
        Ok(Self::create_fingerprint_hex(&k_user_bytes))
    }

    /// 外部から k-user (Base64) を受け取り、DPAPI 保護して永続化する。
    /// ランタイムの k-user も即時更新する。
    pub fn import_k_user(&self, k_user_bytes: &[u8]) -> Result<(), String> {
        if k_user_bytes.len() != 32 {
            return Err(format!("Invalid k-user length: {} (expected 32)", k_user_bytes.len()));
        }
        let encrypted = dpapi::protect_data(k_user_bytes)?;
        let path = Path::new(WRAPPED_K_USER_PATH);
        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }
        fs::write(path, encrypted).map_err(|e| format!("Write wrapped_k_user.bin error: {}", e))?;

        let mut guard = self.imported_k_user.lock().unwrap();
        let mut k_user = [0u8; 32];
        k_user.copy_from_slice(k_user_bytes);
        *guard = Some(k_user);
        Ok(())
    }

    /// k-user のソースを返す。デバッグ／管理用。
    pub fn get_k_user_source(&self) -> &'static str {
        let guard = self.imported_k_user.lock().unwrap();
        if guard.is_some() {
            "imported (wrapped_k_user.bin)"
        } else {
            "derived (s_random -> HKDF)"
        }
    }

    /// k-user の fingerprint（先頭16桁hex）を返す。
    pub fn get_k_user_fingerprint(&self) -> String {
        let k_user = self.get_k_user();
        Self::create_fingerprint_hex(&k_user)
    }

    /// k-user バックアップ暗号化キーを導出する。
    fn derive_backup_key(
        backup_password: &str,
        salt: &[u8],
        n_log2: u8,
        r: u32,
        p: u32,
        key_len: usize,
    ) -> Result<[u8; 32], String> {
        if backup_password.is_empty() {
            return Err("derive_backup_key failed. backup_password is empty.".to_string());
        }
        if key_len != 32 {
            return Err(format!("derive_backup_key failed. unsupported key_len={}", key_len));
        }
        let params = ScryptParams::new(n_log2, r, p, key_len)
            .map_err(|e| format!("derive_backup_key failed. params error={}", e))?;
        let mut key_bytes = [0u8; 32];
        scrypt(backup_password.as_bytes(), salt, &params, &mut key_bytes)
            .map_err(|e| format!("derive_backup_key failed. scrypt error={}", e))?;
        Ok(key_bytes)
    }

    /// キーフィンガープリントを生成する。
    fn create_fingerprint_hex(key_bytes: &[u8]) -> String {
        use sha2::Digest;
        let digest = Sha256::digest(key_bytes);
        hex::encode(&digest[0..8])
    }

    /// wrapped_secret の保存データを作成する。
    ///
    /// [重要] DPAPI で保護した後に version / createdAt / integrity を付与する。
    fn create_wrapped_secret_blob(secret_bytes: &[u8; 32]) -> Result<Vec<u8>, String> {
        let protected_bytes = dpapi::protect_data(secret_bytes)?;
        let integrity_digest = Sha256::digest(&protected_bytes);
        let envelope = WrappedSecretEnvelope {
            version: WRAPPED_SECRET_FILE_VERSION,
            created_at: chrono::Utc::now().to_rfc3339(),
            alg: WRAPPED_SECRET_ALG.to_string(),
            payload_base64: base64::prelude::BASE64_STANDARD.encode(&protected_bytes),
            payload_sha256_base64: base64::prelude::BASE64_STANDARD.encode(integrity_digest),
        };
        serde_json::to_vec_pretty(&envelope)
            .map_err(|e| format!("create_wrapped_secret_blob failed. serialize error={}", e))
    }

    /// wrapped_secret を読み込み、整合性を検証して DPAPI 復号する。
    ///
    /// [厳守] 新形式は version / createdAt / SHA-256 を必ず検証する。
    /// [旧仕様] 旧形式の raw DPAPI blob は互換のため読み込みだけ許可する。
    fn decode_wrapped_secret_blob(wrapped_secret_blob: &[u8]) -> Result<Vec<u8>, String> {
        match serde_json::from_slice::<WrappedSecretEnvelope>(wrapped_secret_blob) {
            Ok(envelope) => Self::decode_wrapped_secret_envelope(envelope),
            Err(_) => {
                eprintln!("Warning: wrapped_secret.bin is legacy raw DPAPI blob. integrity metadata is unavailable.");
                dpapi::unprotect_data(wrapped_secret_blob)
            }
        }
    }

    /// 新形式 wrapped_secret の整合性チェックを行う。
    fn decode_wrapped_secret_envelope(envelope: WrappedSecretEnvelope) -> Result<Vec<u8>, String> {
        if envelope.version != WRAPPED_SECRET_FILE_VERSION {
            return Err(format!(
                "wrapped_secret version mismatch. expected={} actual={}",
                WRAPPED_SECRET_FILE_VERSION, envelope.version
            ));
        }
        if envelope.alg != WRAPPED_SECRET_ALG {
            return Err(format!(
                "wrapped_secret algorithm mismatch. expected={} actual={}",
                WRAPPED_SECRET_ALG, envelope.alg
            ));
        }
        if envelope.created_at.trim().is_empty() {
            return Err("wrapped_secret created_at is empty.".to_string());
        }
        let created_at = DateTime::parse_from_rfc3339(&envelope.created_at).map_err(|e| {
            format!(
                "wrapped_secret created_at parse failed. createdAt={} detail={}",
                envelope.created_at, e
            )
        })?;
        if created_at.timestamp() <= 0 {
            return Err(format!(
                "wrapped_secret created_at is invalid. createdAt={}",
                envelope.created_at
            ));
        }
        let protected_bytes = base64::prelude::BASE64_STANDARD
            .decode(&envelope.payload_base64)
            .map_err(|e| format!("wrapped_secret payload decode failed. detail={}", e))?;
        let expected_digest = base64::prelude::BASE64_STANDARD
            .decode(&envelope.payload_sha256_base64)
            .map_err(|e| format!("wrapped_secret integrity decode failed. detail={}", e))?;
        let actual_digest = Sha256::digest(&protected_bytes);
        if expected_digest.as_slice() != actual_digest.as_slice() {
            return Err("wrapped_secret integrity mismatch.".to_string());
        }
        dpapi::unprotect_data(&protected_bytes)
            .map_err(|e| format!("wrapped_secret dpapi unprotect failed. detail={}", e))
    }

    /// 現在の k-user をパスワード暗号化バックアップJSONへ変換する。
    pub fn export_k_user_backup_json(&self, backup_password: &str) -> Result<String, String> {
        if backup_password.is_empty() {
            return Err("export_k_user_backup_json failed. backup_password is empty.".to_string());
        }
        let k_user = self.get_k_user();
        let mut salt = [0u8; BACKUP_KDF_SALT_BYTES];
        rand::rng().fill_bytes(&mut salt);
        let backup_key = Self::derive_backup_key(
            backup_password,
            &salt,
            BACKUP_KDF_N_LOG2,
            BACKUP_KDF_R,
            BACKUP_KDF_P,
            32,
        )?;
        let cipher = Aes256Gcm::new(backup_key.as_ref().into());
        let mut iv = [0u8; 12];
        rand::rng().fill_bytes(&mut iv);
        let nonce = Nonce::from_slice(&iv);
        let mut buffer = k_user.to_vec();
        let tag = cipher
            .encrypt_in_place_detached(nonce, b"", &mut buffer)
            .map_err(|e| format!("export_k_user_backup_json failed. encrypt error={:?}", e))?;
        use base64::prelude::*;
        let envelope = KUserBackupEnvelope {
            version: BACKUP_FILE_VERSION,
            alg: "A256GCM".to_string(),
            kdf: "scrypt".to_string(),
            kdf_params: KUserBackupKdfParams {
                n_log2: BACKUP_KDF_N_LOG2,
                r: BACKUP_KDF_R,
                p: BACKUP_KDF_P,
                key_len: 32,
                salt_base64: BASE64_STANDARD.encode(salt),
            },
            encrypted: KUserBackupEncrypted {
                iv_base64: BASE64_STANDARD.encode(iv),
                cipher_base64: BASE64_STANDARD.encode(buffer),
                tag_base64: BASE64_STANDARD.encode(tag),
            },
            key_fingerprint: Self::create_fingerprint_hex(&k_user),
            exported_at: chrono::Utc::now().to_rfc3339(),
        };
        serde_json::to_string_pretty(&envelope)
            .map_err(|e| format!("export_k_user_backup_json failed. serialize error={}", e))
    }

    /// バックアップJSON文字列から k-user を復元し、wrapped_k_user.bin へ保存する。
    pub fn import_k_user_backup_json(
        &self,
        backup_password: &str,
        backup_json_text: &str,
    ) -> Result<String, String> {
        if backup_password.is_empty() {
            return Err("import_k_user_backup_json failed. backup_password is empty.".to_string());
        }
        let envelope: KUserBackupEnvelope = serde_json::from_str(backup_json_text)
            .map_err(|e| format!("import_k_user_backup_json failed. json parse error={}", e))?;
        if envelope.version != BACKUP_FILE_VERSION {
            return Err(format!(
                "import_k_user_backup_json failed. unsupported version={}",
                envelope.version
            ));
        }
        if envelope.alg != "A256GCM" {
            return Err(format!(
                "import_k_user_backup_json failed. unsupported alg={}",
                envelope.alg
            ));
        }
        if envelope.kdf != "scrypt" {
            return Err(format!(
                "import_k_user_backup_json failed. unsupported kdf={}",
                envelope.kdf
            ));
        }
        use base64::prelude::*;
        let salt = BASE64_STANDARD
            .decode(&envelope.kdf_params.salt_base64)
            .map_err(|e| format!("import_k_user_backup_json failed. salt decode error={}", e))?;
        let iv = BASE64_STANDARD
            .decode(&envelope.encrypted.iv_base64)
            .map_err(|e| format!("import_k_user_backup_json failed. iv decode error={}", e))?;
        let mut cipher_buffer = BASE64_STANDARD
            .decode(&envelope.encrypted.cipher_base64)
            .map_err(|e| format!("import_k_user_backup_json failed. cipher decode error={}", e))?;
        let tag = BASE64_STANDARD
            .decode(&envelope.encrypted.tag_base64)
            .map_err(|e| format!("import_k_user_backup_json failed. tag decode error={}", e))?;
        if iv.len() != 12 {
            return Err(format!(
                "import_k_user_backup_json failed. iv length invalid={}",
                iv.len()
            ));
        }
        let backup_key = Self::derive_backup_key(
            backup_password,
            &salt,
            envelope.kdf_params.n_log2,
            envelope.kdf_params.r,
            envelope.kdf_params.p,
            envelope.kdf_params.key_len,
        )?;
        let cipher = Aes256Gcm::new(backup_key.as_ref().into());
        let nonce = Nonce::from_slice(&iv);
        cipher
            .decrypt_in_place_detached(nonce, b"", &mut cipher_buffer, tag.as_slice().into())
            .map_err(|e| format!("import_k_user_backup_json failed. decrypt error={:?}", e))?;
        if cipher_buffer.len() != 32 {
            return Err(format!(
                "import_k_user_backup_json failed. k-user length invalid={}",
                cipher_buffer.len()
            ));
        }
        self.import_k_user(&cipher_buffer)?;
        Ok(Self::create_fingerprint_hex(&cipher_buffer))
    }

    /// k-user をパスワード暗号化バックアップファイルへ出力する。
    pub fn export_k_user_backup_file(
        &self,
        backup_password: &str,
        backup_file_path: Option<&str>,
    ) -> Result<(String, String), String> {
        let output_path = backup_file_path
            .map(|v| v.to_string())
            .unwrap_or_else(|| DEFAULT_K_USER_BACKUP_PATH.to_string());
        let json_text = self.export_k_user_backup_json(backup_password)?;
        let path = Path::new(&output_path);
        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }
        fs::write(path, format!("{}\n", json_text))
            .map_err(|e| format!("export_k_user_backup_file failed. write error={}", e))?;
        let envelope: KUserBackupEnvelope = serde_json::from_str(&json_text)
            .map_err(|e| format!("export_k_user_backup_file failed. json parse error={}", e))?;
        Ok((output_path, envelope.key_fingerprint))
    }

    /// k-user をパスワード暗号化バックアップファイルから復元する。
    pub fn import_k_user_backup_file(
        &self,
        backup_password: &str,
        backup_file_path: Option<&str>,
    ) -> Result<String, String> {
        let input_path = backup_file_path
            .map(|v| v.to_string())
            .unwrap_or_else(|| DEFAULT_K_USER_BACKUP_PATH.to_string());
        let json_text = fs::read_to_string(&input_path)
            .map_err(|e| format!("import_k_user_backup_file failed. read error path={} err={}", input_path, e))?;
        self.import_k_user_backup_json(backup_password, &json_text)
    }

    /// デバイス毎の通信鍵 `k-device` を導出して返す。
    ///
    /// # 導出式
    /// ```text
    /// k-device = HMAC-SHA256(key=k-user, message=target_device_name)
    /// ```
    ///
    /// - `target_device_name` には `publicId`（初期値 `IoT_<base MAC からコロン除去>`、例 `IoT_04CEF94EB580`）を渡す。
    /// - 出力は 32 byte（HMAC-SHA256 のダイジェスト長そのもの）。
    ///
    /// # 設計判断（HKDF ではなく HMAC 直接導出を採用する理由）
    /// - 安全性差：HKDF も内部で HMAC を 2 段（extract / expand）使うため、出力 32 byte の
    ///   場合の事実上の安全性差はない。
    /// - 実装統一：command 署名（`sign_by_k_device`）、OTA 開始署名（`ota_workflow.rs:344`）、
    ///   ESP32 側 `mbedtls_md_*`（`MQTT/mqtt.cpp`、`secureNvsInit.cpp`）も HMAC-SHA256 で
    ///   統一されており、`k-device` 導出だけ HKDF にすると実装が分岐するだけで利点がない。
    /// - 互換性：旧 TS 実装で HMAC 直接導出を使っており、後方互換性を維持。
    /// - 詳細：`鍵管理および初期セットアップ設計仕様書.md` §6.2 / §16 変更履歴（2026-05-19）。
    ///
    /// # 引数
    /// - `target_device_name`：`publicId`（公開識別子）
    ///
    /// # 戻り値
    /// - 32 byte の `k-device`（AES-256-GCM 鍵 + HMAC 鍵として使用）
    ///
    /// # 注意
    /// - 返却された値はメモリ上のみで使用し、ディスク保存しない。
    /// - ESP32 側 NVS には別経路で current/previous の 2 スロットで保持される
    ///   （`sensitiveData.cpp`、`鍵管理および初期セットアップ設計仕様書.md` §10.3）。
    /// - IPC 経由で raw 値を返却しない（`LocalServer秘密処理別層仕様書.md` §7.2 禁止 API）。
    ///
    /// # 関連
    /// - 統合ガイド：`セキュア全般ノウハウ_設計から実装まで.md` §12
    /// - マッピング表：`設計書実装マッピング表.md` §1.5
    pub fn get_k_device(&self, target_device_name: &str) -> [u8; 32] {
        let k_user = self.get_k_user();
        type HmacSha256 = Hmac<Sha256>;
        let mut direct_hmac = <HmacSha256 as Mac>::new_from_slice(&k_user).unwrap();
        direct_hmac.update(target_device_name.as_bytes());
        let k_dev_result = direct_hmac.finalize().into_bytes();
        let mut k_device = [0u8; 32];
        k_device.copy_from_slice(&k_dev_result);
        k_device
    }

    /// k-device で HMAC-SHA256 署名を生成する。
    ///
    /// # 用途
    /// - 高リスク command（`otaStart` / `set/keyDeviceSet` / `set/fileLogSet` 等）の
    ///   **真正性保護**。ESP32 側で k-device による HMAC 検証を行い、改ざんを検知する。
    /// - LocalServer または偽 LocalServer から送られたコマンドを区別できる。
    ///
    /// # 署名対象（厳守）
    /// - `signature` フィールドを **除いた** JSON 文字列（payload 全体）。
    /// - ESP32 側受信時に同じ手順で `signature` を除去して再構築・検証する。
    /// - 詳細：`LocalServer秘密処理別層仕様書.md` §7.5 / `MQTTコマンド仕様書.md` §3.1.1
    ///
    /// # 引数
    /// - `target_device_name`：`publicId`（k-device 導出のキー）
    /// - `message_text`：署名対象 JSON 文字列（`signature` 追加前）
    ///
    /// # 戻り値
    /// - 成功時：Base64 エンコード済み HMAC-SHA256 値（44 文字、32 byte の Base64）
    /// - 失敗時：`Err`
    ///   - "sign_by_k_device failed. target_device_name is empty."
    ///   - "sign_by_k_device failed. message_text is empty."
    ///   - "sign_by_k_device failed. init error=..."
    ///
    /// # 関連
    /// - 統合ガイド：`セキュア全般ノウハウ_設計から実装まで.md` §11
    /// - マッピング表：`設計書実装マッピング表.md` §2.2
    /// - 試験：`7042`（OTA 開始 HMAC、OK 2026-05-07）／`7052`（重要設定変更、OK 2026-05-10）
    /// - ESP32 側対向：`MQTT/mqtt_set.cpp`（受信側 HMAC 検証）／`MQTT/mqtt.cpp`（OTA）
    pub fn sign_by_k_device(&self, target_device_name: &str, message_text: &str) -> Result<String, String> {
        if target_device_name.trim().is_empty() {
            return Err("sign_by_k_device failed. target_device_name is empty.".to_string());
        }
        if message_text.is_empty() {
            return Err("sign_by_k_device failed. message_text is empty.".to_string());
        }
        let key_device = self.get_k_device(target_device_name);
        type HmacSha256 = Hmac<Sha256>;
        let mut hmac_state = <HmacSha256 as Mac>::new_from_slice(&key_device)
            .map_err(|e| format!("sign_by_k_device failed. init error={}", e))?;
        hmac_state.update(message_text.as_bytes());
        let signature_bytes = hmac_state.finalize().into_bytes();
        use base64::prelude::*;
        Ok(BASE64_STANDARD.encode(signature_bytes))
    }

    /// k-device で平文を AES-256-GCM 暗号化する。
    ///
    /// # 用途
    /// - LocalServer から ESP32 へ送る MQTT payload や AP pairing bundle を **二重暗号化**
    ///   する（TLS 経路保護に加えて、Mosquitto 管理者にも読まれないアプリ層暗号化）。
    /// - AEAD（Authenticated Encryption with Associated Data）として動作し、改ざん検知用 tag を出力する。
    ///
    /// # 引数
    /// - `target_device_name`：`publicId`（k-device 導出のキー）
    /// - `plain_text`：暗号化対象の UTF-8 文字列（JSON payload 等）
    ///
    /// # 戻り値
    /// - 成功時：タプル `(iv_b64, cipher_b64, tag_b64)`
    ///   - `iv_b64`：12 byte nonce の Base64（**毎回ランダム生成、再利用厳禁**）
    ///   - `cipher_b64`：暗号文の Base64
    ///   - `tag_b64`：16 byte GCM tag の Base64
    /// - 失敗時：`Err("Encryption error: ...")`
    ///
    /// # 注意：nonce 再利用の禁止
    /// - 同じ鍵で同じ nonce を 2 回使うと、過去メッセージとの XOR で平文が推測される
    ///   可能性がある（GCM の致命的脆弱性）。
    /// - 本関数は毎回 CSPRNG で 12 byte nonce を生成するため、呼び出し側で nonce を
    ///   再利用しなければ安全。
    ///
    /// # ESP32 側対向実装
    /// - `maintenanceApServer.cpp:881-927`（AP bundle 復号、AAD あり）
    /// - `MQTT/mqtt.cpp:604-639`（MQTT payload 復号、旧版、AAD なし）
    /// - `MQTT/mqttPayloadSecurity.cpp:92-131`（MQTT payload security 層、新版、AAD あり）
    ///
    /// # 関連
    /// - 統合ガイド：`セキュア全般ノウハウ_設計から実装まで.md` §10
    /// - マッピング表：`設計書実装マッピング表.md` §2.1
    pub fn encrypt_by_k_device(&self, target_device_name: &str, plain_text: &str) -> Result<(String, String, String), String> {
        let key = self.get_k_device(target_device_name);
        let cipher = Aes256Gcm::new(key.as_ref().into());
        let mut iv = [0u8; 12];
        rand::rng().fill_bytes(&mut iv);
        let nonce = Nonce::from_slice(&iv);
        let mut buffer = plain_text.as_bytes().to_vec();
        let tag = cipher.encrypt_in_place_detached(nonce, b"", &mut buffer)
            .map_err(|e| format!("Encryption error: {:?}", e))?;
        use base64::prelude::*;
        Ok((
            BASE64_STANDARD.encode(&iv),
            BASE64_STANDARD.encode(&buffer),
            BASE64_STANDARD.encode(&tag),
        ))
    }

    /// k-device で AES-256-GCM 復号する。
    ///
    /// # 用途
    /// - LocalServer 側で ESP32 からの受信 MQTT payload（暗号化済み）を復号する。
    /// - GCM tag 検証が同時に行われるため、改ざんを検知できる（AEAD = Authenticated Encryption）。
    ///
    /// # 引数
    /// - `target_device_name`：`publicId`（例：`"IoT_04CEF94EB580"`）
    /// - `iv_b64`：12 byte nonce の Base64（必須 12 byte）
    /// - `cipher_b64`：暗号文の Base64
    /// - `tag_b64`：16 byte GCM tag の Base64
    ///
    /// # 戻り値
    /// - 成功時：復号された UTF-8 文字列（JSON payload 等）
    /// - 失敗時：`Err`
    ///   - "Invalid IV length"（nonce 長不正）
    ///   - "Decryption error: ..."（tag 検証失敗＝改ざんあり、または鍵不一致）
    ///   - Base64 デコード失敗・UTF-8 デコード失敗
    ///
    /// # 注意
    /// - tag 検証で失敗した場合、その内容は **改ざんされた可能性が高い**ため、応答も無視するか
    ///   セキュリティ警告として扱う。
    /// - `target_device_name` は完全一致が必須（publicId の大文字小文字も区別）。
    ///
    /// # 関連
    /// - 設計：`セキュア全般ノウハウ_設計から実装まで.md` §10
    /// - マッピング：`設計書実装マッピング表.md` §2.1
    /// - ESP32 側対向実装：`maintenanceApServer.cpp:881-927`、`MQTT/mqttPayloadSecurity.cpp:92-131`
    pub fn decrypt_by_k_device(&self, target_device_name: &str, iv_b64: &str, cipher_b64: &str, tag_b64: &str) -> Result<String, String> {
        let key = self.get_k_device(target_device_name);
        let cipher = Aes256Gcm::new(key.as_ref().into());
        use base64::prelude::*;
        let iv = BASE64_STANDARD.decode(iv_b64).map_err(|e| e.to_string())?;
        let mut buffer = BASE64_STANDARD.decode(cipher_b64).map_err(|e| e.to_string())?;
        let tag = BASE64_STANDARD.decode(tag_b64).map_err(|e| e.to_string())?;
        if iv.len() != 12 {
            return Err("Invalid IV length".to_string());
        }
        let nonce = Nonce::from_slice(&iv);
        cipher.decrypt_in_place_detached(nonce, b"", &mut buffer, tag.as_slice().into())
            .map_err(|e| format!("Decryption error: {:?}", e))?;
        String::from_utf8(buffer).map_err(|e| e.to_string())
    }
}
