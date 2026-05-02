//! `ProductionTool` の `PC-004` 鍵ID確認モジュール。
//!
//! [重要] 本ファイルは `PC-004` の「鍵ID・署名素材ID・ロット・作業指示番号」の照合を
//! `ProductionTool` 側で一貫して扱うための状態と入力処理を定義します。
//! 主な仕様:
//! - `PRODUCTION_TOOL_FE_KEY_ID`、`PRODUCTION_TOOL_SB_SIGNING_MATERIAL_ID`、
//!   `PRODUCTION_TOOL_TARGET_LOT_ID` を期待値として読む
//! - CLI で操作者に照合入力を求め、期待値がある場合は Enter でそのまま採用できる
//! - 秘密値ではない ID 情報だけを比較し、結果を `pc004-check-summary.json` に保存する
//! - 未設定または不一致時は不可逆実行へ進ませない
//! 制限事項:
//! - 現フェーズでは外部台帳やバーコードリーダーとは連携しない
//! - 期待値は環境変数からのみ取得する
//! - 秘密鍵本文、raw key、実ファイル内容は扱わない
//! 変更履歴:
//! - 2026-05-02: 新規追加。理由: `PC-004` を `ProductionTool` の責任で照合し、不可逆直前の人依存を減らすため。

use serde::Serialize;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

/// `PC-004` の期待値です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Pc004ExpectedValueSet {
    /// Flash Encryption 鍵IDです。
    pub flash_encryption_key_id: String,
    /// Secure Boot 署名素材IDです。
    pub secure_boot_signing_material_id: String,
    /// 対象ロットIDです。
    pub target_lot_id: String,
}

/// `PC-004` の操作者入力です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Pc004Input {
    /// 入力された Flash Encryption 鍵IDです。
    pub entered_flash_encryption_key_id: String,
    /// Flash Encryption 鍵IDの入力元です。
    pub flash_encryption_key_id_source: Pc004InputSource,
    /// 入力された Secure Boot 署名素材IDです。
    pub entered_secure_boot_signing_material_id: String,
    /// Secure Boot 署名素材IDの入力元です。
    pub secure_boot_signing_material_id_source: Pc004InputSource,
    /// 入力された対象ロットIDです。
    pub entered_target_lot_id: String,
    /// 対象ロットIDの入力元です。
    pub target_lot_id_source: Pc004InputSource,
    /// `PT-002` で確定した作業指示番号です。
    pub work_order_id: String,
}

/// `PC-004` 入力値の採用元です。
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum Pc004InputSource {
    /// 期待値をそのまま採用しました。
    ExpectedValueAccepted,
    /// 作業者が手動で上書きしました。
    OperatorOverride,
}

/// `PC-004` の照合結果です。
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum Pc004CheckResult {
    /// 期待値不足で照合不能です。
    MissingExpectedValue,
    /// 照合に成功しました。
    Confirmed,
    /// 照合不一致です。
    Mismatch,
}

/// `PC-004` の照合状態です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Pc004CheckState {
    /// 期待値です。
    pub expected_value_set: Pc004ExpectedValueSet,
    /// 操作者入力です。
    pub input_value_set: Pc004Input,
    /// 照合結果です。
    pub check_result: Pc004CheckResult,
    /// 不可逆実行へ進めるかどうかです。
    pub can_proceed_to_irreversible_execution: bool,
    /// 補足メッセージです。
    pub detail_message: String,
    /// 監査ラベルです。
    pub audit_label: String,
}

/// `PC-004` の期待値を環境変数から読み込みます。
pub fn load_pc004_expected_value_set_from_env() -> Pc004ExpectedValueSet {
    Pc004ExpectedValueSet {
        flash_encryption_key_id: env::var("PRODUCTION_TOOL_FE_KEY_ID").unwrap_or_default(),
        secure_boot_signing_material_id: env::var("PRODUCTION_TOOL_SB_SIGNING_MATERIAL_ID")
            .unwrap_or_default(),
        target_lot_id: env::var("PRODUCTION_TOOL_TARGET_LOT_ID").unwrap_or_default(),
    }
}

