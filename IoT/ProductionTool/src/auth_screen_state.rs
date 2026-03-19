//! `ProductionTool` 追加認証画面（`PT-002`）の状態モジュール。
//!
//! [重要] 本ファイルは `PT-002` 追加認証画面の入力・結果状態を定義します。
//! 主な仕様:
//! - メーカーモード専用パスワードによる追加認証を受け付ける
//! - 認証失敗時は `PT-007` 安全停止へ遷移する
//! - パスワード平文は状態構造体へ保持しない
//! 制限事項:
//! - 現フェーズでは CLI 上のパスワード入力（マスク表示なし）を使用する
//! - 本格 GUI 移行後は入力 UI を置き換える
//! - パスワードの保管・配布方法は `005-0003` で設計する

use serde::Serialize;

/// 追加認証の試行結果を表します。
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum AuthAttemptResult {
    /// 認証に成功しました。
    Success,
    /// パスワードが一致しませんでした。
    WrongPassword,
    /// 操作者 ID が空でした。
    EmptyOperatorId,
    /// 認証試行回数の上限に達しました。
    MaxAttemptsExceeded,
}

/// `PT-002` 追加認証画面の状態です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AuthScreenState {
    /// 操作者 ID です。
    pub operator_id: String,
    /// 作業指示番号です。
    pub work_order_id: String,
    /// 認証試行回数です。
    pub attempt_count: u32,
    /// 最大認証試行回数です。
    pub max_attempts: u32,
    /// 認証試行結果です。
    pub attempt_result: AuthAttemptResult,
    /// 次画面（PT-003）へ進めるかどうかです。
    pub can_proceed_to_device_select: bool,
    /// 監査ログ用の認証結果ラベルです（パスワードを含まない）。
    pub audit_label: String,
}

/// `PT-002` 認証試行の入力データです。
///
/// [厳守] パスワード平文はこの構造体の外でのみ使用し、ログや状態へ保存しない。
#[derive(Debug)]
pub struct AuthInput {
    /// 操作者 ID です。
    pub operator_id: String,
    /// 作業指示番号です。
    pub work_order_id: String,
    /// パスワード（認証後は即時破棄）です。
    pub password_plain: String,
}

/// 追加認証を試行し、認証画面状態を構築します。
///
/// # 引数
/// - `auth_input`: 操作者 ID・作業指示番号・パスワード
/// - `expected_password_hash`: 正解パスワードの SHA-256 ハッシュ（16進文字列）
/// - `attempt_count`: 現在の試行回数（1始まり）
/// - `max_attempts`: 最大試行回数
///
/// # 戻り値
/// `AuthScreenState`
pub fn attempt_auth(
    auth_input: &AuthInput,
    expected_password_hash: &str,
    attempt_count: u32,
    max_attempts: u32,
) -> AuthScreenState {
    // 操作者 ID の空チェック
    if auth_input.operator_id.trim().is_empty() {
        return AuthScreenState {
            operator_id: auth_input.operator_id.clone(),
            work_order_id: auth_input.work_order_id.clone(),
            attempt_count,
            max_attempts,
            attempt_result: AuthAttemptResult::EmptyOperatorId,
            can_proceed_to_device_select: false,
            audit_label: format!(
                "auth_failed:emptyOperatorId attempt={}/{}",
                attempt_count, max_attempts
            ),
        };
    }

    // 試行回数上限チェック
    if attempt_count > max_attempts {
        return AuthScreenState {
            operator_id: auth_input.operator_id.clone(),
            work_order_id: auth_input.work_order_id.clone(),
            attempt_count,
            max_attempts,
            attempt_result: AuthAttemptResult::MaxAttemptsExceeded,
            can_proceed_to_device_select: false,
            audit_label: format!(
                "auth_failed:maxAttemptsExceeded attempt={}/{}",
                attempt_count, max_attempts
            ),
        };
    }

    // パスワード照合（SHA-256 ハッシュで比較）
    // [厳守] パスワード平文をログや構造体へ保存しない
    let password_hash = sha256_hex(&auth_input.password_plain);
    let password_matched = password_hash == expected_password_hash;

    if password_matched {
        AuthScreenState {
            operator_id: auth_input.operator_id.clone(),
            work_order_id: auth_input.work_order_id.clone(),
            attempt_count,
            max_attempts,
            attempt_result: AuthAttemptResult::Success,
            can_proceed_to_device_select: true,
            audit_label: format!(
                "auth_success operatorId={} workOrderId={} attempt={}/{}",
                auth_input.operator_id, auth_input.work_order_id, attempt_count, max_attempts
            ),
        }
    } else {
        AuthScreenState {
            operator_id: auth_input.operator_id.clone(),
            work_order_id: auth_input.work_order_id.clone(),
            attempt_count,
            max_attempts,
            attempt_result: AuthAttemptResult::WrongPassword,
            can_proceed_to_device_select: false,
            audit_label: format!(
                "auth_failed:wrongPassword operatorId={} attempt={}/{}",
                auth_input.operator_id, attempt_count, max_attempts
            ),
        }
    }
}

/// 文字列の SHA-256 ハッシュを 16進文字列で返します（外部公開用）。
///
/// [厳守] 本関数はパスワード照合のみに使用し、ハッシュ化前の平文を外部へ漏らさない。
/// [将来対応] 本番では `sha2` クレートの真の SHA-256 へ置き換える。
pub fn sha256_hex_public(input: &str) -> String {
    sha256_hex(input)
}

/// 文字列の SHA-256 ハッシュを 16進文字列で返します（内部実装）。
///
/// [厳守] 本関数はパスワード照合のみに使用し、ハッシュ化前の平文を外部へ漏らさない。
fn sha256_hex(input: &str) -> String {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};

    // [注意] 現フェーズは開発用の簡易ハッシュを使用する。
    // [将来対応] 本番実装では `sha2` クレートの SHA-256 へ置き換える。
    // 理由: 現フェーズではパスワード管理方式が未確定のため、まず導線を確認する。
    let mut hasher = DefaultHasher::new();
    input.hash(&mut hasher);
    format!("{:016x}", hasher.finish())
}

/// CLI での追加認証入力プロンプトを表示し、入力を受け付けます。
///
/// # 引数
/// - `attempt_count`: 試行番号表示用
/// - `max_attempts`: 上限表示用
///
/// # 戻り値
/// `AuthInput`（パスワードは実行時のみ保持）
pub fn prompt_auth_input(attempt_count: u32, max_attempts: u32) -> AuthInput {
    use std::io::{self, Write};

    println!("--- PT-002 追加認証 ({}/{}) ---", attempt_count, max_attempts);
    print!("操作者 ID: ");
    io::stdout().flush().unwrap_or_default();
    let mut operator_id = String::new();
    io::stdin().read_line(&mut operator_id).unwrap_or_default();

    print!("作業指示番号: ");
    io::stdout().flush().unwrap_or_default();
    let mut work_order_id = String::new();
    io::stdin().read_line(&mut work_order_id).unwrap_or_default();

    // [重要] 現フェーズはマスク表示なしの平文入力。将来 GUI 移行時は置き換える。
    print!("パスワード: ");
    io::stdout().flush().unwrap_or_default();
    let mut password_plain = String::new();
    io::stdin().read_line(&mut password_plain).unwrap_or_default();

    AuthInput {
        operator_id: operator_id.trim().to_string(),
        work_order_id: work_order_id.trim().to_string(),
        password_plain: password_plain.trim().to_string(),
    }
}
