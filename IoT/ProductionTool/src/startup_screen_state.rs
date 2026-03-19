//! `ProductionTool` 起動画面 / 安全停止画面の状態モデル。
//!
//! [重要] 本ファイルは `PT-001` 起動画面と `PT-007` 安全停止画面へ対応する
//! 画面状態モデルを定義します。
//! 主な仕様:
//! - 起動時に表示したい情報を `StartupScreenState` へ集約する
//! - 安全停止時に表示したい情報を `SafeStopScreenState` へ集約する
//! - 将来 GUI へ移行しても再利用しやすい DTO 形式にする
//! 制限事項:
//! - まだ実 GUI は持たず、現在は CLI 出力で利用する
//! - 追加認証画面、対象機確認画面の状態は未定義

use serde::Serialize;

/// `PT-001` 起動画面の状態です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StartupScreenState {
    /// アプリケーション名です。
    pub application_name: String,
    /// アプリケーション版数です。
    pub application_version: String,
    /// 実行モードです。
    pub execution_mode: String,
    /// プロジェクトルートです。
    pub project_root_path: String,
    /// 設定読込元です。
    pub config_source_label: String,
    /// `SecretCore` 接続先 Named Pipe 名です。
    pub secret_core_pipe_name: String,
    /// `SecretCore` 事前確認結果です。
    pub secret_core_preflight_result: String,
    /// 監査ログファイルパスです。
    pub audit_log_file_path: String,
    /// 起動要約メッセージです。
    pub summary_message: String,
    /// 次画面へ進めるかどうかです。
    pub can_proceed_to_next_step: bool,
}

/// `PT-007` 安全停止画面の状態です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SafeStopScreenState {
    /// エラーコードです。
    pub error_code: String,
    /// エラー概要です。
    pub error_summary: String,
    /// 推奨対応です。
    pub recommended_action: String,
    /// `SecretCore` 事前確認結果です。
    pub secret_core_preflight_result: String,
    /// 監査ログファイルパスです。
    pub audit_log_file_path: String,
    /// 起動画面へ戻せる状態かどうかです。
    pub can_return_to_startup_screen: bool,
}
