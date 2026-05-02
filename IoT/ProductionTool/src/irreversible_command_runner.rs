//! `ProductionTool` 不可逆コマンドランナーモジュール。
//!
//! [重要] 本ファイルは `ProductionTool` が eFuse / Secure Boot / Flash Encryption の
//! 実コマンド実行責任を持つための実装層です。
//! 主な仕様:
//! - `irreversible_stage_plan.rs` の段階順に対応するコマンドテンプレートを構築する
//! - 実行に必要な環境変数の有無を検査する
//! - `PC-004` 用の鍵ID・署名素材ID・ロット期待値が設定済みかどうかも実行前に検査する
//! - 既定ではプレビュー表示のみとし、明示的な環境変数と操作者入力が揃った場合のみ実行する
//! - 段階単位（例: Stage 1 のみ）で実行対象を絞り、段階ごとに readback/evidence/stability を閉じられる
//! - 実行時は `ProductionTool` 側で証跡ディレクトリ作成、コマンド展開、標準出力/標準エラー保存を行う
//! - 実行後は段階ごとの `readback/evidence` 要約と、stability 入力受け皿用の記録ひな形を扱える
//! - `DIS_USB_SERIAL_JTAG` は既定で保留し、`PRODUCTION_TOOL_BURN_DIS_USB_SERIAL_JTAG=1` のときのみ
//!   段階4の最終クローズ候補として追加する
//! 制限事項:
//! - `precheck -> stage_execute -> readback -> evidence -> stability` のうち、現段階で自動化できるのは
//!   `stage_execute/readback/evidence` の最小骨格まで。`NVS暗号化なし確認`、AP 認証、安定性確認は手動観測を残す
//! - Windows CLI (`cmd /C`) 前提で実行する。非 Windows ではプレビューのみ
//! - 鍵ファイル実パスや秘密値は UI へ出さず、監査上必要な範囲の実行コマンドと結果だけを証跡へ残す
//! - 不可逆コマンドは `PRODUCTION_TOOL_ALLOW_IRREVERSIBLE_EXECUTION=1` がない限り実行しない
//! 変更履歴:
//! - 2026-04-29: `ProductionTool` が不可逆工程の実行責任を持つ王道設計へ収束するため、プレビュー層として追加。
//! - 2026-04-29: NVS 段階から HMAC 鍵投入テンプレートを削除。理由: 現行案Aでは NVS Encryption / HMAC eFuse 投入を本番不可逆として実施しないため。
//! - 2026-04-29: `espefuse summary` の出力テンプレートを段階別ファイル名へ変更。理由: 実ランナー実装時に before/after 証跡の上書きを防ぐため。
//! - 2026-05-02: 既定無効の最小実ランナーを追加。理由: `ProductionTool` 管理下で証跡ディレクトリ作成、コマンド展開、実行ログ保存まで閉じる第一歩を実装するため。
//! - 2026-05-02: stability 入力受け皿用の記録構造と JSON 保存を追加。理由: 各段階の `OTA x2 / AP 10分 / STA 10分` を `ProductionTool` 側で回収・保存するため。
//! - 2026-05-02: `PC-004` 期待値の precheck を追加。理由: 不可逆直前ではなく、`PT-005z precheck` の段階で鍵ID・署名素材ID・ロットの設定漏れを見えるようにするため。
//! - 2026-05-02: 段階指定実行を追加。理由: 本番作業で全段一括ではなく、各段階の証跡・安定性確認を閉じてから次段へ進めるため。

use chrono::Local;
use serde::{Deserialize, Serialize};
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::irreversible_stage_plan::{IrreversibleStageKind, IrreversibleStagePlan};

/// 不可逆コマンドテンプレートです。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleCommandTemplate {
    /// 対応する段階番号です。
    pub stage_number: u32,
    /// コマンド番号です。
    pub command_number: u32,
    /// コマンド名です。
    pub command_name: String,
    /// コマンドテンプレートです。実値ではなく環境変数名を含みます。
    pub command_template: String,
    /// コマンドの実行種別です。
    pub execution_kind: IrreversibleCommandExecutionKind,
    /// 不可逆操作を含むかどうかです。
    pub is_irreversible: bool,
    /// このコマンドに必要な環境変数名一覧です。
    pub required_env_var_list: Vec<String>,
    /// このコマンドの実行証跡ファイル名です。
    pub evidence_file_name: String,
}

/// コマンドの実行種別です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum IrreversibleCommandExecutionKind {
    /// シェルコマンドとして実行できる項目です。
    ShellCommand,
    /// 画面、シリアル、AP 操作などの手動確認が必要な項目です。
    ManualCheck,
}

/// 実行済みコマンドの結果状態です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum IrreversibleCommandExecutionStatus {
    /// 正常終了しました。
    Completed,
    /// 手動確認待ちで停止しました。
    ManualCheckRequired,
    /// 安全ゲートで停止しました。
    StoppedBySafetyGate,
    /// 実行に失敗しました。
    Failed,
}

/// 実行済みコマンド1件分の記録です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleCommandExecutionRecord {
    /// 対応する段階番号です。
    pub stage_number: u32,
    /// コマンド番号です。
    pub command_number: u32,
    /// コマンド名です。
    pub command_name: String,
    /// 実行種別です。
    pub execution_kind: IrreversibleCommandExecutionKind,
    /// 不可逆操作かどうかです。
    pub is_irreversible: bool,
    /// 展開後コマンドです。手動確認項目では空文字列です。
    pub expanded_command: String,
    /// 実行結果です。
    pub execution_status: IrreversibleCommandExecutionStatus,
    /// 終了コードです。実行していない場合は `None` です。
    pub exit_code: Option<i32>,
    /// 実行証跡ファイルパスです。
    pub evidence_file_path: String,
    /// 補足メッセージです。
    pub detail_message: String,
}

/// 不可逆コマンドランナーのプレビュー結果です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleCommandRunnerPreview {
    /// ランナーが実コマンド起動を許可されているかどうかです。
    pub command_execution_armed: bool,
    /// この実装が実際にコマンドを起動するかどうかです（現フェーズでは常に false）。
    pub command_execution_supported: bool,
    /// 不可逆コマンド実行が明示的に許可されているかどうかです。
    pub irreversible_execution_allowed: bool,
    /// 不足している環境変数名一覧です。
    pub missing_env_var_list: Vec<String>,
    /// コマンドテンプレート一覧です。
    pub command_template_list: Vec<IrreversibleCommandTemplate>,
    /// 監査ログ用ラベルです。
    pub audit_label: String,
}

/// 実行前チェック1件分の結果です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleCommandRunnerPrecheckItem {
    /// 確認項目名です。
    pub check_name: String,
    /// 確認対象です。
    pub target_label: String,
    /// 確認結果です。
    pub passed: bool,
    /// 補足メッセージです。
    pub detail_message: String,
}

