//! Cross-platform key protection: file-based wrapping key + AES-256-GCM.
//!
//! # Design
//! A 32-byte random wrapping key is generated on first use and stored in
//! `../LocalServer/data/keys/master_key.bin` inside a dedicated `keys/` subfolder.
//! All subsequent encrypt/decrypt operations on the same machine use this wrapping key.
//!
//! # Security model
//! The `keys/` directory and all files within it receive OS-native restrictive permissions
//! on creation, so that only the LocalServer process account (and SYSTEM) can read them.
//! Administrators can delete files for maintenance but cannot read them without first
//! taking ownership (which leaves an audit trail).
//!
//! ## Unix (Linux / macOS)
//! - `keys/`          → chmod 700 (owner rwx only)
//! - `master_key.bin` → chmod 600 (owner rw only)
//!
//! ## Windows
//! - `icacls /inheritance:r` removes inherited ACEs.
//! - Current user + `NT AUTHORITY\SYSTEM` receive Full Control.
//! - `BUILTIN\Administrators` receive Delete only (no Read).
//! - Other accounts: no access.
//! - An Administrator who must delete can Take Ownership first (standard Windows admin flow).
//!
//! # Why file-based instead of OS keyring
//! The OS keyring (Windows Credential Manager, macOS Keychain, libsecret) is not reliably
//! shared between a parent Node.js process and its spawned child (SecretCore). The file-based
//! approach is predictable, cross-platform, and provides equivalent security when the
//! `keys/` directory is protected by OS file permissions.
//!
//! # File format (protect_data output)
//! [12-byte nonce][variable-length ciphertext][16-byte GCM tag]
//!
//! # Migration from DPAPI
//! Existing wrapped_secret.bin / wrapped_k_user.bin encrypted with DPAPI cannot be
//! decrypted by this module. Before switching builds:
//!   1. Run IPC command: export_k_user  ->  saves k_user_backup.enc.json
//!   2. After switching: delete wrapped_secret.bin and wrapped_k_user.bin
//!   3. Restart SecretCore  ->  generates new s_random, creates new wrapped_secret.bin
//!   4. Run IPC command: import_k_user_backup  ->  restores k-user from backup file
//!
//! # Related
//! - Design: `鍵管理および初期セットアップ設計仕様書.md` §4
//! - Task:   `020-0004` (Mac/Linux IPC transport generalization)

use aes_gcm::aead::{AeadInPlace, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use rand::Rng;
use std::fs;
use std::path::Path;

const MASTER_KEY_DIR: &str = "../LocalServer/data/keys";
const MASTER_KEY_PATH: &str = "../LocalServer/data/keys/master_key.bin";

// ============================================================
// Permission helpers
// ============================================================

/// Apply restrictive permissions to a key file (600 on Unix, ACL on Windows).
/// Call this after writing any file under the `keys/` directory.
pub fn apply_key_file_permissions(path: &Path) -> Result<(), String> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o600))
            .map_err(|e| format!("chmod 600 failed on {:?}: {}", path, e))?;
    }
    #[cfg(windows)]
    {
        windows_apply_file_acl(path)?;
    }
    Ok(())
}

/// Apply restrictive permissions to the `keys/` directory itself (700 on Unix, ACL on Windows).
fn apply_keys_dir_permissions(dir: &Path) -> Result<(), String> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(dir, fs::Permissions::from_mode(0o700))
            .map_err(|e| format!("chmod 700 failed on {:?}: {}", dir, e))?;
    }
    #[cfg(windows)]
    {
        windows_apply_dir_acl(dir)?;
    }
    Ok(())
}

// ---- Windows ACL via icacls ----

#[cfg(windows)]
fn current_windows_user() -> String {
    std::env::var("USERNAME").unwrap_or_else(|_| "SYSTEM".to_string())
}

#[cfg(windows)]
fn run_icacls(args: &[&str]) -> Result<(), String> {
    let out = std::process::Command::new("icacls")
        .args(args)
        .output()
        .map_err(|e| format!("icacls spawn error: {}", e))?;
    if !out.status.success() {
        return Err(format!(
            "icacls failed [{}]: {}",
            args.join(" "),
            String::from_utf8_lossy(&out.stderr).trim()
        ));
    }
    Ok(())
}

/// Set ACL on a single key file:
///   current user + SYSTEM = Full control
///   Administrators = Delete only (no Read)
#[cfg(windows)]
fn windows_apply_file_acl(path: &Path) -> Result<(), String> {
    let p = path.to_string_lossy().to_string();
    let user_grant = format!("{}:(F)", current_windows_user());
    run_icacls(&[&p, "/inheritance:r"])?;
    run_icacls(&[&p, "/grant:r", &user_grant])?;
    run_icacls(&[&p, "/grant:r", "NT AUTHORITY\\SYSTEM:(F)"])?;
    // D = Delete right only (no Read, no Write, no Execute)
    run_icacls(&[&p, "/grant:r", "BUILTIN\\Administrators:(D)"])?;
    Ok(())
}

