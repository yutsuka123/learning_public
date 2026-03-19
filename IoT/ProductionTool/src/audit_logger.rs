//! `ProductionTool` の監査ログ出力モジュール。
//!
//! [重要] 本ファイルは最小実行骨格向けの監査ログ出力を担当します。
//! 主な仕様:
//! - 起動時の状態を JSON で 1 ファイル出力する
//! - 秘密値は保存しない
//! - `SecretCore` 事前確認結果を記録する
//! 制限事項:
//! - 本格的な監査イベント蓄積や検索は未実装
//! - 画面操作単位の監査は後続フェーズで追加する

use chrono::Local;
use serde::Serialize;
use std::fmt;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// 起動時の監査ログ内容を表します。
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StartupAuditLogRecord {
    /// イベント種別です。
    pub event_type: String,
    /// アプリケーション名です。
    pub application_name: String,
    /// アプリケーション版数です。
    pub application_version: String,
    /// 実行モードです。
    pub execution_mode: String,
    /// ホスト名です。
    pub host_name: String,
    /// 設定読込元です。
    pub config_source_label: String,
    /// 監査ログ出力先です。
    pub audit_log_directory_path: String,
    /// `SecretCore` Pipe 名です。
    pub secret_core_pipe_name: String,
    /// `SecretCore` 事前確認結果です。
    pub secret_core_preflight_result: String,
    /// 詳細メッセージです。
    pub summary_message: String,
    /// 記録時刻です。
    pub recorded_at_local: String,
}

/// 監査ログ出力結果を表します。
#[derive(Debug, Clone)]
pub struct AuditWriteResult {
    /// 出力先ファイルパスです。
    pub audit_log_file_path: PathBuf,
}

/// 監査ログ関連のエラーを表します。
#[derive(Debug)]
pub enum AuditLoggerError {
    /// ログ出力先ディレクトリの作成に失敗しました。
    CreateDirectoryFailed {
        directory_path: PathBuf,
        source_error: io::Error,
    },
    /// ログ内容の JSON 変換に失敗しました。
    SerializeFailed {
        detail_message: String,
    },
    /// ログファイル書込に失敗しました。
    WriteFailed {
        file_path: PathBuf,
        source_error: io::Error,
    },
}

impl fmt::Display for AuditLoggerError {
    /// エラー内容を利用者向け文字列へ変換します。
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::CreateDirectoryFailed {
                directory_path,
                source_error,
            } => write!(
                formatter,
                "AuditLoggerError::CreateDirectoryFailed directory={} detail={}",
                directory_path.display(),
                source_error
            ),
            Self::SerializeFailed { detail_message } => write!(
                formatter,
                "AuditLoggerError::SerializeFailed detail={}",
                detail_message
            ),
            Self::WriteFailed {
                file_path,
                source_error,
            } => write!(
                formatter,
                "AuditLoggerError::WriteFailed path={} detail={}",
                file_path.display(),
                source_error
            ),
        }
    }
}

impl std::error::Error for AuditLoggerError {}

/// 起動監査ログを 1 件出力します。
pub fn write_startup_audit_log(
    audit_log_directory_path: &Path,
    startup_audit_log_record: &StartupAuditLogRecord,
) -> Result<AuditWriteResult, AuditLoggerError> {
    fs::create_dir_all(audit_log_directory_path).map_err(|source_error| {
        AuditLoggerError::CreateDirectoryFailed {
            directory_path: audit_log_directory_path.to_path_buf(),
            source_error,
        }
    })?;

    let timestamp_text = Local::now().format("%Y%m%d-%H%M%S").to_string();
    let audit_log_file_path = audit_log_directory_path.join(format!("audit-{}.json", timestamp_text));
    let audit_log_text = serde_json::to_string_pretty(startup_audit_log_record).map_err(|source_error| {
        AuditLoggerError::SerializeFailed {
            detail_message: source_error.to_string(),
        }
    })?;

    fs::write(&audit_log_file_path, audit_log_text).map_err(|source_error| {
        AuditLoggerError::WriteFailed {
            file_path: audit_log_file_path.clone(),
            source_error,
        }
    })?;

    Ok(AuditWriteResult { audit_log_file_path })
}
