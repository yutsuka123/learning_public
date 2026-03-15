// key_manager.rs - SecretCore の鍵管理モジュール。
//
// [重要] k-user は2つの経路で取得可能:
//   (a) s_random → HKDF 導出（新規発行時）
//   (b) wrapped_k_user.bin からDPAPI復号（TS版からのインポート時）
// [厳守] wrapped_k_user.bin が存在する場合は (b) を優先する。
// [厳守] k-user の平文をファイル保存しない（DPAPI暗号化のみ）。
// 変更日: 2026-03-15 import/export と fingerprint 取得機能を追加。理由: TS-Rust 境界IFを固定するため。

use crate::dpapi;
use aes_gcm::aead::{AeadInPlace, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use hkdf::Hkdf;
use hmac::{Hmac, Mac};
use rand::Rng;
use scrypt::{Params as ScryptParams, scrypt};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
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
            let decrypted = dpapi::unprotect_data(&encrypted)?;
            if decrypted.len() != 32 {
                return Err("Invalid S_random length".to_string());
            }
            let mut s_random = [0u8; 32];
            s_random.copy_from_slice(&decrypted);
            Ok(s_random)
        } else {
            let mut s_random = [0u8; 32];
            rand::rng().fill_bytes(&mut s_random);
            let encrypted = dpapi::protect_data(&s_random)?;
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
        let decrypted = dpapi::unprotect_data(&encrypted).ok()?;
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

    /// k-device を返す。k-user → HMAC-SHA256(targetDeviceName) で導出。
    /// [重要] TS版互換のため HMAC-SHA256 直接導出を使用する。
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

    /// k-device で平文を AES-256-GCM 暗号化する。
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