/// 不可逆コマンドランナーの実行前チェック結果です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleCommandRunnerPrecheckResult {
    /// 実行前チェックを通過したかどうかです。
    pub can_proceed: bool,
    /// 証跡ディレクトリです。未確定時は空文字列です。
    pub evidence_dir_path: String,
    /// 確認項目一覧です。
    pub check_item_list: Vec<IrreversibleCommandRunnerPrecheckItem>,
    /// 監査ログ用ラベルです。
    pub audit_label: String,
}

/// 不可逆コマンドランナーの実行結果です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleCommandRunnerExecution {
    /// 実行開始日時です。
    pub started_at_local: String,
    /// 実行終了日時です。
    pub finished_at_local: String,
    /// 証跡ディレクトリです。
    pub evidence_dir_path: String,
    /// 実行対象として指定された段階番号です。全段実行の場合は `None` です。
    pub selected_stage_number: Option<u32>,
    /// 実行結果一覧です。
    pub command_record_list: Vec<IrreversibleCommandExecutionRecord>,
    /// 段階ごとの要約一覧です。
    pub stage_summary_list: Vec<IrreversibleStageExecutionSummary>,
    /// 手動確認待ちで停止したかどうかです。
    pub stopped_for_manual_check: bool,
    /// 安全ゲートで停止したかどうかです。
    pub stopped_by_safety_gate: bool,
    /// 監査ログ用ラベルです。
    pub audit_label: String,
}

/// 安定性確認の結果状態です。
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum StabilityObservationStatus {
    /// 合格です。
    Passed,
    /// 不合格です。
    Failed,
    /// 未実施または保留です。
    Skipped,
}

/// 段階ごとの安定性確認記録です。
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleStageStabilityRecord {
    /// 段階番号です。
    pub stage_number: u32,
    /// 段階名です。
    pub stage_name: String,
    /// 安定性確認を要求する対象かどうかです。
    pub requires_stability_check: bool,
    /// OTA 2回連続の結果です。
    pub ota_twice_status: StabilityObservationStatus,
    /// AP 10分の結果です。
    pub ap_ten_minutes_status: StabilityObservationStatus,
    /// STA 10分の結果です。
    pub sta_ten_minutes_status: StabilityObservationStatus,
    /// OTA 側の証跡パスまたはメモです。
    pub ota_evidence_path: String,
    /// AP 側の証跡パスまたはメモです。
    pub ap_evidence_path: String,
    /// STA 側の証跡パスまたはメモです。
    pub sta_evidence_path: String,
    /// 次段へ進めるかどうかです。
    pub can_proceed_to_next_stage: bool,
    /// 補足メモです。
    pub note_text: String,
}

/// 段階ごとの実行要約です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleStageExecutionSummary {
    /// 段階番号です。
    pub stage_number: u32,
    /// 段階名です。
    pub stage_name: String,
    /// 実行対象コマンド数です。
    pub total_command_count: usize,
    /// 実行済みまたは停止記録済みのコマンド数です。
    pub recorded_command_count: usize,
    /// before readback があるかどうかです。
    pub has_before_readback: bool,
    /// after readback があるかどうかです。
    pub has_after_readback: bool,
    /// 失敗があるかどうかです。
    pub has_failed_command: bool,
    /// 手動確認待ち停止があるかどうかです。
    pub has_manual_check_stop: bool,
    /// 安全ゲート停止があるかどうかです。
    pub has_safety_gate_stop: bool,
    /// 証跡ファイル一覧です。
    pub evidence_file_path_list: Vec<String>,
    /// 段階要約メッセージです。
    pub detail_message: String,
}

/// 実行時に使用する安全設定です。
#[derive(Debug, Clone)]
struct IrreversibleCommandRunnerRuntimeSettings {
    /// 実ランナーが起動条件を満たしているかどうかです。
    command_execution_armed: bool,
    /// Windows 実行が可能かどうかです。
    command_execution_supported: bool,
    /// 不可逆コマンドまで許可するかどうかです。
    irreversible_execution_allowed: bool,
    /// 証跡保存先です。
    evidence_dir_path: PathBuf,
}

/// 不可逆コマンドランナーのプレビューを構築します。
///
/// # 引数
/// - `stage_plan`: `ProductionTool` 所有の不可逆段階計画。
///
/// # 戻り値
/// 実行前に必要な環境変数と、段階別コマンドテンプレートです。
pub fn build_irreversible_command_runner_preview(
    stage_plan: &IrreversibleStagePlan,
) -> IrreversibleCommandRunnerPreview {
    let command_template_list = build_command_templates(stage_plan);
    let mut required_env_var_list = vec![
        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
        "PRODUCTION_TOOL_EVIDENCE_DIR".to_string(),
        "PRODUCTION_TOOL_FE_KEY_ID".to_string(),
        "PRODUCTION_TOOL_FLASH_ARGS_STAGE1".to_string(),
        "PRODUCTION_TOOL_FLASH_ARGS_STAGE3".to_string(),
        "PRODUCTION_TOOL_SB_SIGNING_MATERIAL_ID".to_string(),
        "PRODUCTION_TOOL_TARGET_LOT_ID".to_string(),
    ];
    for command_template in &command_template_list {
        for env_var_name in &command_template.required_env_var_list {
            if !required_env_var_list.contains(env_var_name) {
                required_env_var_list.push(env_var_name.clone());
            }
        }
    }
    required_env_var_list.sort();

    let missing_env_var_list = required_env_var_list
        .iter()
        .filter(|env_var_name| env::var(env_var_name).unwrap_or_default().trim().is_empty())
        .cloned()
        .collect::<Vec<_>>();
    let command_execution_armed = env::var("PRODUCTION_TOOL_ENABLE_IRREVERSIBLE_COMMAND_RUNNER")
        .unwrap_or_default()
        .eq_ignore_ascii_case("1")
        && missing_env_var_list.is_empty();
    let command_execution_supported = cfg!(windows);
    let irreversible_execution_allowed = env::var("PRODUCTION_TOOL_ALLOW_IRREVERSIBLE_EXECUTION")
        .unwrap_or_default()
        .eq_ignore_ascii_case("1");

    IrreversibleCommandRunnerPreview {
        command_execution_armed,
        command_execution_supported,
        irreversible_execution_allowed,
        missing_env_var_list: missing_env_var_list.clone(),
        command_template_list,
        audit_label: format!(
            "irreversible_command_runner_preview armed={} supported={} irreversibleAllowed={} missingEnvCount={}",
            command_execution_armed,
            command_execution_supported,
            irreversible_execution_allowed,
            missing_env_var_list.len()
        ),
    }
}

