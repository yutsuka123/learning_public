//! Key protection: file-based AES-256-GCM (default) or Windows DPAPI (PROTECT_MODE=dpapi).
//!
//! # Protection modes
//! Set env var `PROTECT_MODE=dpapi` to use Windows DPAPI (Windows only).
//! Default (or `PROTECT_MODE=file`) uses file-based AES-256-GCM with `master_key.bin`.
//!
//! ## File-based mode (default)
//! A 32-byte random wrapping key is generated on first use and stored in
//! `../LocalServer/data/keys/master_key.bin`. All wrapped_*.bin files are encrypted with
//! AES-256-GCM using this wrapping key.
//!
//! File format: `[12-byte nonce][ciphertext][16-byte GCM tag]`
//!
//! ## Windows DPAPI mode (PROTECT_MODE=dpapi)
//! Uses `CryptProtectData` / `CryptUnprotectData` (user-scope, no extra entropy).
//! `master_key.bin` is not used. The OS user's DPAPI secret is the protection boundary.
//! This mode is Windows-only; setting it on other platforms falls back to file-based.
//!
//! # OS permissions (both modes)
//! The `keys/` directory and all files within it receive restrictive OS permissions.
//! ## Unix:  keys/=700, files=600
//! ## Windows: current user+SYSTEM=Full; Administrators=Delete only (no Read).
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
// Protection mode detection
// ============================================================

/// Returns the active protection mode: "dpapi" or "file".
/// Reads PROTECT_MODE env var; falls back to "file".
pub fn active_protection_mode() -> String {
    std::env::var("PROTECT_MODE")
        .map(|s| s.to_lowercase())
        .unwrap_or_else(|_| "file".to_string())
}

// ============================================================
// File-based AES-256-GCM (internal)
// ============================================================

fn protect_data_file(data: &[u8]) -> Result<Vec<u8>, String> {
    let wk = get_or_create_wrapping_key()?;
    let cipher = Aes256Gcm::new(wk.as_ref().into());
    let mut nonce_bytes = [0u8; 12];
    rand::rng().fill_bytes(&mut nonce_bytes);
    let nonce = Nonce::from_slice(&nonce_bytes);
    let mut buffer = data.to_vec();
    let tag = cipher
        .encrypt_in_place_detached(nonce, b"", &mut buffer)
        .map_err(|e| format!("protect_data_file encrypt error: {:?}", e))?;
    let mut result = Vec::with_capacity(12 + buffer.len() + 16);
    result.extend_from_slice(&nonce_bytes);
    result.extend_from_slice(&buffer);
    result.extend_from_slice(tag.as_ref());
    Ok(result)
}

fn unprotect_data_file(encrypted_data: &[u8]) -> Result<Vec<u8>, String> {
    const MIN_LEN: usize = 12 + 16;
    if encrypted_data.len() < MIN_LEN {
        return Err(format!(
            "unprotect_data_file: data too short ({} bytes, need at least {}). \
            If this file was encrypted with DPAPI, set PROTECT_MODE=dpapi.",
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
            "unprotect_data_file decrypt error: {:?}. \
            If this file was encrypted with a different key or mode, \
            check PROTECT_MODE and wrapped_*.bin consistency.",
            e
        ))?;
    Ok(buffer)
}

// ============================================================
// Windows DPAPI (internal, Windows-only)
// ============================================================

#[cfg(windows)]
fn protect_data_dpapi(data: &[u8]) -> Result<Vec<u8>, String> {
    use winapi::ctypes::c_void;
    use winapi::um::dpapi::CryptProtectData;
    use winapi::um::winbase::LocalFree;
    use winapi::um::wincrypt::DATA_BLOB;

    let mut in_blob = DATA_BLOB {
        cbData: data.len() as u32,
        pbData: data.as_ptr() as *mut u8,
    };
    let mut out_blob = DATA_BLOB {
        cbData: 0,
        pbData: std::ptr::null_mut(),
    };

    let ok = unsafe {
        CryptProtectData(
            &mut in_blob,
            std::ptr::null(),     // description (unused)
            std::ptr::null_mut(), // optional entropy (none)
            std::ptr::null_mut(), // reserved
            std::ptr::null_mut(), // prompt struct (none)
            0,                    // flags: 0 = current-user scope
            &mut out_blob,
        )
    };
    if ok == 0 {
        return Err(format!(
            "protect_data_dpapi: CryptProtectData failed: {}",
            std::io::Error::last_os_error()
        ));
    }
    let result = unsafe {
        std::slice::from_raw_parts(out_blob.pbData, out_blob.cbData as usize).to_vec()
    };
    unsafe {
        LocalFree(out_blob.pbData as *mut c_void);
    }
    Ok(result)
}

#[cfg(windows)]
fn unprotect_data_dpapi(data: &[u8]) -> Result<Vec<u8>, String> {
    use winapi::ctypes::c_void;
    use winapi::um::dpapi::CryptUnprotectData;
    use winapi::um::winbase::LocalFree;
    use winapi::um::wincrypt::DATA_BLOB;

    let mut in_blob = DATA_BLOB {
        cbData: data.len() as u32,
        pbData: data.as_ptr() as *mut u8,
    };
    let mut out_blob = DATA_BLOB {
        cbData: 0,
        pbData: std::ptr::null_mut(),
    };

    let ok = unsafe {
        CryptUnprotectData(
            &mut in_blob,
            std::ptr::null_mut(), // description out (unused)
            std::ptr::null_mut(), // optional entropy (none)
            std::ptr::null_mut(), // reserved
            std::ptr::null_mut(), // prompt struct (none)
            0,                    // flags
            &mut out_blob,
        )
    };
    if ok == 0 {
        return Err(format!(
            "unprotect_data_dpapi: CryptUnprotectData failed: {}",
            std::io::Error::last_os_error()
        ));
    }
    let result = unsafe {
        std::slice::from_raw_parts(out_blob.pbData, out_blob.cbData as usize).to_vec()
    };
    unsafe {
        LocalFree(out_blob.pbData as *mut c_void);
    }
    Ok(result)
}

// ============================================================
// Public encrypt / decrypt API — dispatch on PROTECT_MODE
// ============================================================

/// Encrypt `data` using the active protection mode.
/// `PROTECT_MODE=dpapi` → Windows DPAPI (Windows only).
/// Default → file-based AES-256-GCM with master_key.bin.
pub fn protect_data(data: &[u8]) -> Result<Vec<u8>, String> {
    #[cfg(windows)]
    if active_protection_mode() == "dpapi" {
        return protect_data_dpapi(data);
    }
    protect_data_file(data)
}

/// Decrypt `encrypted_data` using the active protection mode.
/// `PROTECT_MODE=dpapi` → Windows DPAPI (Windows only).
/// Default → file-based AES-256-GCM with master_key.bin.
pub fn unprotect_data(encrypted_data: &[u8]) -> Result<Vec<u8>, String> {
    #[cfg(windows)]
    if active_protection_mode() == "dpapi" {
        return unprotect_data_dpapi(encrypted_data);
    }
    unprotect_data_file(encrypted_data)
}
