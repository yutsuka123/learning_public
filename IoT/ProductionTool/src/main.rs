//! `ProductionTool` 起動骨格（`004-0008` 完了 / `004-0009` 対応中）。
//!
//! [重要] 本ファイルは `ProductionTool` のメインエントリーポイントです。
//! 主な仕様:
//! - `LocalServer` と分離した独立実行物として起動する（PT-001）
//! - 設定を読み込み、監査ログ出力先を初期化し、起動記録を JSON で保存する
//! - `SecretCore` Named Pipe 到達確認を行い、不可逆処理へ進まず安全停止できる
//! - 追加認証（PT-002）→ 対象機識別確認（PT-003）→ dry-run 確認（PT-005）のウィザードフローを提供する
//! 制限事項:
//! - GUI 本体は未実装（現フェーズは CLI ウィザード）
//! - 現フェーズのパスワード照合は開発用簡易ハッシュを使用（将来 SHA-256 + pepper に置き換え）
//! - 対象機情報は現フェーズ手動入力（将来 SecretCore IPC 自動取得へ置き換え）
//! - eFuse / Secure Boot / Flash Encryption の不可逆処理は未実装
//! 変更:
//! - 2026-03-18: `004-0009` 対応として PT-002 追加認証・PT-003 対象機確認・PT-005 dry-run のウィザードフローを追加。

mod app_config;
mod audit_logger;
mod auth_screen_state;
mod device_select_screen_state;
mod dry_run_screen_state;
mod startup_screen_state;

use app_config::{load_app_config, LoadedConfig};
use audit_logger::{write_startup_audit_log, StartupAuditLogRecord};
use auth_screen_state::{attempt_auth, prompt_auth_input, AuthAttemptResult};
use chrono::Local;
use device_select_screen_state::{
    build_placeholder_device_info, prompt_device_verification, verify_device_selection,
};
use dry_run_screen_state::{build_dry_run_steps, run_dry_run};
use std::env;
use std::fmt;
use std::io;
use std::path::{Path, PathBuf};
use startup_screen_state::{SafeStopScreenState, StartupScreenState};

#[cfg(windows)]
use tokio::net::windows::named_pipe::ClientOptions;

/// 起動時の `SecretCore` 事前確認状態です。
#[derive(Debug, Clone)]
enum SecretCorePreflightStatus {
    /// 接続確認に成功しました。
    Reachable,
    /// 接続確認に失敗しました。
    Unreachable { detail_message: String },
    /// 設定により確認を省略しました。
    Skipped,
}

/// アプリ全体の起動サマリです。
#[derive(Debug, Clone)]
struct StartupSummary {
    /// 設定読込結果です。
    loaded_config: LoadedConfig,
    /// `SecretCore` 事前確認状態です。
    secret_core_preflight_status: SecretCorePreflightStatus,
    /// 起動メッセージです。
    summary_message: String,
}

/// アプリ全体のエラーを表します。
#[derive(Debug)]
enum ProductionToolError {
    /// 現在の作業ディレクトリ取得に失敗しました。
    CurrentDirectoryResolveFailed { source_error: io::Error },
    /// 設定読込に失敗しました。
    ConfigLoadFailed { detail_message: String },
    /// 監査ログ出力に失敗しました。
    AuditWriteFailed { detail_message: String },
    /// `SecretCore` 事前確認に失敗しました。
    SecretCorePreflightFailed { detail_message: String },
    /// 追加認証に失敗しました（PT-007 安全停止）。
    AuthFailed { detail_message: String },
    /// 対象機識別確認に失敗しました（PT-007 安全停止）。
    DeviceVerifyFailed { detail_message: String },
}

impl fmt::Display for ProductionToolError {
    /// エラー内容を利用者向け文字列へ変換します。
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::CurrentDirectoryResolveFailed { source_error } => write!(
                formatter,
                "ProductionToolError::CurrentDirectoryResolveFailed detail={}",
                source_error
            ),
            Self::ConfigLoadFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::ConfigLoadFailed detail={}",
                detail_message
            ),
            Self::AuditWriteFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::AuditWriteFailed detail={}",
                detail_message
            ),
            Self::SecretCorePreflightFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::SecretCorePreflightFailed detail={}",
                detail_message
            ),
            Self::AuthFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::AuthFailed detail={}",
                detail_message
            ),
            Self::DeviceVerifyFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::DeviceVerifyFailed detail={}",
                detail_message
            ),
        }
    }
}

impl std::error::Error for ProductionToolError {}

