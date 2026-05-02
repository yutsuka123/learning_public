//! `ProductionTool` 起動骨格（`004-0008` 完了 / `004-0009` 対応中）。
//!
//! [重要] 本ファイルは `ProductionTool` のメインエントリーポイントです。
//! 主な仕様:
//! - `LocalServer` と分離した独立実行物として起動する（PT-001）
//! - 設定を読み込み、監査ログ出力先を初期化し、起動記録を JSON で保存する
//! - `SecretCore` Named Pipe 到達確認を行い、`ProductionTool` 所有の不可逆段階計画を表示する
//! - 追加認証（PT-002）→ 対象機識別確認（PT-003）→ `PC-004` 鍵ID確認 → dry-run 確認（PT-005）
//! - オプションで PT-005a: `LocalServer` へ不可逆監査受理の HTTP POST（eFuse 実書込みはしない）
//! 制限事項:
//! - GUI 本体は未実装（現フェーズは CLI ウィザード）
//! - 現フェーズのパスワード照合は開発用簡易ハッシュを使用（将来 SHA-256 + pepper に置き換え）
//! - 対象機一覧は現フェーズ手動入力（将来 SecretCore IPC 自動取得へ置き換え）
//! - eFuse / Secure Boot / Flash Encryption の実コマンド起動ランナーは最小骨格のみ実装。
//!   既定では未実行で、`PRODUCTION_TOOL_ENABLE_IRREVERSIBLE_COMMAND_RUNNER=1` と操作者の `run` 入力、
//!   さらに不可逆コマンドでは `PRODUCTION_TOOL_ALLOW_IRREVERSIBLE_EXECUTION=1` が揃った場合のみ実行する。
//!   ただし責任主体は `ProductionTool` とし、現時点でも §6.4 の段階計画、precheck、
//!   段階別 readback/evidence 要約、stability 入力受け皿を `ProductionTool` 側で所有する。
//! 変更:
//! - 2026-03-18: `004-0009` 対応として PT-002 追加認証・PT-003 対象機確認・PT-005 dry-run のウィザードフローを追加。
//! - 2026-04-29: PT-005a で LocalServer へ不可逆監査受理記録するオプション（`production_workflow_handoff`）を追加。
//! - 2026-04-29: `irreversible_stage_plan` を追加し、不可逆工程の責任主体を `ProductionTool` としてコード上に固定。
//! - 2026-04-29: `irreversible_command_runner` を追加し、実コマンドランナーの環境変数検査とテンプレート表示を追加。
//! - 2026-05-02: `PT-005z` 最小実ランナーの起動導線を追加。理由: `ProductionTool` 管理下で証跡ディレクトリ作成、コマンド展開、実行ログ保存まで行えるようにするため。
//! - 2026-05-02: `PT-005z` の stability 入力受け皿を追加。理由: 各段階の `OTA x2 / AP 10分 / STA 10分` を `ProductionTool` 側で回収・保存し、一貫した段階判定へ寄せるため。
//! - 2026-05-02: `PC-004` 鍵ID確認の CLI 照合と JSON 証跡保存を追加。理由: 鍵ID・署名素材ID・ロット・作業指示番号の照合責任も `ProductionTool` に集約するため。

mod app_config;
mod audit_logger;
mod auth_screen_state;
mod device_select_screen_state;
mod dry_run_screen_state;
mod irreversible_command_runner;
mod irreversible_stage_plan;
mod pc004_check_state;
mod production_workflow_handoff;
mod startup_screen_state;