/// 不可逆コマンドランナーの実行前チェックを行います。
///
/// # 引数
/// - `stage_plan`: `ProductionTool` 所有の不可逆段階計画。
///
/// # 戻り値
/// 実行前チェック結果です。必要に応じて証跡ディレクトリへ precheck 結果も保存します。
pub fn run_irreversible_command_runner_precheck(
    stage_plan: &IrreversibleStagePlan,
) -> IrreversibleCommandRunnerPrecheckResult {
    let preview = build_irreversible_command_runner_preview(stage_plan);
    let evidence_dir_path = env::var("PRODUCTION_TOOL_EVIDENCE_DIR").unwrap_or_default();
    let mut check_item_list = Vec::new();

    check_item_list.push(IrreversibleCommandRunnerPrecheckItem {
        check_name: "runnerArmed".to_string(),
        target_label: "PRODUCTION_TOOL_ENABLE_IRREVERSIBLE_COMMAND_RUNNER".to_string(),
        passed: preview.command_execution_armed,
        detail_message: if preview.command_execution_armed {
            "runner is armed.".to_string()
        } else {
            format!(
                "runner is not armed. missingEnv={}",
                preview.missing_env_var_list.join(", ")
            )
        },
    });
    check_item_list.push(IrreversibleCommandRunnerPrecheckItem {
        check_name: "executionSupported".to_string(),
        target_label: "windows-cmd".to_string(),
        passed: preview.command_execution_supported,
        detail_message: if preview.command_execution_supported {
            "Windows cmd execution is supported.".to_string()
        } else {
            "Current platform does not support irreversible command execution.".to_string()
        },
    });
    check_item_list.push(IrreversibleCommandRunnerPrecheckItem {
        check_name: "evidenceDirConfigured".to_string(),
        target_label: "PRODUCTION_TOOL_EVIDENCE_DIR".to_string(),
        passed: !evidence_dir_path.trim().is_empty(),
        detail_message: if evidence_dir_path.trim().is_empty() {
            "PRODUCTION_TOOL_EVIDENCE_DIR is empty.".to_string()
        } else {
            format!("configured: {}", evidence_dir_path)
        },
    });

    if !evidence_dir_path.trim().is_empty() {
        let evidence_dir = PathBuf::from(&evidence_dir_path);
        let create_directory_result = ensure_evidence_directory(&evidence_dir);
        check_item_list.push(IrreversibleCommandRunnerPrecheckItem {
            check_name: "evidenceDirWritable".to_string(),
            target_label: evidence_dir.display().to_string(),
            passed: create_directory_result.is_ok(),
            detail_message: match create_directory_result {
                Ok(_) => "evidence directory is writable.".to_string(),
                Err(detail_message) => detail_message,
            },
        });
    }

    check_item_list.push(IrreversibleCommandRunnerPrecheckItem {
        check_name: "pythonCommandAvailable".to_string(),
        target_label: "python".to_string(),
        passed: command_is_available("python"),
        detail_message: if command_is_available("python") {
            "python command is available.".to_string()
        } else {
            "python command is not available.".to_string()
        },
    });

    for value_check_item in build_required_value_precheck_item_list() {
        check_item_list.push(value_check_item);
    }
    for execution_mode_check_item in build_execution_mode_precheck_item_list() {
        check_item_list.push(execution_mode_check_item);
    }

    for file_check_item in build_optional_file_precheck_item_list(&preview) {
        check_item_list.push(file_check_item);
    }

    let can_proceed = check_item_list.iter().all(|check_item| check_item.passed);
    let precheck_result = IrreversibleCommandRunnerPrecheckResult {
        can_proceed,
        evidence_dir_path: evidence_dir_path.clone(),
        check_item_list,
        audit_label: format!(
            "irreversible_command_runner_precheck canProceed={} evidenceDirConfigured={}",
            can_proceed,
            !evidence_dir_path.trim().is_empty()
        ),
    };

    if !evidence_dir_path.trim().is_empty() {
        let precheck_summary_file_path =
            PathBuf::from(&evidence_dir_path).join("runner-precheck-summary.json");
        let _ = fs::write(
            &precheck_summary_file_path,
            serde_json::to_string_pretty(&precheck_result).unwrap_or_else(|_| {
                "{\"error\":\"precheckResultSerializationFailed\"}".to_string()
            }),
        );
    }

    precheck_result
}

/// 不可逆コマンドランナーを指定段階だけ実行します。
///
/// # 引数
/// - `stage_plan`: `ProductionTool` 所有の不可逆段階計画。
/// - `selected_stage_number`: 実行対象の段階番号です。
///
/// # 戻り値
/// 指定段階に絞った実行結果です。
pub fn run_irreversible_command_runner_for_stage(
    stage_plan: &IrreversibleStagePlan,
    selected_stage_number: u32,
) -> Result<IrreversibleCommandRunnerExecution, String> {
    run_irreversible_command_runner_with_stage_filter(stage_plan, Some(selected_stage_number))
}