/// `PC-004` の CLI 入力を受け付けます。
pub fn prompt_pc004_input(
    work_order_id: &str,
    expected_value_set: &Pc004ExpectedValueSet,
) -> Pc004Input {
    use std::io::{self, Write};

    println!("--- PC-004 鍵ID確認 ---");
    println!(
        "[重要] 秘密値ではなく、鍵ID・署名素材ID・ロット・作業指示番号の照合のみを行います。"
    );
    println!("作業指示番号は PT-002 の値をそのまま使用します: {}", work_order_id);
    println!(
        "[推奨] 期待値どおりでよい項目は Enter のみで採用できます。"
    );

    print!(
        "Flash Encryption 鍵ID [{}]: ",
        build_default_display_text(&expected_value_set.flash_encryption_key_id)
    );
    io::stdout().flush().unwrap_or_default();
    let mut flash_encryption_key_id = String::new();
    io::stdin()
        .read_line(&mut flash_encryption_key_id)
        .unwrap_or_default();

    print!(
        "Secure Boot 署名素材ID [{}]: ",
        build_default_display_text(&expected_value_set.secure_boot_signing_material_id)
    );
    io::stdout().flush().unwrap_or_default();
    let mut secure_boot_signing_material_id = String::new();
    io::stdin()
        .read_line(&mut secure_boot_signing_material_id)
        .unwrap_or_default();

    print!(
        "対象ロットID [{}]: ",
        build_default_display_text(&expected_value_set.target_lot_id)
    );
    io::stdout().flush().unwrap_or_default();
    let mut target_lot_id = String::new();
    io::stdin()
        .read_line(&mut target_lot_id)
        .unwrap_or_default();

    let (resolved_flash_encryption_key_id, flash_encryption_key_id_source) =
        resolve_input_with_source(
            &flash_encryption_key_id,
            &expected_value_set.flash_encryption_key_id,
        );
    let (
        resolved_secure_boot_signing_material_id,
        secure_boot_signing_material_id_source,
    ) = resolve_input_with_source(
        &secure_boot_signing_material_id,
        &expected_value_set.secure_boot_signing_material_id,
    );
    let (resolved_target_lot_id, target_lot_id_source) =
        resolve_input_with_source(&target_lot_id, &expected_value_set.target_lot_id);

    Pc004Input {
        entered_flash_encryption_key_id: resolved_flash_encryption_key_id,
        flash_encryption_key_id_source,
        entered_secure_boot_signing_material_id: resolved_secure_boot_signing_material_id,
        secure_boot_signing_material_id_source,
        entered_target_lot_id: resolved_target_lot_id,
        target_lot_id_source,
        work_order_id: work_order_id.to_string(),
    }
}

/// `PC-004` の照合を実施します。
pub fn verify_pc004_check(
    expected_value_set: &Pc004ExpectedValueSet,
    input_value_set: &Pc004Input,
) -> Pc004CheckState {
    let missing_expected_name_list = build_missing_expected_name_list(expected_value_set);
    if !missing_expected_name_list.is_empty() {
        let detail_message = format!(
            "pc004_check missing expected values: {}",
            missing_expected_name_list.join(", ")
        );
        return Pc004CheckState {
            expected_value_set: expected_value_set.clone(),
            input_value_set: input_value_set.clone(),
            check_result: Pc004CheckResult::MissingExpectedValue,
            can_proceed_to_irreversible_execution: false,
            detail_message: detail_message.clone(),
            audit_label: format!("pc004_failed:missingExpectedValue {}", detail_message),
        };
    }

    let mismatch_field_name_list = build_mismatch_field_name_list(expected_value_set, input_value_set);
    if !mismatch_field_name_list.is_empty() {
        let detail_message = format!(
            "pc004_check mismatch fields: {}",
            mismatch_field_name_list.join(", ")
        );
        return Pc004CheckState {
            expected_value_set: expected_value_set.clone(),
            input_value_set: input_value_set.clone(),
            check_result: Pc004CheckResult::Mismatch,
            can_proceed_to_irreversible_execution: false,
            detail_message: detail_message.clone(),
            audit_label: format!(
                "pc004_failed:mismatch workOrderId={} {}",
                input_value_set.work_order_id, detail_message
            ),
        };
    }

    let detail_message = format!(
        "pc004_check confirmed. workOrderId={} feKeyId={} sbSigningMaterialId={} targetLotId={}",
        input_value_set.work_order_id,
        input_value_set.entered_flash_encryption_key_id,
        input_value_set.entered_secure_boot_signing_material_id,
        input_value_set.entered_target_lot_id
    );
    Pc004CheckState {
        expected_value_set: expected_value_set.clone(),
        input_value_set: input_value_set.clone(),
        check_result: Pc004CheckResult::Confirmed,
        can_proceed_to_irreversible_execution: true,
        detail_message: detail_message.clone(),
        audit_label: format!("pc004_success {}", detail_message),
    }
}