/// `ProductionTool` のメインウィザードフローを開始します。
///
/// フロー: PT-001 起動 → SecretCore 確認 → PT-002 追加認証 → PT-003 対象機確認 → PT-005 dry-run
#[tokio::main]
async fn main() -> Result<(), ProductionToolError> {
    let project_root_path = resolve_project_root_path()
        .map_err(|source_error| ProductionToolError::CurrentDirectoryResolveFailed { source_error })?;

    // --- PT-001: 起動・SecretCore 確認 ---
    let startup_summary = build_startup_summary(&project_root_path).await?;
    let audit_write_result = persist_startup_audit_log(&project_root_path, &startup_summary)?;

    print_startup_banner(&project_root_path, &startup_summary, &audit_write_result.audit_log_file_path);

    // SecretCore 未到達なら PT-007 安全停止
    if let SecretCorePreflightStatus::Unreachable { .. } = &startup_summary.secret_core_preflight_status {
        let safe_stop_screen_state = build_safe_stop_screen_state(
            &startup_summary,
            &audit_write_result.audit_log_file_path,
        );
        print_safe_stop_screen_state(&safe_stop_screen_state);
        eprintln!(
            "ProductionTool safe stop: SecretCore 事前確認に失敗したため、不可逆処理へ進まず終了します。"
        );
        return Err(ProductionToolError::SecretCorePreflightFailed {
            detail_message: describe_secret_core_preflight_status(
                &startup_summary.secret_core_preflight_status,
            ),
        });
    }

    // 「開始」を確認する（CLI では Enter キー待ち）
    {
        use std::io::{self, Write};
        print!("\n[Enter] キーを押して追加認証へ進む / Ctrl+C で終了: ");
        io::stdout().flush().unwrap_or_default();
        let mut buf = String::new();
        io::stdin().read_line(&mut buf).unwrap_or_default();
    }

    // --- PT-002: 追加認証 ---
    // [重要] 現フェーズのパスワードは開発用固定ハッシュを使用する。
    // [将来対応] 本番実装では SecretCore 側でのパスワード検証へ委任する。
    // 現フェーズ固定パスワード: "maker2026" の簡易ハッシュ
    let expected_password_hash = auth_screen_state::sha256_hex_public("maker2026");
    let max_attempts: u32 = 3;
    let mut auth_state = None;

    for attempt in 1..=max_attempts {
        let auth_input = prompt_auth_input(attempt, max_attempts);
        let state = attempt_auth(&auth_input, &expected_password_hash, attempt, max_attempts);
        let result = state.attempt_result.clone();
        println!("  認証結果: {:?}", result);
        println!("  監査: {}", state.audit_label);
        if result == AuthAttemptResult::Success {
            auth_state = Some(state);
            break;
        }
        if result == AuthAttemptResult::MaxAttemptsExceeded {
            break;
        }
        if attempt == max_attempts {
            eprintln!("ProductionTool safe stop: 追加認証に {} 回失敗したため、安全停止します。", max_attempts);
            return Err(ProductionToolError::AuthFailed {
                detail_message: state.audit_label.clone(),
            });
        }
    }

    let auth_state = match auth_state {
        Some(s) => s,
        None => {
            eprintln!("ProductionTool safe stop: 追加認証に失敗しました。");
            return Err(ProductionToolError::AuthFailed {
                detail_message: "auth_failed: no success state".to_string(),
            });
        }
    };

    println!("\n=== PT-002 追加認証 OK ===");
    println!("  操作者 ID: {}", auth_state.operator_id);
    println!("  作業指示番号: {}", auth_state.work_order_id);

    // --- PT-003: 対象機識別確認 ---
    let device_info = build_placeholder_device_info();
    let (entered_serial, entered_mac) = prompt_device_verification(&device_info);
    let device_state = verify_device_selection(Some(&device_info), &entered_serial, &entered_mac);

    println!("\n=== PT-003 対象機確認結果 ===");
    println!("  結果: {:?}", device_state.verify_result);
    println!("  監査: {}", device_state.audit_label);

    if !device_state.can_proceed_to_execution_confirm {
        eprintln!(
            "ProductionTool safe stop: 対象機識別確認に失敗したため、安全停止します。detail={}",
            device_state.audit_label
        );
        return Err(ProductionToolError::DeviceVerifyFailed {
            detail_message: device_state.audit_label.clone(),
        });
    }

    // --- PT-005: dry-run 確認 ---
    println!("\n=== PT-005 dry-run 確認 ===");
    let dry_run_steps = build_dry_run_steps();
    let dry_run_state = run_dry_run(
        dry_run_steps,
        &audit_write_result.audit_log_file_path.display().to_string(),
    );

    println!("  完了状態: {:?}", dry_run_state.completion_status);
    println!("  監査: {}", dry_run_state.audit_label);
    for step in &dry_run_state.steps {
        println!(
            "  Step {}: {} -> {:?}",
            step.step_number, step.step_name, step.step_result
        );
    }

    println!("\n=== ProductionTool ウィザードフロー完了 ===");
    println!("  起動・SecretCore 確認・追加認証・対象機確認・dry-run 停止点の確認が完了しました。");
    println!("  監査ログ: {}", audit_write_result.audit_log_file_path.display());
    Ok(())
}