/// 不可逆コマンドランナーを全段または指定段階で実行します。
fn run_irreversible_command_runner_with_stage_filter(
    stage_plan: &IrreversibleStagePlan,
    selected_stage_number: Option<u32>,
) -> Result<IrreversibleCommandRunnerExecution, String> {
    let precheck_result = run_irreversible_command_runner_precheck(stage_plan);
    if !precheck_result.can_proceed {
        return Err(format!(
            "run_irreversible_command_runner blocked by precheck. auditLabel={} evidenceDirPath={}",
            precheck_result.audit_label,
            precheck_result.evidence_dir_path
        ));
    }
    let preview = build_irreversible_command_runner_preview(stage_plan);
    let runtime_settings = load_runtime_settings_from_preview(&preview)?;
    ensure_evidence_directory(&runtime_settings.evidence_dir_path)?;
    validate_selected_stage_number(stage_plan, selected_stage_number)?;
    validate_previous_stage_stability_gate(
        &runtime_settings.evidence_dir_path,
        selected_stage_number,
    )?;

    let started_at_local = Local::now().to_rfc3339();
    let mut command_record_list = Vec::new();
    let mut stopped_for_manual_check = false;
    let mut stopped_by_safety_gate = false;

    for command_template in &preview.command_template_list {
        if let Some(stage_number) = selected_stage_number {
            if command_template.stage_number != stage_number {
                continue;
            }
        }
        let evidence_file_path = runtime_settings
            .evidence_dir_path
            .join(&command_template.evidence_file_name);

        if matches!(
            command_template.execution_kind,
            IrreversibleCommandExecutionKind::ManualCheck
        ) {
            let detail_message = format!(
                "manual check required. stage={} command={} check={}",
                command_template.stage_number,
                command_template.command_number,
                command_template.command_template
            );
            write_execution_log(
                &evidence_file_path,
                "",
                None,
                "",
                "",
                &detail_message,
            )?;
            command_record_list.push(IrreversibleCommandExecutionRecord {
                stage_number: command_template.stage_number,
                command_number: command_template.command_number,
                command_name: command_template.command_name.clone(),
                execution_kind: command_template.execution_kind.clone(),
                is_irreversible: command_template.is_irreversible,
                expanded_command: String::new(),
                execution_status: IrreversibleCommandExecutionStatus::ManualCheckRequired,
                exit_code: None,
                evidence_file_path: evidence_file_path.display().to_string(),
                detail_message,
            });
            stopped_for_manual_check = true;
            break;
        }

        let expanded_command = expand_env_placeholders(&command_template.command_template)?;
        if command_template.is_irreversible && !runtime_settings.irreversible_execution_allowed {
            let detail_message = format!(
                "safety gate stopped before irreversible command. env=PRODUCTION_TOOL_ALLOW_IRREVERSIBLE_EXECUTION stage={} command={}",
                command_template.stage_number,
                command_template.command_number
            );
            write_execution_log(
                &evidence_file_path,
                &expanded_command,
                None,
                "",
                "",
                &detail_message,
            )?;
            command_record_list.push(IrreversibleCommandExecutionRecord {
                stage_number: command_template.stage_number,
                command_number: command_template.command_number,
                command_name: command_template.command_name.clone(),
                execution_kind: command_template.execution_kind.clone(),
                is_irreversible: command_template.is_irreversible,
                expanded_command,
                execution_status: IrreversibleCommandExecutionStatus::StoppedBySafetyGate,
                exit_code: None,
                evidence_file_path: evidence_file_path.display().to_string(),
                detail_message,
            });
            stopped_by_safety_gate = true;
            break;
        }

        let command_output =
            execute_shell_command(&expanded_command).map_err(|detail_message| {
                format!(
                    "run_irreversible_command_runner failed. stage={} command={} detail={}",
                    command_template.stage_number, command_template.command_number, detail_message
                )
            })?;

        write_execution_log(
            &evidence_file_path,
            &expanded_command,
            Some(command_output.exit_code),
            &command_output.stdout_text,
            &command_output.stderr_text,
            &command_output.detail_message,
        )?;

        let execution_status = if command_output.exit_code == 0 {
            IrreversibleCommandExecutionStatus::Completed
        } else {
            IrreversibleCommandExecutionStatus::Failed
        };

        let record = IrreversibleCommandExecutionRecord {
            stage_number: command_template.stage_number,
            command_number: command_template.command_number,
            command_name: command_template.command_name.clone(),
            execution_kind: command_template.execution_kind.clone(),
            is_irreversible: command_template.is_irreversible,
            expanded_command,
            execution_status,
            exit_code: Some(command_output.exit_code),
            evidence_file_path: evidence_file_path.display().to_string(),
            detail_message: command_output.detail_message.clone(),
        };
        let should_stop = matches!(
            record.execution_status,
            IrreversibleCommandExecutionStatus::Failed
        );
        command_record_list.push(record);
        if should_stop {
            break;
        }
    }

    let finished_at_local = Local::now().to_rfc3339();
    let stage_summary_list = build_stage_summary_list(stage_plan, &command_record_list);
    write_execution_summary_file(
        &runtime_settings.evidence_dir_path,
        &command_record_list,
        &stage_summary_list,
        stopped_for_manual_check,
        stopped_by_safety_gate,
        selected_stage_number,
    )?;
    Ok(IrreversibleCommandRunnerExecution {
        started_at_local,
        finished_at_local,
        evidence_dir_path: runtime_settings.evidence_dir_path.display().to_string(),
        selected_stage_number,
        command_record_list,
        stage_summary_list,
        stopped_for_manual_check,
        stopped_by_safety_gate,
        audit_label: format!(
            "irreversible_command_runner_execution armed={} supported={} irreversibleAllowed={} selectedStage={:?} manualStop={} safetyStop={}",
            runtime_settings.command_execution_armed,
            runtime_settings.command_execution_supported,
            runtime_settings.irreversible_execution_allowed,
            selected_stage_number,
            stopped_for_manual_check,
            stopped_by_safety_gate
        ),
    })
}

/// 段階計画からコマンドテンプレート一覧を構築します。
///
/// # 引数
/// - `stage_plan`: `ProductionTool` 所有の不可逆段階計画。
///
/// # 戻り値
/// 各段階に対応するコマンドテンプレート一覧です。
fn build_command_templates(stage_plan: &IrreversibleStagePlan) -> Vec<IrreversibleCommandTemplate> {
    let mut template_list = Vec::new();
    for stage in &stage_plan.stage_list {
        match stage.stage_kind {
            IrreversibleStageKind::FlashEncryption => {
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    1,
                    "FE前 eFuse summary",
                );
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 2,
                    command_name: "FE鍵投入".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_key BLOCK_KEY0 %PRODUCTION_TOOL_FE_KEY_PATH% XTS_AES_128_KEY"
                            .to_string(),
                    execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
                    is_irreversible: true,
                    required_env_var_list: vec![
                        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
                        "PRODUCTION_TOOL_FE_KEY_PATH".to_string(),
                    ],
                    evidence_file_name: "stage01-command02-fe-key-burn.txt".to_string(),
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 3,
                    command_name: "FE有効化と関連eFuse".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse SPI_BOOT_CRYPT_CNT 0x7 && python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse DIS_DOWNLOAD_MANUAL_ENCRYPT DIS_DOWNLOAD_ICACHE DIS_DOWNLOAD_DCACHE DIS_DIRECT_BOOT"
                            .to_string(),
                    execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
                    is_irreversible: true,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                    evidence_file_name: "stage01-command03-fe-efuse-enable.txt".to_string(),
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 4,
                    command_name: "暗号化済みバイナリ書込み".to_string(),
                    command_template:
                        "python -m esptool --chip esp32s3 --port %PRODUCTION_TOOL_SERIAL_PORT% write_flash %PRODUCTION_TOOL_FLASH_ARGS_STAGE1%"
                            .to_string(),
                    execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
                    is_irreversible: false,
                    required_env_var_list: vec![
                        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
                        "PRODUCTION_TOOL_FLASH_ARGS_STAGE1".to_string(),
                    ],
                    evidence_file_name: "stage01-command04-stage1-flash.txt".to_string(),
                });
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    5,
                    "FE後 eFuse summary",
                );
            }
            IrreversibleStageKind::NvsPlaintextValidation => {
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    1,
                    "NVS前 eFuse summary",
                );
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 2,
                    command_name: "NVS暗号化なし方針確認".to_string(),
                    command_template:
                        "確認: NVS Encryption / HMAC eFuse burn は案Aでは実行しない。serial/APログで patternA と AP認証を確認"
                            .to_string(),
                    execution_kind: IrreversibleCommandExecutionKind::ManualCheck,
                    is_irreversible: false,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                    evidence_file_name: "stage02-command02-nvs-plaintext-manual-check.txt"
                        .to_string(),
                });
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    3,
                    "NVS後 eFuse summary",
                );
            }
            IrreversibleStageKind::SecureBootV2 => {
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    1,
                    "SB前 eFuse summary",
                );
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 2,
                    command_name: "Secure Boot digest投入".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_key_digest BLOCK_KEY1 %PRODUCTION_TOOL_SB_SIGNING_KEY_PATH% SECURE_BOOT_DIGEST0"
                            .to_string(),
                    execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
                    is_irreversible: true,
                    required_env_var_list: vec![
                        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
                        "PRODUCTION_TOOL_SB_SIGNING_KEY_PATH".to_string(),
                    ],
                    evidence_file_name: "stage03-command02-sb-digest-burn.txt".to_string(),
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 3,
                    command_name: "Secure Boot有効化とrevoke".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse SECURE_BOOT_EN SECURE_BOOT_KEY_REVOKE1 SECURE_BOOT_KEY_REVOKE2"
                            .to_string(),
                    execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
                    is_irreversible: true,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                    evidence_file_name: "stage03-command03-sb-enable-and-revoke.txt"
                        .to_string(),
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 4,
                    command_name: "RD_DIS write-protect".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% write_protect_efuse RD_DIS"
                            .to_string(),
                    execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
                    is_irreversible: true,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                    evidence_file_name: "stage03-command04-rd-dis-write-protect.txt"
                        .to_string(),
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 5,
                    command_name: "署名済みバイナリ書込み".to_string(),
                    command_template:
                        "python -m esptool --chip esp32s3 --port %PRODUCTION_TOOL_SERIAL_PORT% write_flash %PRODUCTION_TOOL_FLASH_ARGS_STAGE3%"
                            .to_string(),
                    execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
                    is_irreversible: false,
                    required_env_var_list: vec![
                        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
                        "PRODUCTION_TOOL_FLASH_ARGS_STAGE3".to_string(),
                    ],
                    evidence_file_name: "stage03-command05-stage3-flash.txt".to_string(),
                });
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    6,
                    "SB後 eFuse summary",
                );
            }
            IrreversibleStageKind::FinalLockdown => {
                template_list.extend(build_final_lockdown_stage_command_template_list(
                    stage.stage_number,
                    should_burn_dis_usb_serial_jtag(),
                ));
            }
        }
    }
    template_list
}