/// `PC-004` の照合結果を JSON として保存します。
pub fn persist_pc004_check_state(
    evidence_dir_path: &Path,
    check_state: &Pc004CheckState,
) -> Result<PathBuf, String> {
    fs::create_dir_all(evidence_dir_path).map_err(|source_error| {
        format!(
            "persist_pc004_check_state failed. evidenceDirPath={} detail={}",
            evidence_dir_path.display(),
            source_error
        )
    })?;
    let output_file_path = evidence_dir_path.join("pc004-check-summary.json");
    fs::write(
        &output_file_path,
        serde_json::to_string_pretty(check_state)
            .unwrap_or_else(|_| "{\"error\":\"pc004CheckSerializationFailed\"}".to_string()),
    )
    .map_err(|source_error| {
        format!(
            "persist_pc004_check_state failed. outputFilePath={} detail={}",
            output_file_path.display(),
            source_error
        )
    })?;
    Ok(output_file_path)
}

/// 未設定の期待値名一覧を構築します。
fn build_missing_expected_name_list(
    expected_value_set: &Pc004ExpectedValueSet,
) -> Vec<String> {
    let mut missing_name_list = Vec::new();
    if expected_value_set.flash_encryption_key_id.trim().is_empty() {
        missing_name_list.push("PRODUCTION_TOOL_FE_KEY_ID".to_string());
    }
    if expected_value_set
        .secure_boot_signing_material_id
        .trim()
        .is_empty()
    {
        missing_name_list.push("PRODUCTION_TOOL_SB_SIGNING_MATERIAL_ID".to_string());
    }
    if expected_value_set.target_lot_id.trim().is_empty() {
        missing_name_list.push("PRODUCTION_TOOL_TARGET_LOT_ID".to_string());
    }
    missing_name_list
}

/// 不一致項目名一覧を構築します。
fn build_mismatch_field_name_list(
    expected_value_set: &Pc004ExpectedValueSet,
    input_value_set: &Pc004Input,
) -> Vec<String> {
    let mut mismatch_field_name_list = Vec::new();
    if expected_value_set.flash_encryption_key_id.trim()
        != input_value_set.entered_flash_encryption_key_id.trim()
    {
        mismatch_field_name_list.push("flashEncryptionKeyId".to_string());
    }
    if expected_value_set.secure_boot_signing_material_id.trim()
        != input_value_set.entered_secure_boot_signing_material_id.trim()
    {
        mismatch_field_name_list.push("secureBootSigningMaterialId".to_string());
    }
    if expected_value_set.target_lot_id.trim() != input_value_set.entered_target_lot_id.trim() {
        mismatch_field_name_list.push("targetLotId".to_string());
    }
    mismatch_field_name_list
}

/// CLI 入力値と採用元を解決します。
fn resolve_input_with_source(
    input_text: &str,
    default_value: &str,
) -> (String, Pc004InputSource) {
    let normalized_input_text = input_text.trim();
    if normalized_input_text.is_empty() {
        (
            default_value.trim().to_string(),
            Pc004InputSource::ExpectedValueAccepted,
        )
    } else {
        (
            normalized_input_text.to_string(),
            Pc004InputSource::OperatorOverride,
        )
    }
}

/// CLI 表示用の既定値テキストを構築します。
fn build_default_display_text(default_value: &str) -> String {
    if default_value.trim().is_empty() {
        "<empty>".to_string()
    } else {
        default_value.trim().to_string()
    }
}