/// Set ACL on the keys/ directory:
///   current user + SYSTEM = Full control (inherited by children)
///   Administrators = Delete + Delete Child (inherited); no Read
#[cfg(windows)]
fn windows_apply_dir_acl(dir: &Path) -> Result<(), String> {
    let p = dir.to_string_lossy().to_string();
    let user_grant = format!("{}:(OI)(CI)(F)", current_windows_user());
    run_icacls(&[&p, "/inheritance:r"])?;
    run_icacls(&[&p, "/grant:r", &user_grant])?;
    run_icacls(&[&p, "/grant:r", "NT AUTHORITY\\SYSTEM:(OI)(CI)(F)"])?;
    // (OI)(CI)(D,DC): Object Inherit + Container Inherit; Delete + Delete Child
    run_icacls(&[&p, "/grant:r", "BUILTIN\\Administrators:(OI)(CI)(D,DC)"])?;
    Ok(())
}

// ============================================================
// Wrapping key management
// ============================================================

/// Load the wrapping key from disk, or generate and persist a new one on first use.
/// On first creation, restrictive OS permissions are applied to the file and its directory.
fn get_or_create_wrapping_key() -> Result<[u8; 32], String> {
    let path = Path::new(MASTER_KEY_PATH);
    if path.exists() {
        let data = fs::read(path).map_err(|e| format!("master_key.bin read error: {}", e))?;
        if data.len() != 32 {
            return Err(format!(
                "master_key.bin length invalid: {} (expected 32)",
                data.len()
            ));
        }
        let mut wk = [0u8; 32];
        wk.copy_from_slice(&data);
        Ok(wk)
    } else {
        let mut wk = [0u8; 32];
        rand::rng().fill_bytes(&mut wk);

        // Ensure keys/ directory exists with restrictive permissions.
        let dir = Path::new(MASTER_KEY_DIR);
        let dir_is_new = !dir.exists();
        fs::create_dir_all(dir).map_err(|e| format!("keys/ dir create failed: {}", e))?;
        if dir_is_new {
            if let Err(e) = apply_keys_dir_permissions(dir) {
                eprintln!("Warning: could not set keys/ dir permissions: {}", e);
            }
        }

        fs::write(path, &wk).map_err(|e| format!("master_key.bin write error: {}", e))?;
        if let Err(e) = apply_key_file_permissions(path) {
            eprintln!("Warning: could not set master_key.bin permissions: {}", e);
        }
        Ok(wk)
    }
}

// ============================================================
// Public encrypt / decrypt API
// ============================================================

/// Encrypt `data` using the file-based wrapping key + AES-256-GCM.
///
/// Output format: `[12-byte nonce][ciphertext][16-byte GCM tag]`
///
/// The wrapping key is loaded from `master_key.bin`, or created on first use.
pub fn protect_data(data: &[u8]) -> Result<Vec<u8>, String> {
    let wk = get_or_create_wrapping_key()?;
    let cipher = Aes256Gcm::new(wk.as_ref().into());
    let mut nonce_bytes = [0u8; 12];
    rand::rng().fill_bytes(&mut nonce_bytes);
    let nonce = Nonce::from_slice(&nonce_bytes);
    let mut buffer = data.to_vec();
    let tag = cipher
        .encrypt_in_place_detached(nonce, b"", &mut buffer)
        .map_err(|e| format!("protect_data encrypt error: {:?}", e))?;
    let mut result = Vec::with_capacity(12 + buffer.len() + 16);
    result.extend_from_slice(&nonce_bytes);
    result.extend_from_slice(&buffer);
    result.extend_from_slice(tag.as_ref());
    Ok(result)
}

/// Decrypt `encrypted_data` using the file-based wrapping key + AES-256-GCM.
///
/// Input format: `[12-byte nonce][ciphertext][16-byte GCM tag]`
///
/// Returns an error if:
/// - `master_key.bin` cannot be read
/// - GCM tag verification fails (data corruption, wrong key, or DPAPI format)
pub fn unprotect_data(encrypted_data: &[u8]) -> Result<Vec<u8>, String> {
    const MIN_LEN: usize = 12 + 16; // nonce + GCM tag
    if encrypted_data.len() < MIN_LEN {
        return Err(format!(
            "unprotect_data: data too short ({} bytes, need at least {}). \
            If this file was created with DPAPI, see migration steps in key_protect.rs.",
            encrypted_data.len(),
            MIN_LEN
        ));
    }
    let wk = get_or_create_wrapping_key()?;
    let cipher = Aes256Gcm::new(wk.as_ref().into());
    let nonce = Nonce::from_slice(&encrypted_data[..12]);
    let tag_start = encrypted_data.len() - 16;
    let mut buffer = encrypted_data[12..tag_start].to_vec();
    let tag = &encrypted_data[tag_start..];
    cipher
        .decrypt_in_place_detached(nonce, b"", &mut buffer, tag.into())
        .map_err(|e| format!(
            "unprotect_data decrypt error: {:?}. \
            If this file was created with DPAPI or a different key, \
            see migration steps in key_protect.rs.",
            e
        ))?;
    Ok(buffer)
}
