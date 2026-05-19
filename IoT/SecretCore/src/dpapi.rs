//! Windows DPAPI（Data Protection API）ラッパモジュール。
//!
//! # 目的
//! - LocalServer の秘密値（`S_random` / `k-user`）を、現在の Windows ユーザー資格情報に
//!   拘束して暗号化／復号する。
//! - 別 PC や別 Windows ユーザーへバイナリ（`wrapped_secret.bin` / `wrapped_k_user.bin`）を
//!   コピーされても、復号できないようにする。
//!
//! # 仕組み
//! - `CryptProtectData(flags=0)` を呼ぶことで、Windows の内部マスター鍵
//!   （ユーザープロファイル + ログイン credential 由来）で AES 暗号化される。
//! - 復号は `CryptUnprotectData` を同一 Windows ユーザーで呼ぶと自動的に行われる。
//! - チップ非依存：TPM は使わない。ソフトウェア暗号化のみ。
//!
//! # 設計判断
//! - TPM 2.0 を使わない理由：TPM 非搭載 PC で動作しない／運用複雑度が増える。
//!   詳細は `鍵管理および初期セットアップ設計仕様書.md` §2 / §16（2026-05-19 改訂）。
//! - Mac / Linux 汎用化は `todo.md` `020-0004` で扱う（Keychain Services / libsecret）。
//!
//! # 限界
//! - 同一 Windows ユーザーアカウントが侵害された場合は復号可能（DPAPI の前提）。
//! - 別 PC 復旧経路は `k-user-backup.enc.json`（scrypt + AES-256-GCM）を併用する
//!   （`key_manager.rs` の `export_k_user_backup_file` / `import_k_user_backup_file`）。
//!
//! # 関連
//! - 設計書：`鍵管理および初期セットアップ設計仕様書.md` §4 / `LocalServer秘密処理別層仕様書.md` §5
//! - 統合ガイド：`セキュア全般ノウハウ_設計から実装まで.md` §4
//! - マッピング表：`設計書実装マッピング表.md` §1.1
//!
//! # 機密事項禁止
//! - 本モジュールでは raw 平文（`S_random` / `k-user` の生バイト列）を扱うが、
//!   ログ出力・コメント例値への記載は禁止。
//! - 暗号化前の平文を呼び出し元で長時間保持しない。

use std::ptr;
use winapi::um::dpapi::{CryptProtectData, CryptUnprotectData};
use winapi::um::wincrypt::CRYPTOAPI_BLOB;
use winapi::um::winbase::LocalFree;

/// 平文バイト列を Windows DPAPI で暗号化する。
///
/// # 引数
/// - `data`：暗号化対象の平文（例：32 byte の `S_random` や `k-user`）
///
/// # 戻り値
/// - 成功時：DPAPI 暗号化済みバイト列（呼び出し元でファイルへ保存可）
/// - 失敗時：`Err` に "CryptProtectData failed"（OS API 失敗時）
///
/// # 動作
/// - `flags=0` を渡すため、暗号化は **現在ログオン中の Windows ユーザー** に拘束される。
/// - 復号は同一 Windows ユーザー上で `unprotect_data` を呼ぶ。
///
/// # 注意
/// - `data` の生バイト列はメモリ上で短時間しか保持しないこと（zeroize 推奨）。
/// - 出力バッファは `LocalFree` で解放される（OS API の仕様）。
pub fn protect_data(data: &[u8]) -> Result<Vec<u8>, String> {
    let mut data_in = CRYPTOAPI_BLOB {
        cbData: data.len() as u32,
        pbData: data.as_ptr() as *mut u8,
    };
    let mut data_out = CRYPTOAPI_BLOB {
        cbData: 0,
        pbData: ptr::null_mut(),
    };

    let success = unsafe {
        CryptProtectData(
            &mut data_in,
            ptr::null_mut(), // description (optional)
            ptr::null_mut(), // entropy (optional)
            ptr::null_mut(), // reserved
            ptr::null_mut(), // prompt struct
            0,               // flags (0 = tie to current user)
            &mut data_out,
        )
    };

    if success == 0 {
        return Err("CryptProtectData failed".to_string());
    }

    let out_slice = unsafe { std::slice::from_raw_parts(data_out.pbData, data_out.cbData as usize) };
    let result = out_slice.to_vec();
    unsafe { LocalFree(data_out.pbData as *mut _) };

    Ok(result)
}

/// DPAPI 暗号化済みバイト列を復号する。
///
/// # 引数
/// - `encrypted_data`：`protect_data` で暗号化されたバイト列
///   （例：`wrapped_secret.bin` を読み込んだ内容）
///
/// # 戻り値
/// - 成功時：復号された平文バイト列
/// - 失敗時：`Err` に "CryptUnprotectData failed"
///   - 別 Windows ユーザー上で実行した場合
///   - Windows ユーザープロファイルが再作成された場合
///   - 暗号化済みバイト列が壊れている場合
///
/// # 動作
/// - `flags=0` で復号。現在の Windows ユーザー資格情報で復号できなければ失敗する。
/// - 結果バッファは `LocalFree` で解放される（OS API の仕様）。
///
/// # 注意
/// - 復号後の平文は呼び出し元で速やかに用途実行（HKDF や HMAC への入力）し、
///   不要になったら zeroize すること。
/// - 失敗時のエラーメッセージに復号対象データの内容を含めないこと（情報漏洩防止）。
pub fn unprotect_data(encrypted_data: &[u8]) -> Result<Vec<u8>, String> {
    let mut data_in = CRYPTOAPI_BLOB {
        cbData: encrypted_data.len() as u32,
        pbData: encrypted_data.as_ptr() as *mut u8,
    };
    let mut data_out = CRYPTOAPI_BLOB {
        cbData: 0,
        pbData: ptr::null_mut(),
    };

    let success = unsafe {
        CryptUnprotectData(
            &mut data_in,
            ptr::null_mut(), // description out
            ptr::null_mut(), // entropy
            ptr::null_mut(), // reserved
            ptr::null_mut(), // prompt struct
            0,               // flags
            &mut data_out,
        )
    };

    if success == 0 {
        return Err("CryptUnprotectData failed".to_string());
    }

    let out_slice = unsafe { std::slice::from_raw_parts(data_out.pbData, data_out.cbData as usize) };
    let result = out_slice.to_vec();
    unsafe { LocalFree(data_out.pbData as *mut _) };

    Ok(result)
}