use app_config::{load_app_config, LoadedConfig};
use audit_logger::{write_startup_audit_log, StartupAuditLogRecord};
use auth_screen_state::{attempt_auth, prompt_auth_input, AuthAttemptResult};
use chrono::Local;
use device_select_screen_state::{
    build_placeholder_device_list, prompt_device_selection, verify_device_selection,
};
use dry_run_screen_state::{build_dry_run_steps, run_dry_run};
use irreversible_command_runner::{
    build_irreversible_command_runner_preview, build_stage_stability_record_template_list,
    persist_stage_stability_record_list, run_irreversible_command_runner_for_stage,
    run_irreversible_command_runner_precheck, IrreversibleCommandRunnerExecution,
    IrreversibleStageStabilityRecord, StabilityObservationStatus,
};
use irreversible_stage_plan::{build_irreversible_stage_plan, IrreversibleStagePlan};
use pc004_check_state::{
    load_pc004_expected_value_set_from_env, persist_pc004_check_state, prompt_pc004_input,
    verify_pc004_check,
};
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

/// `PT-002` 追加認証の実行ポリシーです。
#[derive(Debug, Clone)]
struct AuthPolicy {
    /// 許可する操作者 ID（空文字なら未固定）。
    expected_operator_id: String,
    /// 正解パスワードのハッシュ値です。
    expected_password_hash: String,
    /// 最大認証試行回数です。
    max_attempts: u32,
}

/// アプリ全体のエラーを表します。
#[derive(Debug)]
enum ProductionToolError {
    /// 現在の作業ディレクトリ取得に失敗しました。
    CurrentDirectoryResolveFailed { source_error: io::Error },
    /// 設定読込に失敗しました。
    ConfigLoadFailed { detail_message: String },
    /// `.env` 読込に失敗しました。
    RuntimeEnvLoadFailed { detail_message: String },
    /// 追加認証設定の読込/検証に失敗しました。
    AuthPolicyLoadFailed { detail_message: String },
    /// 監査ログ出力に失敗しました。
    AuditWriteFailed { detail_message: String },
    /// `SecretCore` 事前確認に失敗しました。
    SecretCorePreflightFailed { detail_message: String },
    /// 追加認証に失敗しました（PT-007 安全停止）。
    AuthFailed { detail_message: String },
    /// 対象機識別確認に失敗しました（PT-007 安全停止）。
    DeviceVerifyFailed { detail_message: String },
    /// `PC-004` 鍵ID確認に失敗しました（PT-007 相当の安全停止）。
    Pc004CheckFailed { detail_message: String },
    /// `PT-005a` で LocalServer handoff に失敗しました。
    IrreversibleHandoffFailed { detail_message: String },
    /// `PT-005z` 実コマンドランナーに失敗しました。
    IrreversibleCommandRunnerFailed { detail_message: String },
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
            Self::RuntimeEnvLoadFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::RuntimeEnvLoadFailed detail={}",
                detail_message
            ),
            Self::AuthPolicyLoadFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::AuthPolicyLoadFailed detail={}",
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
            Self::Pc004CheckFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::Pc004CheckFailed detail={}",
                detail_message
            ),
            Self::IrreversibleHandoffFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::IrreversibleHandoffFailed detail={}",
                detail_message
            ),
            Self::IrreversibleCommandRunnerFailed { detail_message } => write!(
                formatter,
                "ProductionToolError::IrreversibleCommandRunnerFailed detail={}",
                detail_message
            ),
        }
    }
}

impl std::error::Error for ProductionToolError {}