/// 段階4(封鎖) 用コマンドテンプレート一覧を構築します。
fn build_final_lockdown_stage_command_template_list(
    stage_number: u32,
    burn_dis_usb_serial_jtag: bool,
) -> Vec<IrreversibleCommandTemplate> {
    let mut template_list = Vec::new();
    push_stage_summary_template(&mut template_list, stage_number, 1, "封鎖前 eFuse summary");
    template_list.push(IrreversibleCommandTemplate {
        stage_number,
        command_number: 2,
        command_name: "JTAG無効化".to_string(),
        command_template:
            "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse DIS_PAD_JTAG DIS_USB_JTAG"
                .to_string(),
        execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
        is_irreversible: true,
        required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
        evidence_file_name: "stage04-command02-disable-jtag.txt".to_string(),
    });
    let mut next_command_number = 3;
    if burn_dis_usb_serial_jtag {
        template_list.push(IrreversibleCommandTemplate {
            stage_number,
            command_number: next_command_number,
            command_name: "USB Serial/JTAG無効化".to_string(),
            command_template:
                "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse DIS_USB_SERIAL_JTAG"
                    .to_string(),
            execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
            is_irreversible: true,
            required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
            evidence_file_name: "stage04-command03-disable-usb-serial-jtag.txt".to_string(),
        });
        next_command_number += 1;
    }
    template_list.push(IrreversibleCommandTemplate {
        stage_number,
        command_number: next_command_number,
        command_name: "Security Download有効化".to_string(),
        command_template:
            "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse ENABLE_SECURITY_DOWNLOAD"
                .to_string(),
        execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
        is_irreversible: true,
        required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
        evidence_file_name: format!(
            "stage04-command{next_command_number:02}-enable-security-download.txt"
        ),
    });
    push_stage_summary_template(
        &mut template_list,
        stage_number,
        next_command_number + 1,
        "封鎖後 eFuse summary",
    );
    template_list
}

/// `espefuse summary` のテンプレートを追加します。
///
/// # 引数
/// - `template_list`: 追加先テンプレート一覧。
/// - `stage_number`: 段階番号。
/// - `command_number`: コマンド番号。
/// - `command_name`: コマンド名。
fn push_stage_summary_template(
    template_list: &mut Vec<IrreversibleCommandTemplate>,
    stage_number: u32,
    command_number: u32,
    command_name: &str,
) {
    let evidence_file_name =
        format!("stage{stage_number:02}-command{command_number:02}-efuse-summary.txt");
    template_list.push(IrreversibleCommandTemplate {
        stage_number,
        command_number,
        command_name: command_name.to_string(),
        command_template: "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% summary"
            .to_string(),
        execution_kind: IrreversibleCommandExecutionKind::ShellCommand,
        is_irreversible: false,
        required_env_var_list: vec![
            "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
            "PRODUCTION_TOOL_EVIDENCE_DIR".to_string(),
        ],
        evidence_file_name,
    });
}

/// 実行前に確認すべきファイル存在チェック一覧を構築します。
fn build_optional_file_precheck_item_list(
    preview: &IrreversibleCommandRunnerPreview,
) -> Vec<IrreversibleCommandRunnerPrecheckItem> {
    let mut check_item_list = Vec::new();
    let mut env_var_name_list = preview
        .command_template_list
        .iter()
        .flat_map(|command_template| command_template.required_env_var_list.iter().cloned())
        .collect::<Vec<_>>();
    env_var_name_list.sort();
    env_var_name_list.dedup();

    for env_var_name in env_var_name_list {
        if !env_var_name.ends_with("_PATH") {
            continue;
        }
        let env_var_value = env::var(&env_var_name).unwrap_or_default();
        if env_var_value.trim().is_empty() {
            check_item_list.push(IrreversibleCommandRunnerPrecheckItem {
                check_name: "requiredPathConfigured".to_string(),
                target_label: env_var_name.clone(),
                passed: false,
                detail_message: format!("{} is empty.", env_var_name),
            });
            continue;
        }

        let target_path = PathBuf::from(&env_var_value);
        let path_exists = target_path.exists();
        check_item_list.push(IrreversibleCommandRunnerPrecheckItem {
            check_name: "requiredPathExists".to_string(),
            target_label: env_var_name,
            passed: path_exists,
            detail_message: if path_exists {
                format!("path exists: {}", target_path.display())
            } else {
                format!("path does not exist: {}", target_path.display())
            },
        });
    }

    check_item_list
}

/// 実行前に確認すべき ID / ロット期待値の設定有無一覧を構築します。
fn build_required_value_precheck_item_list() -> Vec<IrreversibleCommandRunnerPrecheckItem> {
    let required_value_env_var_name_list = vec![
        "PRODUCTION_TOOL_FE_KEY_ID",
        "PRODUCTION_TOOL_SB_SIGNING_MATERIAL_ID",
        "PRODUCTION_TOOL_TARGET_LOT_ID",
    ];
    let mut check_item_list = Vec::new();

    for env_var_name in required_value_env_var_name_list {
        let env_var_value = env::var(env_var_name).unwrap_or_default();
        let configured = !env_var_value.trim().is_empty();
        check_item_list.push(IrreversibleCommandRunnerPrecheckItem {
            check_name: "requiredValueConfigured".to_string(),
            target_label: env_var_name.to_string(),
            passed: configured,
            detail_message: if configured {
                "configured.".to_string()
            } else {
                format!("{} is empty.", env_var_name)
            },
        });
    }

    check_item_list
}