/// 現在の作業ディレクトリから `ProductionTool` ルートを解決します。
fn resolve_project_root_path() -> Result<PathBuf, io::Error> {
    env::current_dir()
}

/// 起動に必要な情報をまとめて収集します。
async fn build_startup_summary(
    project_root_path: &Path,
) -> Result<StartupSummary, ProductionToolError> {
    let loaded_config = load_app_config(project_root_path).map_err(|source_error| {
        ProductionToolError::ConfigLoadFailed {
            detail_message: source_error.to_string(),
        }
    })?;
    let secret_core_preflight_status =
        run_secret_core_preflight(&loaded_config.config.secret_core_pipe_name, loaded_config.config.enable_secret_core_preflight)
            .await;
    let summary_message = build_summary_message(&secret_core_preflight_status);

    Ok(StartupSummary {
        loaded_config,
        secret_core_preflight_status,
        summary_message,
    })
}

/// `SecretCore` 接続先に対して最小限の到達確認を行います。
async fn run_secret_core_preflight(
    secret_core_pipe_name: &str,
    enable_secret_core_preflight: bool,
) -> SecretCorePreflightStatus {
    if !enable_secret_core_preflight {
        return SecretCorePreflightStatus::Skipped;
    }

    if secret_core_pipe_name.trim().is_empty() {
        return SecretCorePreflightStatus::Unreachable {
            detail_message:
                "runSecretCorePreflight secretCorePipeName is empty. Named Pipe 名が未設定です。"
                    .to_string(),
        };
    }

    #[cfg(windows)]
    {
        match ClientOptions::new().open(secret_core_pipe_name) {
            Ok(_client) => SecretCorePreflightStatus::Reachable,
            Err(source_error) => SecretCorePreflightStatus::Unreachable {
                detail_message: format!(
                    "runSecretCorePreflight secretCorePipeName={} detail={}",
                    secret_core_pipe_name, source_error
                ),
            },
        }
    }

    #[cfg(not(windows))]
    {
        SecretCorePreflightStatus::Unreachable {
            detail_message:
                "runSecretCorePreflight is only implemented for Windows in the current phase."
                    .to_string(),
        }
    }
}

/// 起動状態から利用者向けの要約文を作成します。
fn build_summary_message(secret_core_preflight_status: &SecretCorePreflightStatus) -> String {
    match secret_core_preflight_status {
        SecretCorePreflightStatus::Reachable => {
            "起動、設定読込、監査ログ初期化、SecretCore 事前確認が完了しました。".to_string()
        }
        SecretCorePreflightStatus::Unreachable { detail_message } => format!(
            "起動骨格は立ち上がりましたが、SecretCore 事前確認に失敗したため安全停止します。detail={}",
            detail_message
        ),
        SecretCorePreflightStatus::Skipped => {
            "起動、設定読込、監査ログ初期化が完了しました。SecretCore 事前確認は設定により省略しました。"
                .to_string()
        }
    }
}

/// 起動監査ログを永続化します。
fn persist_startup_audit_log(
    project_root_path: &Path,
    startup_summary: &StartupSummary,
) -> Result<audit_logger::AuditWriteResult, ProductionToolError> {
    let startup_audit_log_record = StartupAuditLogRecord {
        event_type: "startupSkeleton".to_string(),
        application_name: startup_summary.loaded_config.config.application_name.clone(),
        application_version: startup_summary.loaded_config.config.application_version.clone(),
        execution_mode: startup_summary.loaded_config.config.default_mode.clone(),
        host_name: env::var("COMPUTERNAME").unwrap_or_else(|_| "unknown-host".to_string()),
        config_source_label: startup_summary.loaded_config.config_source_label.clone(),
        audit_log_directory_path: startup_summary
            .loaded_config
            .config
            .audit_log_directory_path
            .clone(),
        secret_core_pipe_name: startup_summary
            .loaded_config
            .config
            .secret_core_pipe_name
            .clone(),
        secret_core_preflight_result: describe_secret_core_preflight_status(
            &startup_summary.secret_core_preflight_status,
        ),
        summary_message: startup_summary.summary_message.clone(),
        recorded_at_local: Local::now().to_rfc3339(),
    };

    let audit_log_directory_path =
        resolve_audit_log_directory_path(project_root_path, &startup_summary.loaded_config.config.audit_log_directory_path);
    write_startup_audit_log(&audit_log_directory_path, &startup_audit_log_record).map_err(|source_error| {
        ProductionToolError::AuditWriteFailed {
            detail_message: source_error.to_string(),
        }
    })
}

