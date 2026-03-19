//! `ProductionTool` の設定読込モジュール。
//!
//! [重要] 本ファイルは `ProductionTool` 最小実行骨格向けの設定読込を担当します。
//! 主な仕様:
//! - `config/productionTool.settings.json` を優先読込する
//! - 実ファイルが無い場合は `config/productionTool.settings.example.json` を読込する
//! - どちらも無い場合は安全側の既定値を使用する
//! 制限事項:
//! - この段階では機密情報を設定ファイルへ保存しない
//! - 不可逆処理用の詳細設定はまだ扱わない

use serde::{Deserialize, Serialize};
use std::fmt;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// `ProductionTool` の起動設定を表します。
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProductionToolConfig {
    /// アプリケーション表示名です。
    pub application_name: String,
    /// 表示用バージョン文字列です。
    pub application_version: String,
    /// 実行モード名です。
    pub default_mode: String,
    /// 監査ログ出力先です。
    pub audit_log_directory_path: String,
    /// `SecretCore` 接続先の Named Pipe 名です。
    pub secret_core_pipe_name: String,
    /// 起動時に `SecretCore` 事前確認を行うかどうかです。
    pub enable_secret_core_preflight: bool,
}

/// 設定ファイル読込結果を表します。
#[derive(Debug, Clone)]
pub struct LoadedConfig {
    /// 実際に使用する設定値です。
    pub config: ProductionToolConfig,
    /// 読込元の説明です。
    pub config_source_label: String,
}

/// 設定関連のエラーを表します。
#[derive(Debug)]
pub enum AppConfigError {
    /// 設定ファイルの JSON 解析に失敗しました。
    InvalidJson {
        config_path: PathBuf,
        detail_message: String,
    },
    /// 設定ファイルの読込に失敗しました。
    ReadFailed {
        config_path: PathBuf,
        source_error: io::Error,
    },
}

impl fmt::Display for AppConfigError {
    /// エラー内容を利用者向け文字列へ変換します。
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidJson {
                config_path,
                detail_message,
            } => write!(
                formatter,
                "AppConfigError::InvalidJson path={} detail={}",
                config_path.display(),
                detail_message
            ),
            Self::ReadFailed {
                config_path,
                source_error,
            } => write!(
                formatter,
                "AppConfigError::ReadFailed path={} detail={}",
                config_path.display(),
                source_error
            ),
        }
    }
}

impl std::error::Error for AppConfigError {}

/// `ProductionTool` 設定を読み込みます。
///
/// 読込順序:
/// 1. 実運用想定の `config/productionTool.settings.json`
/// 2. 共有用の `config/productionTool.settings.example.json`
/// 3. コード内既定値
pub fn load_app_config(project_root_path: &Path) -> Result<LoadedConfig, AppConfigError> {
    let primary_config_path = project_root_path.join("config").join("productionTool.settings.json");
    if primary_config_path.exists() {
        let config = read_config_file(&primary_config_path)?;
        return Ok(LoadedConfig {
            config,
            config_source_label: primary_config_path.display().to_string(),
        });
    }

    let example_config_path = project_root_path
        .join("config")
        .join("productionTool.settings.example.json");
    if example_config_path.exists() {
        let config = read_config_file(&example_config_path)?;
        return Ok(LoadedConfig {
            config,
            config_source_label: format!("example: {}", example_config_path.display()),
        });
    }

    Ok(LoadedConfig {
        config: build_default_config(project_root_path),
        config_source_label: "built-in defaults".to_string(),
    })
}

/// 指定設定ファイルを読み込み、構造体へ変換します。
fn read_config_file(config_path: &Path) -> Result<ProductionToolConfig, AppConfigError> {
    let config_text = fs::read_to_string(config_path).map_err(|source_error| AppConfigError::ReadFailed {
        config_path: config_path.to_path_buf(),
        source_error,
    })?;

    serde_json::from_str::<ProductionToolConfig>(&config_text).map_err(|source_error| {
        AppConfigError::InvalidJson {
            config_path: config_path.to_path_buf(),
            detail_message: source_error.to_string(),
        }
    })
}

/// 設定ファイルが存在しない場合の既定設定を生成します。
fn build_default_config(project_root_path: &Path) -> ProductionToolConfig {
    ProductionToolConfig {
        application_name: "ProductionTool".to_string(),
        application_version: env!("CARGO_PKG_VERSION").to_string(),
        default_mode: "startupSkeleton".to_string(),
        audit_log_directory_path: project_root_path.join("logs").display().to_string(),
        secret_core_pipe_name: r"\\.\pipe\iot-secret-core-ipc".to_string(),
        enable_secret_core_preflight: true,
    }
}
