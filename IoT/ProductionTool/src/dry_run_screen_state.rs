//! `ProductionTool` dry-run 実行・結果画面（`PT-005`）の状態モジュール。
//!
//! [重要] 本ファイルは `PT-005` dry-run 実行・結果画面の状態を定義します。
//! 主な仕様:
//! - eFuse / Secure Boot / Flash Encryption の本実行前に必ず停止する
//! - 導線、認証、対象機確認、監査ログ出力、停止点を確認する
//! - `ProductionTool` が責任を持つ不可逆段階計画を、実コマンド起動前に利用者へ明示する
//! 制限事項:
//! - dry-run 成功をもって不可逆処理本体完了とみなさない
//! - 現フェーズは「実行ステップの事前確認」と「段階計画表示」までで、`espefuse` 等の実行は後続フェーズ

use chrono::Local;
use serde::Serialize;

/// dry-run ステップの結果を表します。
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
#[allow(dead_code)] // Failed は将来フェーズで使用する
pub enum DryRunStepResult {
    /// 確認 OK です。
    Ok,
    /// スキップしました（現フェーズ未実装）。
    Skipped,
    /// 確認 NG です（将来フェーズで使用）。
    Failed { reason: String },
}

/// dry-run の各ステップを表します。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DryRunStep {
    /// ステップ番号です（1始まり）。
    pub step_number: u32,
    /// ステップ名です。
    pub step_name: String,
    /// ステップ説明です。
    pub step_description: String,
    /// ステップ結果です。
    pub step_result: DryRunStepResult,
}

/// dry-run 全体の完了状態を表します。
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
#[allow(dead_code)] // Aborted は将来フェーズで使用する
pub enum DryRunCompletionStatus {
    /// 全ステップ完了しました。
    Completed,
    /// いずれかのステップが失敗しました。
    FailedAtStep { step_number: u32 },
    /// 中断しました（将来フェーズで使用）。
    Aborted { reason: String },
}

/// `PT-005` dry-run 実行・結果画面の状態です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DryRunScreenState {
    /// dry-run 開始時刻です。
    pub started_at_local: String,
    /// dry-run 完了時刻です（完了時のみ）。
    pub finished_at_local: Option<String>,
    /// ステップ一覧です。
    pub steps: Vec<DryRunStep>,
    /// 完了状態です。
    pub completion_status: DryRunCompletionStatus,
    /// 監査ログファイルパスです。
    pub audit_log_file_path: String,
    /// 次画面（PT-006）へ進めるかどうかです。
    pub can_proceed_to_audit_log: bool,
    /// 監査ログ用ラベルです。
    pub audit_label: String,
}

/// 現フェーズの dry-run ステップ一覧を構築します。
///
/// [厳守] この段階では eFuse / Secure Boot / Flash Encryption の本実行手前で停止する。
pub fn build_dry_run_steps() -> Vec<DryRunStep> {
    vec![
        DryRunStep {
            step_number: 1,
            step_name: "起動確認".to_string(),
            step_description: "ProductionTool が独立起動し、SecretCore と通信できることを確認します。".to_string(),
            step_result: DryRunStepResult::Ok,
        },
        DryRunStep {
            step_number: 2,
            step_name: "追加認証確認".to_string(),
            step_description: "メーカーモード専用パスワードによる追加認証が成功していることを確認します。".to_string(),
            step_result: DryRunStepResult::Ok,
        },
        DryRunStep {
            step_number: 3,
            step_name: "対象機識別確認".to_string(),
            step_description: "シリアル番号・MAC アドレスの再入力が一致し、対象機取り違えがないことを確認します。".to_string(),
            step_result: DryRunStepResult::Ok,
        },
        DryRunStep {
            step_number: 4,
            step_name: "監査ログ初期化確認".to_string(),
            step_description: "監査ログ出力先が初期化され、起動・認証・対象機確認が記録されていることを確認します。".to_string(),
            step_result: DryRunStepResult::Ok,
        },
        DryRunStep {
            step_number: 5,
            step_name: "ProductionTool 所有の不可逆段階計画確認".to_string(),
            step_description: "[厳守] FE -> NVS暗号化なし確認（非焼込み） -> SB -> 封鎖の順序を ProductionTool 側の段階計画として確認し、実コマンド起動の直前で停止します。".to_string(),
            // [厳守] 現フェーズでは必ず Skipped とする。本実装は後続フェーズで追加する。
            step_result: DryRunStepResult::Skipped,
        },
    ]
}

/// dry-run を実行し、`PT-005` 画面状態を構築します。
///
/// # 引数
/// - `steps`: 実行対象ステップ一覧
/// - `audit_log_file_path`: 監査ログファイルパス
///
/// # 戻り値
/// `DryRunScreenState`
pub fn run_dry_run(steps: Vec<DryRunStep>, audit_log_file_path: &str) -> DryRunScreenState {
    let started_at = Local::now().to_rfc3339();

    // NG ステップを先頭から探す
    let failed_step = steps.iter().find(|step| {
        matches!(&step.step_result, DryRunStepResult::Failed { .. })
    });

    let completion_status = if let Some(failed) = failed_step {
        DryRunCompletionStatus::FailedAtStep {
            step_number: failed.step_number,
        }
    } else {
        DryRunCompletionStatus::Completed
    };

    let can_proceed = completion_status == DryRunCompletionStatus::Completed;
    let finished_at = Some(Local::now().to_rfc3339());

    let audit_label = match &completion_status {
        DryRunCompletionStatus::Completed => "dryRun_completed".to_string(),
        DryRunCompletionStatus::FailedAtStep { step_number } => {
            format!("dryRun_failed_at_step={}", step_number)
        }
        DryRunCompletionStatus::Aborted { reason } => {
            format!("dryRun_aborted reason={}", reason)
        }
    };

    DryRunScreenState {
        started_at_local: started_at,
        finished_at_local: finished_at,
        steps,
        completion_status,
        audit_log_file_path: audit_log_file_path.to_string(),
        can_proceed_to_audit_log: can_proceed,
        audit_label,
    }
}