/// `ProductionTool` のメインウィザードフローを開始します。
///
/// フロー: PT-001 起動 → SecretCore 確認 → PT-002 追加認証 → PT-003 対象機確認 → `PC-004` 鍵ID確認 → PT-005 dry-run
#[tokio::main]
async fn main() -> Result<(), ProductionToolError> {
    let project_root_path = resolve_project_root_path()
        .map_err(|source_error| ProductionToolError::CurrentDirectoryResolveFailed { source_error })?;
    load_runtime_env_file(&project_root_path)?;
    let auth_policy = load_auth_policy_from_env()?;

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
    // [重要] 認証ポリシーは `.env`（未設定時は安全な既定値）から読み込む。
    // [将来対応] 本番実装では SecretCore 側でのパスワード検証へ委任する。
    let expected_operator_id = auth_policy.expected_operator_id.trim().to_string();
    let max_attempts: u32 = auth_policy.max_attempts;
    let mut auth_state = None;

    println!("  認証ポリシー: 操作者 ID は任意入力（空欄可）");
    if !expected_operator_id.is_empty() {
        println!(
            "  認証ポリシー: operatorId 固定値は監査表示のみ（operatorId={})",
            expected_operator_id
        );
    }
    println!("  認証ポリシー: 最大試行回数={}", max_attempts);

    for attempt in 1..=max_attempts {
        let auth_input = prompt_auth_input(attempt, max_attempts);
        let state = attempt_auth(
            &auth_input,
            &expected_operator_id,
            &auth_policy.expected_password_hash,
            attempt,
            max_attempts,
        );
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
    if auth_state.operator_id.trim().is_empty() {
        println!("  操作者 ID: <empty>");
    } else {
        println!("  操作者 ID: {}", auth_state.operator_id);
    }
    println!("  作業指示番号: {}", auth_state.work_order_id);

    // --- PT-003: 対象機識別確認 ---
    let device_list = build_placeholder_device_list();
    let selected_index = prompt_device_selection(&device_list);
    let device_state = verify_device_selection(&device_list, selected_index);

    println!("\n=== PT-003 対象機確認結果 ===");
    println!("  結果: {:?}", device_state.verify_result);
    println!("  監査: {}", device_state.audit_label);
    if let Some(selected_device) = &device_state.selected_device {
        println!(
            "  選択対象: serial={} mac={} publicId={} connection={}",
            selected_device.serial_number,
            selected_device.mac_address,
            selected_device.public_id,
            selected_device.connection_status_label
        );
    }

    if !device_state.can_proceed_to_execution_confirm {
        eprintln!(
            "ProductionTool safe stop: 対象機識別確認に失敗したため、安全停止します。detail={}",
            device_state.audit_label
        );
        return Err(ProductionToolError::DeviceVerifyFailed {
            detail_message: device_state.audit_label.clone(),
        });
    }

    // --- PC-004: 鍵ID・署名素材ID・ロット照合 ---
    let pc004_expected_value_set = load_pc004_expected_value_set_from_env();
    let pc004_input = prompt_pc004_input(&auth_state.work_order_id, &pc004_expected_value_set);
    let pc004_check_state = verify_pc004_check(&pc004_expected_value_set, &pc004_input);
    let pc004_summary_file_path = if let Ok(evidence_dir_path) = env::var("PRODUCTION_TOOL_EVIDENCE_DIR") {
        if evidence_dir_path.trim().is_empty() {
            None
        } else {
            Some(
                persist_pc004_check_state(Path::new(evidence_dir_path.trim()), &pc004_check_state)
                    .map_err(|detail_message| ProductionToolError::Pc004CheckFailed {
                        detail_message,
                    })?,
            )
        }
    } else {
        None
    };

    println!("\n=== PC-004 鍵ID確認結果 ===");
    println!("  結果: {:?}", pc004_check_state.check_result);
    println!(
        "  実行可否: {}",
        pc004_check_state.can_proceed_to_irreversible_execution
    );
    println!("  監査: {}", pc004_check_state.audit_label);
    println!(
        "  expected: feKeyId={} sbSigningMaterialId={} targetLotId={}",
        pc004_check_state.expected_value_set.flash_encryption_key_id,
        pc004_check_state.expected_value_set.secure_boot_signing_material_id,
        pc004_check_state.expected_value_set.target_lot_id
    );
    println!(
        "  entered: feKeyId={} ({:?}) sbSigningMaterialId={} ({:?}) targetLotId={} ({:?}) workOrderId={}",
        pc004_check_state.input_value_set.entered_flash_encryption_key_id,
        pc004_check_state.input_value_set.flash_encryption_key_id_source,
        pc004_check_state.input_value_set.entered_secure_boot_signing_material_id,
        pc004_check_state.input_value_set.secure_boot_signing_material_id_source,
        pc004_check_state.input_value_set.entered_target_lot_id,
        pc004_check_state.input_value_set.target_lot_id_source,
        pc004_check_state.input_value_set.work_order_id
    );
    println!("  detail: {}", pc004_check_state.detail_message);
    if let Some(summary_file_path) = &pc004_summary_file_path {
        println!("  summary: {}", summary_file_path.display());
    } else {
        println!("  summary: <PRODUCTION_TOOL_EVIDENCE_DIR 未設定のため未保存>");
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

    let irreversible_stage_plan = build_irreversible_stage_plan();
    println!("\n=== ProductionTool 所有の不可逆段階計画 ===");
    println!("  計画名: {}", irreversible_stage_plan.plan_name);
    println!("  根拠: {}", irreversible_stage_plan.source_document_label);
    println!(
        "  実コマンド起動: {}（現フェーズは計画表示と監査のみ）",
        irreversible_stage_plan.command_execution_enabled
    );
    println!("  監査: {}", irreversible_stage_plan.audit_label);
    for stage in &irreversible_stage_plan.stage_list {
        println!("  Stage {}: {}", stage.stage_number, stage.stage_name);
        println!("    主操作: {}", stage.operation_summary_list.join(" / "));
        println!("    必須ゲート: {}", stage.required_gate_list.join(" -> "));
    }

    let irreversible_runner_preview = build_irreversible_command_runner_preview(&irreversible_stage_plan);
    let irreversible_runner_precheck =
        run_irreversible_command_runner_precheck(&irreversible_stage_plan);
    println!("\n=== ProductionTool 不可逆コマンドランナー準備 ===");
    println!(
        "  実コマンド起動サポート: {}（Windows CLI 限定）",
        irreversible_runner_preview.command_execution_supported
    );
    println!(
        "  ランナー armed: {}",
        irreversible_runner_preview.command_execution_armed
    );
    println!(
        "  不可逆実行許可: {}",
        irreversible_runner_preview.irreversible_execution_allowed
    );
    println!("  監査: {}", irreversible_runner_preview.audit_label);
    if irreversible_runner_preview.missing_env_var_list.is_empty() {
        println!("  必須環境変数: 充足");
    } else {
        println!(
            "  必須環境変数不足: {}",
            irreversible_runner_preview.missing_env_var_list.join(", ")
        );
    }
    for command_template in &irreversible_runner_preview.command_template_list {
        println!(
            "  Stage {} Command {} [{} irreversible={}]: {}",
            command_template.stage_number,
            command_template.command_number,
            command_template.command_name,
            command_template.is_irreversible,
            command_template.command_template
        );
        println!(
            "    kind={:?} evidence={}",
            command_template.execution_kind,
            command_template.evidence_file_name
        );
    }

    println!("\n=== PT-005z precheck ===");
    println!("  canProceed: {}", irreversible_runner_precheck.can_proceed);
    if irreversible_runner_precheck.evidence_dir_path.trim().is_empty() {
        println!("  precheck evidence: <not configured>");
    } else {
        println!(
            "  precheck evidence: {}\\runner-precheck-summary.json",
            irreversible_runner_precheck.evidence_dir_path
        );
    }
    println!("  監査: {}", irreversible_runner_precheck.audit_label);
    for check_item in &irreversible_runner_precheck.check_item_list {
        println!(
            "  [{}] {} target={} detail={}",
            if check_item.passed { "OK" } else { "NG" },
            check_item.check_name,
            check_item.target_label,
            check_item.detail_message
        );
    }

    if irreversible_runner_preview.command_execution_supported
        && irreversible_runner_preview.command_execution_armed
    {
        use std::io::{self, Write};
        println!(
            "\n=== PT-005z 最小実ランナー（既定未実行） ===\n  全段実行は `run`、段階指定は `run 1`〜`run 4` を入力してください。\
\n  [厳守] `PRODUCTION_TOOL_ALLOW_IRREVERSIBLE_EXECUTION=1` が無い場合、不可逆コマンド手前で安全停止します。"
        );
        println!(
            "  [重要] 段階4で `DIS_USB_SERIAL_JTAG` も焼く場合だけ `PRODUCTION_TOOL_BURN_DIS_USB_SERIAL_JTAG=1` を設定してください。既定では試験運用どおり保留します。"
        );
        println!(
            "  [重要] シリアル出力停止は最終クローズ判断です。他の不可逆・readback・4点証跡・stability が問題ないことを確認した後、シリアル抑止済みの量産通常FWへ更新する前提で扱ってください。"
        );
        if !pc004_check_state.can_proceed_to_irreversible_execution {
            println!(
                "  [厳守] PC-004 鍵ID確認が未完了のため、`run` を選んだ場合は安全停止します。detail={}",
                pc004_check_state.detail_message
            );
        }
        print!("  runner command [run/run 1..4/skip]: ");
        io::stdout().flush().unwrap_or_default();
        let mut runner_input = String::new();
        io::stdin().read_line(&mut runner_input).unwrap_or_default();
        let requested_stage_number = parse_runner_stage_command(&runner_input);
        if runner_input.trim().eq_ignore_ascii_case("run") || requested_stage_number.is_some() {
            if !pc004_check_state.can_proceed_to_irreversible_execution {
                eprintln!(
                    "ProductionTool safe stop: PC-004 鍵ID確認が未完了のため、PT-005z 実行を禁止します。detail={}",
                    pc004_check_state.detail_message
                );
                return Err(ProductionToolError::Pc004CheckFailed {
                    detail_message: pc004_check_state.audit_label.clone(),
                });
            }
            if let Some(stage_number) = requested_stage_number {
                let execution_result =
                    run_irreversible_command_runner_for_stage(&irreversible_stage_plan, stage_number)
                        .map_err(|detail_message| {
                            ProductionToolError::IrreversibleCommandRunnerFailed { detail_message }
                        })?;
                let _ = print_and_follow_up_irreversible_execution(&execution_result)?;
            } else {
                run_full_irreversible_stage_sequence(&irreversible_stage_plan)?;
            }
        } else {
            println!("  PT-005z は未実行のまま継続します。");
        }
    }

    // --- PT-005a（任意）: LocalServer で不可逆監査受理を記録（eFuse 実書込みはしない） ---
    if let Some(ref selected_device) = device_state.selected_device {
        match production_workflow_handoff::prompt_and_post_irreversible_workflow_acceptance(
            selected_device,
            &auth_state.work_order_id,
            &auth_state.operator_id,
        )
        .await
        {
            Ok(Some(audit_record)) => {
                println!("  PT-005a 監査記録: {:?}", audit_record);
            }
            Ok(None) => {}
            Err(detail_message) => {
                eprintln!(
                    "ProductionTool: PT-005a(LocalServer irreversible handoff) に失敗しました。detail={}",
                    detail_message
                );
                return Err(ProductionToolError::IrreversibleHandoffFailed { detail_message });
            }
        }
    }

    println!("\n=== ProductionTool ウィザードフロー完了 ===");
    println!("  起動・SecretCore 確認・追加認証・対象機確認・dry-run 停止点の確認が完了しました。");
    println!("  監査ログ: {}", audit_write_result.audit_log_file_path.display());
    Ok(())
}

/// `run` 指定時に、各段階を順番に実行して stability 判定まで閉じます。
fn run_full_irreversible_stage_sequence(
    irreversible_stage_plan: &IrreversibleStagePlan,
) -> Result<(), ProductionToolError> {
    println!(
        "  [重要] 全段 `run` は段階ごとに実行し、その都度 stability と次段可否を回収します。"
    );
    for stage_plan_item in &irreversible_stage_plan.stage_list {
        println!(
            "\n=== PT-005z 段階逐次実行 Stage {} [{}] ===",
            stage_plan_item.stage_number, stage_plan_item.stage_name
        );
        let execution_result = run_irreversible_command_runner_for_stage(
            irreversible_stage_plan,
            stage_plan_item.stage_number,
        )
        .map_err(|detail_message| ProductionToolError::IrreversibleCommandRunnerFailed {
            detail_message,
        })?;
        let can_continue_to_next_stage =
            print_and_follow_up_irreversible_execution(&execution_result)?;
        if !can_continue_to_next_stage {
            println!(
                "  [重要] Stage {} で停止しました。理由を確認し、必要なら `run {}` から再開してください。",
                stage_plan_item.stage_number,
                stage_plan_item.stage_number
            );
            break;
        }
    }
    Ok(())
}

/// `PT-005z` 実行結果を表示し、必要なら stability を回収して次段可否を返します。
fn print_and_follow_up_irreversible_execution(
    execution_result: &IrreversibleCommandRunnerExecution,
) -> Result<bool, ProductionToolError> {
    println!("  PT-005z 実行監査: {}", execution_result.audit_label);
    println!("  証跡ディレクトリ: {}", execution_result.evidence_dir_path);
    println!("  selectedStage={:?}", execution_result.selected_stage_number);
    println!(
        "  manualStop={} safetyStop={}",
        execution_result.stopped_for_manual_check, execution_result.stopped_by_safety_gate
    );
    println!(
        "  execution summary: {}\\runner-execution-summary.json",
        execution_result.evidence_dir_path
    );
    for command_record in &execution_result.command_record_list {
        println!(
            "  Stage {} Command {} [{}] -> {:?} exitCode={:?}",
            command_record.stage_number,
            command_record.command_number,
            command_record.command_name,
            command_record.execution_status,
            command_record.exit_code
        );
        println!("    evidence={}", command_record.evidence_file_path);
    }
    println!("  ---- stage summary ----");
    for stage_summary in &execution_result.stage_summary_list {
        println!(
            "  Stage {} [{}] recorded={}/{} before={} after={} failed={} manualStop={} safetyStop={}",
            stage_summary.stage_number,
            stage_summary.stage_name,
            stage_summary.recorded_command_count,
            stage_summary.total_command_count,
            stage_summary.has_before_readback,
            stage_summary.has_after_readback,
            stage_summary.has_failed_command,
            stage_summary.has_manual_check_stop,
            stage_summary.has_safety_gate_stop
        );
    }

    let stage_stability_record_list =
        prompt_stage_stability_record_list(&execution_result.stage_summary_list)?;
    let mut can_continue_to_next_stage = true;
    if !stage_stability_record_list.is_empty() {
        let stability_summary_file_path = persist_stage_stability_record_list(
            std::path::Path::new(&execution_result.evidence_dir_path),
            &stage_stability_record_list,
        )
        .map_err(|detail_message| ProductionToolError::IrreversibleCommandRunnerFailed {
            detail_message,
        })?;
        println!("  stability summary: {}", stability_summary_file_path.display());
        for stage_stability_record in &stage_stability_record_list {
            println!(
                "  Stability Stage {} [{}] proceed={} ota={:?} ap={:?} sta={:?}",
                stage_stability_record.stage_number,
                stage_stability_record.stage_name,
                stage_stability_record.can_proceed_to_next_stage,
                stage_stability_record.ota_twice_status,
                stage_stability_record.ap_ten_minutes_status,
                stage_stability_record.sta_ten_minutes_status
            );
        }
        can_continue_to_next_stage = stage_stability_record_list
            .iter()
            .filter(|stage_stability_record| stage_stability_record.requires_stability_check)
            .all(|stage_stability_record| stage_stability_record.can_proceed_to_next_stage);
    }

    if execution_result.stopped_by_safety_gate {
        return Ok(false);
    }
    if execution_result
        .stage_summary_list
        .iter()
        .any(|stage_summary| stage_summary.has_failed_command)
    {
        return Ok(false);
    }
    if !can_continue_to_next_stage {
        return Ok(false);
    }
    if execution_result.stopped_for_manual_check && stage_stability_record_list.is_empty() {
        return Ok(false);
    }
    Ok(true)
}

/// 段階ごとの安定性確認結果を CLI から回収します。
///
/// # 引数
/// - `stage_summary_list`: 実行済み段階の要約一覧です。
///
/// # 戻り値
/// `ProductionTool` が保存すべき安定性確認記録一覧です。
fn prompt_stage_stability_record_list(
    stage_summary_list: &[irreversible_command_runner::IrreversibleStageExecutionSummary],
) -> Result<Vec<IrreversibleStageStabilityRecord>, ProductionToolError> {
    let mut stage_stability_record_list =
        build_stage_stability_record_template_list(stage_summary_list);
    if stage_stability_record_list
        .iter()
        .all(|stage_stability_record| !stage_stability_record.requires_stability_check)
    {
        return Ok(Vec::new());
    }

    println!("\n=== PT-005z stability 受け皿 ===");
    println!(
        "  [重要] 各段階の `OTA x2 / AP 10分 / STA 10分` の結果と証跡パスを入力します。\
\n  [厳守] NG または未観測がある場合は `canProceedToNextStage=false` としてください。"
    );

    for stage_stability_record in &mut stage_stability_record_list {
        if !stage_stability_record.requires_stability_check {
            continue;
        }

        println!(
            "\n  Stage {} [{}] の stability 入力",
            stage_stability_record.stage_number, stage_stability_record.stage_name
        );
        stage_stability_record.ota_twice_status =
            prompt_stability_status("OTA x2 status [passed/failed/skipped]")?;
        stage_stability_record.ota_evidence_path =
            prompt_text_line("OTA evidence path or note")?;
        stage_stability_record.ap_ten_minutes_status =
            prompt_stability_status("AP 10min status [passed/failed/skipped]")?;
        stage_stability_record.ap_evidence_path =
            prompt_text_line("AP evidence path or note")?;
        stage_stability_record.sta_ten_minutes_status =
            prompt_stability_status("STA 10min status [passed/failed/skipped]")?;
        stage_stability_record.sta_evidence_path =
            prompt_text_line("STA evidence path or note")?;
        stage_stability_record.can_proceed_to_next_stage =
            prompt_yes_no("Can proceed to next stage? [y/n]")?;
        stage_stability_record.note_text = prompt_text_line("Stability note")?;
    }

    Ok(stage_stability_record_list)
}

/// `PT-005z` の段階指定実行コマンドを解析します。
///
/// # 引数
/// - `runner_input`: 操作者が入力したコマンド文字列です。
///
/// # 戻り値
/// `run 1`〜`run 4` のような段階指定がある場合は段階番号を返します。
fn parse_runner_stage_command(runner_input: &str) -> Option<u32> {
    let trimmed_input = runner_input.trim();
    let mut token_iter = trimmed_input.split_whitespace();
    let command_token = token_iter.next()?;
    if !command_token.eq_ignore_ascii_case("run") {
        return None;
    }
    let stage_token = token_iter.next()?;
    if token_iter.next().is_some() {
        return None;
    }
    stage_token.parse::<u32>().ok()
}

/// `passed/failed/skipped` の安定性結果入力を受け取ります。
fn prompt_stability_status(prompt_label: &str) -> Result<StabilityObservationStatus, ProductionToolError> {
    loop {
        let input_text = prompt_text_line(prompt_label)?;
        match input_text.trim().to_ascii_lowercase().as_str() {
            "passed" | "pass" | "ok" => return Ok(StabilityObservationStatus::Passed),
            "failed" | "fail" | "ng" => return Ok(StabilityObservationStatus::Failed),
            "skipped" | "skip" | "" => return Ok(StabilityObservationStatus::Skipped),
            _ => {
                eprintln!("  入力値が不正です。passed / failed / skipped を入力してください。");
            }
        }
    }
}

/// Yes/No 入力を受け取ります。
fn prompt_yes_no(prompt_label: &str) -> Result<bool, ProductionToolError> {
    loop {
        let input_text = prompt_text_line(prompt_label)?;
        match input_text.trim().to_ascii_lowercase().as_str() {
            "y" | "yes" => return Ok(true),
            "n" | "no" | "" => return Ok(false),
            _ => {
                eprintln!("  入力値が不正です。y または n を入力してください。");
            }
        }
    }
}

/// 1行テキスト入力を受け取ります。
fn prompt_text_line(prompt_label: &str) -> Result<String, ProductionToolError> {
    use std::io::{self, Write};
    print!("  {}: ", prompt_label);
    io::stdout()
        .flush()
        .map_err(|error| ProductionToolError::IrreversibleCommandRunnerFailed {
            detail_message: format!(
                "prompt_text_line flush failed. promptLabel={} detail={}",
                prompt_label, error
            ),
        })?;
    let mut input_text = String::new();
    io::stdin().read_line(&mut input_text).map_err(|error| {
        ProductionToolError::IrreversibleCommandRunnerFailed {
            detail_message: format!(
                "prompt_text_line read failed. promptLabel={} detail={}",
                prompt_label, error
            ),
        }
    })?;
    Ok(input_text.trim().to_string())
}

/// 現在の作業ディレクトリから `ProductionTool` ルートを解決します。
fn resolve_project_root_path() -> Result<PathBuf, io::Error> {
    env::current_dir()
}

/// 実行ディレクトリの `.env` を読み込み、環境変数へ反映します。
fn load_runtime_env_file(project_root_path: &Path) -> Result<(), ProductionToolError> {
    let env_file_path = project_root_path.join(".env");
    if !env_file_path.exists() {
        return Ok(());
    }
    dotenvy::from_path(&env_file_path).map_err(|error| ProductionToolError::RuntimeEnvLoadFailed {
        detail_message: format!(
            "load_runtime_env_file failed. envFilePath={} detail={}",
            env_file_path.display(),
            error
        ),
    })?;
    Ok(())
}

/// 追加認証ポリシーを環境変数から読み込みます。
fn load_auth_policy_from_env() -> Result<AuthPolicy, ProductionToolError> {
    let expected_operator_id = env::var("PRODUCTION_TOOL_AUTH_OPERATOR_ID").unwrap_or_default();
    let expected_password_plain =
        env::var("PRODUCTION_TOOL_AUTH_PASSWORD").unwrap_or_else(|_| "maker2026".to_string());
    if expected_password_plain.trim().is_empty() {
        return Err(ProductionToolError::AuthPolicyLoadFailed {
            detail_message:
                "load_auth_policy_from_env failed. PRODUCTION_TOOL_AUTH_PASSWORD is empty."
                    .to_string(),
        });
    }
    let expected_password_hash = auth_screen_state::sha256_hex_public(expected_password_plain.trim());
    let max_attempts = match env::var("PRODUCTION_TOOL_AUTH_MAX_ATTEMPTS") {
        Ok(raw_value) => raw_value.parse::<u32>().map_err(|error| {
            ProductionToolError::AuthPolicyLoadFailed {
                detail_message: format!(
                    "load_auth_policy_from_env failed. PRODUCTION_TOOL_AUTH_MAX_ATTEMPTS={} parse error={}",
                    raw_value, error
                ),
            }
        })?,
        Err(_) => 3,
    };
    if max_attempts == 0 {
        return Err(ProductionToolError::AuthPolicyLoadFailed {
            detail_message:
                "load_auth_policy_from_env failed. PRODUCTION_TOOL_AUTH_MAX_ATTEMPTS must be >= 1."
                    .to_string(),
        });
    }

    Ok(AuthPolicy {
        expected_operator_id,
        expected_password_hash,
        max_attempts,
    })
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