/// `PC-004` 照合ロジックの単体試験です。
#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    /// 正常系の期待値を構築します。
    fn build_expected_value_set() -> Pc004ExpectedValueSet {
        Pc004ExpectedValueSet {
            flash_encryption_key_id: "fe-key-001".to_string(),
            secure_boot_signing_material_id: "sb-sign-001".to_string(),
            target_lot_id: "lot-20260502-a".to_string(),
        }
    }

    /// 正常系の入力値を構築します。
    fn build_input_value_set() -> Pc004Input {
        Pc004Input {
            entered_flash_encryption_key_id: "fe-key-001".to_string(),
            flash_encryption_key_id_source: Pc004InputSource::ExpectedValueAccepted,
            entered_secure_boot_signing_material_id: "sb-sign-001".to_string(),
            secure_boot_signing_material_id_source: Pc004InputSource::ExpectedValueAccepted,
            entered_target_lot_id: "lot-20260502-a".to_string(),
            target_lot_id_source: Pc004InputSource::ExpectedValueAccepted,
            work_order_id: "WO-20260502-01".to_string(),
        }
    }

    /// 一致時は照合成功になることを確認します。
    #[test]
    fn verify_pc004_check_returns_confirmed_when_all_fields_match() {
        let expected_value_set = build_expected_value_set();
        let input_value_set = build_input_value_set();

        let check_state = verify_pc004_check(&expected_value_set, &input_value_set);

        assert_eq!(check_state.check_result, Pc004CheckResult::Confirmed);
        assert!(check_state.can_proceed_to_irreversible_execution);
        assert!(check_state.audit_label.contains("pc004_success"));
    }

    /// 期待値不足時は照合不能になることを確認します。
    #[test]
    fn verify_pc004_check_returns_missing_expected_value_when_expected_env_is_incomplete() {
        let expected_value_set = Pc004ExpectedValueSet {
            flash_encryption_key_id: "".to_string(),
            secure_boot_signing_material_id: "sb-sign-001".to_string(),
            target_lot_id: "".to_string(),
        };
        let input_value_set = build_input_value_set();

        let check_state = verify_pc004_check(&expected_value_set, &input_value_set);

        assert_eq!(check_state.check_result, Pc004CheckResult::MissingExpectedValue);
        assert!(!check_state.can_proceed_to_irreversible_execution);
        assert!(check_state
            .detail_message
            .contains("PRODUCTION_TOOL_FE_KEY_ID"));
        assert!(check_state
            .detail_message
            .contains("PRODUCTION_TOOL_TARGET_LOT_ID"));
    }

    /// 不一致時は対象項目名が detail に含まれることを確認します。
    #[test]
    fn verify_pc004_check_returns_mismatch_when_any_field_differs() {
        let expected_value_set = build_expected_value_set();
        let mut input_value_set = build_input_value_set();
        input_value_set.entered_target_lot_id = "lot-20260502-b".to_string();

        let check_state = verify_pc004_check(&expected_value_set, &input_value_set);

        assert_eq!(check_state.check_result, Pc004CheckResult::Mismatch);
        assert!(!check_state.can_proceed_to_irreversible_execution);
        assert!(check_state.detail_message.contains("targetLotId"));
    }

    /// JSON 証跡が保存できることを確認します。
    #[test]
    fn persist_pc004_check_state_writes_summary_json_file() {
        let expected_value_set = build_expected_value_set();
        let input_value_set = build_input_value_set();
        let check_state = verify_pc004_check(&expected_value_set, &input_value_set);
        let unique_suffix = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let evidence_dir_path = std::env::temp_dir().join(format!(
            "production_tool_pc004_test_{}",
            unique_suffix
        ));

        let output_file_path =
            persist_pc004_check_state(&evidence_dir_path, &check_state).unwrap_or_else(|error| {
                panic!("persist_pc004_check_state failed. detail={}", error)
            });
        let written_text = std::fs::read_to_string(&output_file_path).unwrap_or_else(|error| {
            panic!(
                "read_to_string failed. outputFilePath={} detail={}",
                output_file_path.display(),
                error
            )
        });

        assert!(output_file_path.exists());
        assert!(written_text.contains("pc004_success"));
        assert!(written_text.contains("WO-20260502-01"));
        assert!(written_text.contains("expectedValueAccepted"));

        let _ = std::fs::remove_file(&output_file_path);
        let _ = std::fs::remove_dir_all(&evidence_dir_path);
    }

    /// 入力が空なら既定値を採用することを確認します。
    #[test]
    fn resolve_input_or_default_returns_default_when_input_is_empty() {
        let (resolved_value, input_source) = resolve_input_with_source("   ", "default-id-001");

        assert_eq!(resolved_value, "default-id-001");
        assert_eq!(input_source, Pc004InputSource::ExpectedValueAccepted);
    }

    /// 入力があるときは入力値を優先することを確認します。
    #[test]
    fn resolve_input_or_default_returns_input_when_input_is_present() {
        let (resolved_value, input_source) =
            resolve_input_with_source("input-id-002", "default-id-001");

        assert_eq!(resolved_value, "input-id-002");
        assert_eq!(input_source, Pc004InputSource::OperatorOverride);
    }
}
