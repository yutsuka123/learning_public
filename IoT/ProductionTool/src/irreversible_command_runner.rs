//! `ProductionTool` 不可逆コマンドランナー準備モジュール。
//!
//! [重要] 本ファイルは `ProductionTool` が eFuse / Secure Boot / Flash Encryption の
//! 実コマンド実行責任を持つための準備層です。
//! 主な仕様:
//! - `irreversible_stage_plan.rs` の段階順に対応するコマンドテンプレートを構築する
//! - 実行に必要な環境変数の有無を検査する
//! - 現フェーズでは **実コマンドを起動しない**。あくまでプレビューと不足条件の明示に限定する
//! 制限事項:
//! - `espefuse` / `espsecure` / `esptool` の呼び出しは未実装
//! - 鍵ファイル実パスや秘密値は標準出力へ表示せず、環境変数名のみ表示する
//! - 実行を有効化する後続実装では、各コマンド直後の readback と 4点証跡保存を必須にする
//! 変更履歴:
//! - 2026-04-29: `ProductionTool` が不可逆工程の実行責任を持つ王道設計へ収束するため、プレビュー層として追加。
//! - 2026-04-29: NVS 段階から HMAC 鍵投入テンプレートを削除。理由: 現行案Aでは NVS Encryption / HMAC eFuse 投入を本番不可逆として実施しないため。
//! - 2026-04-29: `espefuse summary` の出力テンプレートを段階別ファイル名へ変更。理由: 実ランナー実装時に before/after 証跡の上書きを防ぐため。

use serde::Serialize;
use std::env;

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
    /// 不可逆操作を含むかどうかです。
    pub is_irreversible: bool,
    /// このコマンドに必要な環境変数名一覧です。
    pub required_env_var_list: Vec<String>,
}

/// 不可逆コマンドランナーのプレビュー結果です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleCommandRunnerPreview {
    /// ランナーが実コマンド起動を許可されているかどうかです。
    pub command_execution_armed: bool,
    /// この実装が実際にコマンドを起動するかどうかです（現フェーズでは常に false）。
    pub command_execution_supported: bool,
    /// 不足している環境変数名一覧です。
    pub missing_env_var_list: Vec<String>,
    /// コマンドテンプレート一覧です。
    pub command_template_list: Vec<IrreversibleCommandTemplate>,
    /// 監査ログ用ラベルです。
    pub audit_label: String,
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
        "PRODUCTION_TOOL_FLASH_ARGS_STAGE1".to_string(),
        "PRODUCTION_TOOL_FLASH_ARGS_STAGE3".to_string(),
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

    IrreversibleCommandRunnerPreview {
        command_execution_armed,
        command_execution_supported: false,
        missing_env_var_list: missing_env_var_list.clone(),
        command_template_list,
        audit_label: format!(
            "irreversible_command_runner_preview armed={} supported=false missingEnvCount={}",
            command_execution_armed,
            missing_env_var_list.len()
        ),
    }
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
                    is_irreversible: true,
                    required_env_var_list: vec![
                        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
                        "PRODUCTION_TOOL_FE_KEY_PATH".to_string(),
                    ],
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 3,
                    command_name: "FE有効化と関連eFuse".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse SPI_BOOT_CRYPT_CNT 0x7 && python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse DIS_DOWNLOAD_MANUAL_ENCRYPT DIS_DOWNLOAD_ICACHE DIS_DOWNLOAD_DCACHE DIS_DIRECT_BOOT"
                            .to_string(),
                    is_irreversible: true,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 4,
                    command_name: "暗号化済みバイナリ書込み".to_string(),
                    command_template:
                        "python -m esptool --chip esp32s3 --port %PRODUCTION_TOOL_SERIAL_PORT% write_flash %PRODUCTION_TOOL_FLASH_ARGS_STAGE1%"
                            .to_string(),
                    is_irreversible: false,
                    required_env_var_list: vec![
                        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
                        "PRODUCTION_TOOL_FLASH_ARGS_STAGE1".to_string(),
                    ],
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
                    is_irreversible: false,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
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
                    is_irreversible: true,
                    required_env_var_list: vec![
                        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
                        "PRODUCTION_TOOL_SB_SIGNING_KEY_PATH".to_string(),
                    ],
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 3,
                    command_name: "Secure Boot有効化とrevoke".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse SECURE_BOOT_EN SECURE_BOOT_KEY_REVOKE1 SECURE_BOOT_KEY_REVOKE2"
                            .to_string(),
                    is_irreversible: true,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 4,
                    command_name: "RD_DIS write-protect".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% write_protect_efuse RD_DIS"
                            .to_string(),
                    is_irreversible: true,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 5,
                    command_name: "署名済みバイナリ書込み".to_string(),
                    command_template:
                        "python -m esptool --chip esp32s3 --port %PRODUCTION_TOOL_SERIAL_PORT% write_flash %PRODUCTION_TOOL_FLASH_ARGS_STAGE3%"
                            .to_string(),
                    is_irreversible: false,
                    required_env_var_list: vec![
                        "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
                        "PRODUCTION_TOOL_FLASH_ARGS_STAGE3".to_string(),
                    ],
                });
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    6,
                    "SB後 eFuse summary",
                );
            }
            IrreversibleStageKind::FinalLockdown => {
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    1,
                    "封鎖前 eFuse summary",
                );
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 2,
                    command_name: "JTAG無効化".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse DIS_PAD_JTAG DIS_USB_JTAG"
                            .to_string(),
                    is_irreversible: true,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                });
                template_list.push(IrreversibleCommandTemplate {
                    stage_number: stage.stage_number,
                    command_number: 3,
                    command_name: "Security Download有効化".to_string(),
                    command_template:
                        "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% burn_efuse ENABLE_SECURITY_DOWNLOAD"
                            .to_string(),
                    is_irreversible: true,
                    required_env_var_list: vec!["PRODUCTION_TOOL_SERIAL_PORT".to_string()],
                });
                push_stage_summary_template(
                    &mut template_list,
                    stage.stage_number,
                    4,
                    "封鎖後 eFuse summary",
                );
            }
        }
    }
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
        command_template: format!(
            "python -m espefuse --port %PRODUCTION_TOOL_SERIAL_PORT% summary > %PRODUCTION_TOOL_EVIDENCE_DIR%\\{evidence_file_name}"
        ),
        is_irreversible: false,
        required_env_var_list: vec![
            "PRODUCTION_TOOL_SERIAL_PORT".to_string(),
            "PRODUCTION_TOOL_EVIDENCE_DIR".to_string(),
        ],
    });
}