/// 実行モードに関する補足確認一覧を構築します。
fn build_execution_mode_precheck_item_list() -> Vec<IrreversibleCommandRunnerPrecheckItem> {
    vec![
        IrreversibleCommandRunnerPrecheckItem {
            check_name: "serialClosureMode".to_string(),
            target_label: "PRODUCTION_TOOL_BURN_DIS_USB_SERIAL_JTAG".to_string(),
            passed: true,
            detail_message: if should_burn_dis_usb_serial_jtag() {
                "enabled: stage4 final closure includes DIS_USB_SERIAL_JTAG burn.".to_string()
            } else {
                "disabled: USB serial log remains available until final closure decision."
                    .to_string()
            },
        },
        IrreversibleCommandRunnerPrecheckItem {
            check_name: "serialOutputClosureReview".to_string(),
            target_label: "stage4-final-closure".to_string(),
            passed: true,
            detail_message: if should_burn_dis_usb_serial_jtag() {
                "required: after all irreversible steps, readback, evidence, and stability are confirmed, update to the normal production firmware with serial output restrictions before final shipment sign-off.".to_string()
            } else {
                "pending: final closure still requires updating to the normal production firmware with serial output restrictions; ProductionTool does not auto-burn serial output stop efuse.".to_string()
            },
        },
    ]
}

/// 実行前プレビューから実行時設定を組み立てます。
fn load_runtime_settings_from_preview(
    preview: &IrreversibleCommandRunnerPreview,
) -> Result<IrreversibleCommandRunnerRuntimeSettings, String> {
    if !preview.command_execution_supported {
        return Err(
            "load_runtime_settings_from_preview failed. irreversible command runner supports only Windows cmd execution in the current phase."
                .to_string(),
        );
    }
    if !preview.command_execution_armed {
        return Err(format!(
            "load_runtime_settings_from_preview failed. runner is not armed. missingEnv={}",
            preview.missing_env_var_list.join(", ")
        ));
    }
    let evidence_dir_raw = env::var("PRODUCTION_TOOL_EVIDENCE_DIR").unwrap_or_default();
    if evidence_dir_raw.trim().is_empty() {
        return Err(
            "load_runtime_settings_from_preview failed. PRODUCTION_TOOL_EVIDENCE_DIR is empty."
                .to_string(),
        );
    }

    Ok(IrreversibleCommandRunnerRuntimeSettings {
        command_execution_armed: preview.command_execution_armed,
        command_execution_supported: preview.command_execution_supported,
        irreversible_execution_allowed: preview.irreversible_execution_allowed,
        evidence_dir_path: PathBuf::from(evidence_dir_raw),
    })
}

/// 指定段階番号が段階計画内に存在することを確認します。
fn validate_selected_stage_number(
    stage_plan: &IrreversibleStagePlan,
    selected_stage_number: Option<u32>,
) -> Result<(), String> {
    let Some(stage_number) = selected_stage_number else {
        return Ok(());
    };
    if stage_plan
        .stage_list
        .iter()
        .any(|stage| stage.stage_number == stage_number)
    {
        Ok(())
    } else {
        Err(format!(
            "validate_selected_stage_number failed. selectedStageNumber={} is not defined in irreversible stage plan.",
            stage_number
        ))
    }
}

/// `DIS_USB_SERIAL_JTAG` を段階4で焼くかどうかを判定します。
fn should_burn_dis_usb_serial_jtag() -> bool {
    env::var("PRODUCTION_TOOL_BURN_DIS_USB_SERIAL_JTAG")
        .unwrap_or_default()
        .eq_ignore_ascii_case("1")
}

/// 段階指定実行時に、前段の stability 判定が次段進行を許可しているか確認します。
fn validate_previous_stage_stability_gate(
    evidence_dir_path: &Path,
    selected_stage_number: Option<u32>,
) -> Result<(), String> {
    let Some(stage_number) = selected_stage_number else {
        return Ok(());
    };
    if stage_number <= 1 {
        return Ok(());
    }

    let previous_stage_number = stage_number - 1;
    let stability_summary_file_path = evidence_dir_path.join("runner-stability-summary.json");
    if !stability_summary_file_path.exists() {
        return Err(format!(
            "validate_previous_stage_stability_gate failed. selectedStageNumber={} requires previous stage stability summary file={}, but it does not exist.",
            stage_number,
            stability_summary_file_path.display()
        ));
    }

    let stability_summary_text = fs::read_to_string(&stability_summary_file_path).map_err(
        |source_error| {
            format!(
                "validate_previous_stage_stability_gate failed. read stability summary filePath={} detail={}",
                stability_summary_file_path.display(),
                source_error
            )
        },
    )?;
    let stage_stability_record_list: Vec<IrreversibleStageStabilityRecord> =
        serde_json::from_str(&stability_summary_text).map_err(|source_error| {
            format!(
                "validate_previous_stage_stability_gate failed. parse stability summary filePath={} detail={}",
                stability_summary_file_path.display(),
                source_error
            )
        })?;

    let Some(previous_stage_record) = stage_stability_record_list
        .iter()
        .find(|stage_record| stage_record.stage_number == previous_stage_number)
    else {
        return Err(format!(
            "validate_previous_stage_stability_gate failed. previousStageNumber={} is not recorded in {}.",
            previous_stage_number,
            stability_summary_file_path.display()
        ));
    };

    if previous_stage_record.can_proceed_to_next_stage {
        Ok(())
    } else {
        Err(format!(
            "validate_previous_stage_stability_gate failed. previousStageNumber={} is not approved to proceed. noteText={}",
            previous_stage_number,
            previous_stage_record.note_text
        ))
    }
}

/// 証跡ディレクトリを作成します。
fn ensure_evidence_directory(evidence_dir_path: &Path) -> Result<(), String> {
    fs::create_dir_all(evidence_dir_path).map_err(|source_error| {
        format!(
            "ensure_evidence_directory failed. evidenceDirPath={} detail={}",
            evidence_dir_path.display(),
            source_error
        )
    })
}

/// `%ENV_NAME%` 形式の環境変数を実値へ展開します。
fn expand_env_placeholders(template: &str) -> Result<String, String> {
    let mut expanded_text = String::new();
    let character_list = template.chars().collect::<Vec<_>>();
    let mut current_index = 0;

    while current_index < character_list.len() {
        if character_list[current_index] != '%' {
            expanded_text.push(character_list[current_index]);
            current_index += 1;
            continue;
        }

        let variable_start_index = current_index + 1;
        let Some(relative_end_index) = character_list[variable_start_index..]
            .iter()
            .position(|character| *character == '%')
        else {
            expanded_text.push(character_list[current_index]);
            current_index += 1;
            continue;
        };
        let variable_end_index = variable_start_index + relative_end_index;
        let variable_name = character_list[variable_start_index..variable_end_index]
            .iter()
            .collect::<String>();
        if variable_name.trim().is_empty() {
            return Err(format!(
                "expand_env_placeholders failed. empty variable name in template={}",
                template
            ));
        }
        let variable_value = env::var(&variable_name).map_err(|_| {
            format!(
                "expand_env_placeholders failed. variableName={} is not set. template={}",
                variable_name, template
            )
        })?;
        expanded_text.push_str(&variable_value);
        current_index = variable_end_index + 1;
    }

    Ok(expanded_text)
}