/// 監査ログ出力先を絶対パスへ解決します。
fn resolve_audit_log_directory_path(project_root_path: &Path, audit_log_directory_path: &str) -> PathBuf {
    let candidate_path = PathBuf::from(audit_log_directory_path);
    if candidate_path.is_absolute() {
        return candidate_path;
    }

    project_root_path.join(candidate_path)
}

/// `SecretCore` 事前確認状態を監査ログ用文字列へ変換します。
fn describe_secret_core_preflight_status(
    secret_core_preflight_status: &SecretCorePreflightStatus,
) -> String {
    match secret_core_preflight_status {
        SecretCorePreflightStatus::Reachable => "reachable".to_string(),
        SecretCorePreflightStatus::Unreachable { detail_message } => {
            format!("unreachable: {}", detail_message)
        }
        SecretCorePreflightStatus::Skipped => "skipped".to_string(),
    }
}

/// 起動結果を標準出力へ表示します。
fn print_startup_banner(
    project_root_path: &Path,
    startup_summary: &StartupSummary,
    audit_log_file_path: &Path,
) {
    let startup_screen_state =
        build_startup_screen_state(project_root_path, startup_summary, audit_log_file_path);
    println!("============================================================");
    println!("PT-001 startup screen");
    println!(
        "{}",
        serde_json::to_string_pretty(&startup_screen_state)
            .unwrap_or_else(|_| "{\"error\":\"startupScreenSerializationFailed\"}".to_string())
    );
    println!("============================================================");
}

/// `PT-001` 起動画面相当の状態を構築します。
fn build_startup_screen_state(
    project_root_path: &Path,
    startup_summary: &StartupSummary,
    audit_log_file_path: &Path,
) -> StartupScreenState {
    StartupScreenState {
        application_name: startup_summary.loaded_config.config.application_name.clone(),
        application_version: startup_summary.loaded_config.config.application_version.clone(),
        execution_mode: startup_summary.loaded_config.config.default_mode.clone(),
        project_root_path: project_root_path.display().to_string(),
        config_source_label: startup_summary.loaded_config.config_source_label.clone(),
        secret_core_pipe_name: startup_summary
            .loaded_config
            .config
            .secret_core_pipe_name
            .clone(),
        secret_core_preflight_result: describe_secret_core_preflight_status(
            &startup_summary.secret_core_preflight_status,
        ),
        audit_log_file_path: audit_log_file_path.display().to_string(),
        summary_message: startup_summary.summary_message.clone(),
        can_proceed_to_next_step: matches!(
            startup_summary.secret_core_preflight_status,
            SecretCorePreflightStatus::Reachable | SecretCorePreflightStatus::Skipped
        ),
    }
}

/// `PT-007` 安全停止画面相当の状態を構築します。
fn build_safe_stop_screen_state(
    startup_summary: &StartupSummary,
    audit_log_file_path: &Path,
) -> SafeStopScreenState {
    SafeStopScreenState {
        error_code: "PT-STARTUP-001".to_string(),
        error_summary: "SecretCore 事前確認に失敗したため、安全停止しました。".to_string(),
        recommended_action:
            "SecretCore の起動状態、Named Pipe 名、権限、先行プロセス有無を確認してから再試行してください。"
                .to_string(),
        secret_core_preflight_result: describe_secret_core_preflight_status(
            &startup_summary.secret_core_preflight_status,
        ),
        audit_log_file_path: audit_log_file_path.display().to_string(),
        can_return_to_startup_screen: true,
    }
}

/// `PT-007` 安全停止画面相当の状態を標準出力へ表示します。
fn print_safe_stop_screen_state(safe_stop_screen_state: &SafeStopScreenState) {
    println!("PT-007 safe stop screen");
    println!(
        "{}",
        serde_json::to_string_pretty(safe_stop_screen_state)
            .unwrap_or_else(|_| "{\"error\":\"safeStopScreenSerializationFailed\"}".to_string())
    );
}