/// コマンドが利用可能かどうかを確認します。
fn command_is_available(command_name: &str) -> bool {
    #[cfg(windows)]
    {
        Command::new("where")
            .arg(command_name)
            .output()
            .map(|output| output.status.success())
            .unwrap_or(false)
    }

    #[cfg(not(windows))]
    {
        let _ = command_name;
        false
    }
}

/// 実コマンドの標準出力/標準エラーを返す内部結果です。
struct ShellCommandOutput {
    /// 終了コードです。
    exit_code: i32,
    /// 標準出力です。
    stdout_text: String,
    /// 標準エラーです。
    stderr_text: String,
    /// 補足メッセージです。
    detail_message: String,
}

/// Windows `cmd /C` 経由でコマンドを実行します。
fn execute_shell_command(expanded_command: &str) -> Result<ShellCommandOutput, String> {
    #[cfg(windows)]
    {
        let command_output = Command::new("cmd")
            .args(["/C", expanded_command])
            .output()
            .map_err(|source_error| {
                format!(
                    "execute_shell_command failed. command={} detail={}",
                    expanded_command, source_error
                )
            })?;
        let exit_code = command_output.status.code().unwrap_or(-1);
        let stdout_text = String::from_utf8_lossy(&command_output.stdout).to_string();
        let stderr_text = String::from_utf8_lossy(&command_output.stderr).to_string();
        let detail_message = format!(
            "command finished. exitCode={} success={}",
            exit_code,
            command_output.status.success()
        );
        Ok(ShellCommandOutput {
            exit_code,
            stdout_text,
            stderr_text,
            detail_message,
        })
    }

    #[cfg(not(windows))]
    {
        let _ = expanded_command;
        Err("execute_shell_command failed. non-Windows platform is not supported.".to_string())
    }
}

/// 実行証跡を1ファイルへ保存します。
fn write_execution_log(
    evidence_file_path: &Path,
    expanded_command: &str,
    exit_code: Option<i32>,
    stdout_text: &str,
    stderr_text: &str,
    detail_message: &str,
) -> Result<(), String> {
    let file_content = format!(
        "recordedAtLocal={}\ncommand={}\nexitCode={}\ndetail={}\n\n[stdout]\n{}\n\n[stderr]\n{}\n",
        Local::now().to_rfc3339(),
        expanded_command,
        exit_code
            .map(|value| value.to_string())
            .unwrap_or_else(|| "none".to_string()),
        detail_message,
        stdout_text,
        stderr_text
    );
    fs::write(evidence_file_path, file_content).map_err(|source_error| {
        format!(
            "write_execution_log failed. evidenceFilePath={} detail={}",
            evidence_file_path.display(),
            source_error
        )
    })
}

/// 段階ごとの要約一覧を構築します。
fn build_stage_summary_list(
    stage_plan: &IrreversibleStagePlan,
    command_record_list: &[IrreversibleCommandExecutionRecord],
) -> Vec<IrreversibleStageExecutionSummary> {
    let preview = build_irreversible_command_runner_preview(stage_plan);
    let mut stage_summary_list = Vec::new();

    for stage in &stage_plan.stage_list {
        let stage_command_template_list = preview
            .command_template_list
            .iter()
            .filter(|command_template| command_template.stage_number == stage.stage_number)
            .collect::<Vec<_>>();
        let stage_command_record_list = command_record_list
            .iter()
            .filter(|command_record| command_record.stage_number == stage.stage_number)
            .collect::<Vec<_>>();

        let has_before_readback = stage_command_record_list.iter().any(|command_record| {
            command_record
                .command_name
                .contains("前 eFuse summary")
        });
        let has_after_readback = stage_command_record_list.iter().any(|command_record| {
            command_record
                .command_name
                .contains("後 eFuse summary")
        });
        let has_failed_command = stage_command_record_list.iter().any(|command_record| {
            matches!(
                command_record.execution_status,
                IrreversibleCommandExecutionStatus::Failed
            )
        });
        let has_manual_check_stop = stage_command_record_list.iter().any(|command_record| {
            matches!(
                command_record.execution_status,
                IrreversibleCommandExecutionStatus::ManualCheckRequired
            )
        });
        let has_safety_gate_stop = stage_command_record_list.iter().any(|command_record| {
            matches!(
                command_record.execution_status,
                IrreversibleCommandExecutionStatus::StoppedBySafetyGate
            )
        });
        let evidence_file_path_list = stage_command_record_list
            .iter()
            .map(|command_record| command_record.evidence_file_path.clone())
            .collect::<Vec<_>>();

        let detail_message = format!(
            "stage={} totalCommands={} recordedCommands={} beforeReadback={} afterReadback={} failed={} manualStop={} safetyStop={}",
            stage.stage_number,
            stage_command_template_list.len(),
            stage_command_record_list.len(),
            has_before_readback,
            has_after_readback,
            has_failed_command,
            has_manual_check_stop,
            has_safety_gate_stop
        );

        stage_summary_list.push(IrreversibleStageExecutionSummary {
            stage_number: stage.stage_number,
            stage_name: stage.stage_name.clone(),
            total_command_count: stage_command_template_list.len(),
            recorded_command_count: stage_command_record_list.len(),
            has_before_readback,
            has_after_readback,
            has_failed_command,
            has_manual_check_stop,
            has_safety_gate_stop,
            evidence_file_path_list,
            detail_message,
        });
    }

    stage_summary_list
}

/// 実行全体の要約 JSON を保存します。
fn write_execution_summary_file(
    evidence_dir_path: &Path,
    command_record_list: &[IrreversibleCommandExecutionRecord],
    stage_summary_list: &[IrreversibleStageExecutionSummary],
    stopped_for_manual_check: bool,
    stopped_by_safety_gate: bool,
    selected_stage_number: Option<u32>,
) -> Result<(), String> {
    #[derive(Serialize)]
    #[serde(rename_all = "camelCase")]
    struct ExecutionSummaryFile<'a> {
        command_record_count: usize,
        stopped_for_manual_check: bool,
        stopped_by_safety_gate: bool,
        selected_stage_number: Option<u32>,
        command_record_list: &'a [IrreversibleCommandExecutionRecord],
        stage_summary_list: &'a [IrreversibleStageExecutionSummary],
    }

    let summary_file_path = evidence_dir_path.join("runner-execution-summary.json");
    let summary = ExecutionSummaryFile {
        command_record_count: command_record_list.len(),
        stopped_for_manual_check,
        stopped_by_safety_gate,
        selected_stage_number,
        command_record_list,
        stage_summary_list,
    };
    fs::write(
        &summary_file_path,
        serde_json::to_string_pretty(&summary)
            .unwrap_or_else(|_| "{\"error\":\"executionSummarySerializationFailed\"}".to_string()),
    )
    .map_err(|source_error| {
        format!(
            "write_execution_summary_file failed. summaryFilePath={} detail={}",
            summary_file_path.display(),
            source_error
        )
    })
}

/// 安定性確認の候補一覧を構築します。
///
/// # 引数
/// - `stage_summary_list`: 段階ごとの実行要約一覧です。
///
/// # 戻り値
/// `ProductionTool` が回収すべき段階ごとの安定性確認記録ひな形です。
pub fn build_stage_stability_record_template_list(
    stage_summary_list: &[IrreversibleStageExecutionSummary],
) -> Vec<IrreversibleStageStabilityRecord> {
    stage_summary_list
        .iter()
        .map(|stage_summary| {
            let has_any_stage_activity = stage_summary.recorded_command_count > 0;
            let requires_stability_check = has_any_stage_activity
                && !stage_summary.has_failed_command
                && !stage_summary.has_safety_gate_stop;
            IrreversibleStageStabilityRecord {
                stage_number: stage_summary.stage_number,
                stage_name: stage_summary.stage_name.clone(),
                requires_stability_check,
                ota_twice_status: StabilityObservationStatus::Skipped,
                ap_ten_minutes_status: StabilityObservationStatus::Skipped,
                sta_ten_minutes_status: StabilityObservationStatus::Skipped,
                ota_evidence_path: String::new(),
                ap_evidence_path: String::new(),
                sta_evidence_path: String::new(),
                can_proceed_to_next_stage: false,
                note_text: if requires_stability_check {
                    if stage_summary.has_manual_check_stop {
                        "stage manual check 完了後の stability / 次段可否入力待ち".to_string()
                    } else {
                        "stage execute/readback 完了後の stability 入力待ち".to_string()
                    }
                } else {
                    "現時点では stability 対象外".to_string()
                },
            }
        })
        .collect::<Vec<_>>()
}

/// 安定性確認記録を JSON として保存します。
///
/// # 引数
/// - `evidence_dir_path`: 証跡保存先ディレクトリです。
/// - `stage_stability_record_list`: 安定性確認記録一覧です。
///
/// # 戻り値
/// 保存先 JSON ファイルパスです。
pub fn persist_stage_stability_record_list(
    evidence_dir_path: &Path,
    stage_stability_record_list: &[IrreversibleStageStabilityRecord],
) -> Result<PathBuf, String> {
    let stability_summary_file_path = evidence_dir_path.join("runner-stability-summary.json");
    fs::write(
        &stability_summary_file_path,
        serde_json::to_string_pretty(stage_stability_record_list).unwrap_or_else(|_| {
            "[{\"error\":\"stabilitySummarySerializationFailed\"}]".to_string()
        }),
    )
    .map_err(|source_error| {
        format!(
            "persist_stage_stability_record_list failed. filePath={} detail={}",
            stability_summary_file_path.display(),
            source_error
        )
    })?;
    Ok(stability_summary_file_path)
}

/// 不可逆コマンドランナーの補助関数テストです。
#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    /// 段階2以降は前段 stability の許可が必要であることを確認します。
    #[test]
    fn validate_previous_stage_stability_gate_requires_previous_stage_approval() {
        let unique_suffix = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let evidence_dir_path = std::env::temp_dir().join(format!(
            "production_tool_stage_gate_test_{}",
            unique_suffix
        ));
        fs::create_dir_all(&evidence_dir_path).unwrap_or_else(|error| {
            panic!(
                "create_dir_all failed. evidenceDirPath={} detail={}",
                evidence_dir_path.display(),
                error
            )
        });

        let stage_stability_record_list = vec![IrreversibleStageStabilityRecord {
            stage_number: 1,
            stage_name: "段階1(FE): Flash Encryption 系".to_string(),
            requires_stability_check: true,
            ota_twice_status: StabilityObservationStatus::Passed,
            ap_ten_minutes_status: StabilityObservationStatus::Passed,
            sta_ten_minutes_status: StabilityObservationStatus::Passed,
            ota_evidence_path: "ota-ok.txt".to_string(),
            ap_evidence_path: "ap-ok.txt".to_string(),
            sta_evidence_path: "sta-ok.txt".to_string(),
            can_proceed_to_next_stage: true,
            note_text: "stage1 approved".to_string(),
        }];
        persist_stage_stability_record_list(&evidence_dir_path, &stage_stability_record_list)
            .unwrap_or_else(|error| {
                panic!(
                    "persist_stage_stability_record_list failed. evidenceDirPath={} detail={}",
                    evidence_dir_path.display(),
                    error
                )
            });

        let validation_result =
            validate_previous_stage_stability_gate(&evidence_dir_path, Some(2));

        assert!(validation_result.is_ok());

        let _ = fs::remove_dir_all(&evidence_dir_path);
    }

    /// 手動確認待ち段階でも次段可否の入力対象になることを確認します。
    #[test]
    fn build_stage_stability_record_template_list_marks_manual_stop_stage_as_required() {
        let stage_summary_list = vec![IrreversibleStageExecutionSummary {
            stage_number: 2,
            stage_name: "段階2(NVS): 案A / NVS暗号化なし確認".to_string(),
            total_command_count: 3,
            recorded_command_count: 2,
            has_before_readback: true,
            has_after_readback: false,
            has_failed_command: false,
            has_manual_check_stop: true,
            has_safety_gate_stop: false,
            evidence_file_path_list: vec![
                "stage02-command01-efuse-summary.txt".to_string(),
                "stage02-command02-nvs-plaintext-manual-check.txt".to_string(),
            ],
            detail_message: "manual stop".to_string(),
        }];

        let stage_stability_record_list =
            build_stage_stability_record_template_list(&stage_summary_list);

        assert_eq!(stage_stability_record_list.len(), 1);
        assert!(stage_stability_record_list[0].requires_stability_check);
        assert!(
            stage_stability_record_list[0]
                .note_text
                .contains("manual check 完了後")
        );
    }

    /// 段階4では明示許可時のみ DIS_USB_SERIAL_JTAG を最終クローズ候補へ含めます。
    #[test]
    fn build_final_lockdown_stage_command_template_list_adds_usb_serial_closure_only_when_enabled() {
        let default_template_list =
            build_final_lockdown_stage_command_template_list(4, false);
        let final_closure_template_list =
            build_final_lockdown_stage_command_template_list(4, true);

        assert!(
            default_template_list
                .iter()
                .all(|command_template| command_template.command_name != "USB Serial/JTAG無効化")
        );
        assert!(
            final_closure_template_list
                .iter()
                .any(|command_template| command_template.command_name == "USB Serial/JTAG無効化")
        );
        assert_eq!(
            final_closure_template_list
                .iter()
                .find(|command_template| {
                    command_template.command_name == "Security Download有効化"
                })
                .map(|command_template| command_template.command_number),
            Some(4)
        );
    }
}
