//! `ProductionTool` 不可逆段階実行計画モジュール。
//!
//! [重要] 本ファイルは `ProductionTool` が eFuse / Secure Boot / Flash Encryption の
//! 製造責任を持つための、段階計画と停止条件のコード上の親定義です。
//! 主な仕様:
//! - `本番セキュア化出荷準備試験計画書.md` §6.4 と `案A（NVS暗号化なし）` の現方針を `ProductionTool` 側へ写像する
//! - 各段階で `precheck -> stage_execute -> readback -> reboot -> evidence -> stability` を必須化する
//! - 現時点では **計画表示と監査ラベル生成のみ** を行い、`espefuse` 等の実コマンドは起動しない
//! 制限事項:
//! - 実行ランナーは未実装。後続で、ポート、鍵素材、作業ディレクトリ、4点証跡保存先を
//!   全て検証してから `espefuse` / `espsecure` / `esptool` を呼び出す。
//! - 秘密鍵パスや raw key は本構造体に保持しない。
//! 変更履歴:
//! - 2026-04-29: `ProductionTool` が不可逆工程の責任主体であることをコード構造に反映するため追加。
//! - 2026-04-29: 段階2を `NVS(HMAC)` から `NVS暗号化なし確認` へ修正。理由: 現行案Aでは NVS Encryption / HMAC eFuse 投入を本番不可逆として実施しないため。

use serde::Serialize;

/// 不可逆段階の分類です。
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum IrreversibleStageKind {
    /// Flash Encryption 系の段階です。
    FlashEncryption,
    /// NVS 暗号化なし（案A）を確認する非焼込みゲートです。
    NvsPlaintextValidation,
    /// Secure Boot v2 系の段階です。
    SecureBootV2,
    /// JTAG / Download 系の最終封鎖段階です。
    FinalLockdown,
}

/// `ProductionTool` が管理する不可逆段階の1単位です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleStagePlanItem {
    /// 段階番号です（1始まり）。
    pub stage_number: u32,
    /// 段階分類です。
    pub stage_kind: IrreversibleStageKind,
    /// 利用者向け段階名です。
    pub stage_name: String,
    /// 段階内で実施する主な操作です。
    pub operation_summary_list: Vec<String>,
    /// 次段階へ進む前に必ず満たす停止・検証条件です。
    pub required_gate_list: Vec<String>,
}

/// `ProductionTool` 所有の不可逆段階計画です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleStagePlan {
    /// 計画名です。
    pub plan_name: String,
    /// 計画の根拠文書です。
    pub source_document_label: String,
    /// 段階一覧です。
    pub stage_list: Vec<IrreversibleStagePlanItem>,
    /// 現時点で実コマンドを起動するかどうかです。
    pub command_execution_enabled: bool,
    /// 監査ログ用ラベルです。
    pub audit_label: String,
}

/// `ProductionTool` が責任を持つ不可逆段階計画を構築します。
///
/// # 戻り値
/// `本番セキュア化出荷準備試験計画書.md` §6.4、`007_本番1台目_実行順チェックリスト.md`、
/// および案A（NVS暗号化なし）に沿った段階計画です。
/// 現時点では `command_execution_enabled=false` とし、実コマンドは起動しません。
pub fn build_irreversible_stage_plan() -> IrreversibleStagePlan {
    let stage_list = vec![
        IrreversibleStagePlanItem {
            stage_number: 1,
            stage_kind: IrreversibleStageKind::FlashEncryption,
            stage_name: "段階1(FE): Flash Encryption 系".to_string(),
            operation_summary_list: vec![
                "FE 鍵投入（BLOCK_KEY0 / XTS_AES_128_KEY）".to_string(),
                "SPI_BOOT_CRYPT_CNT 有効化".to_string(),
                "FE 関連 eFuse（DIS_DOWNLOAD_MANUAL_ENCRYPT 等）".to_string(),
                "暗号化済みバイナリ書込み".to_string(),
            ],
            required_gate_list: build_common_gate_list("Flash Encryption 有効・暗号化起動確認"),
        },
        IrreversibleStagePlanItem {
            stage_number: 2,
            stage_kind: IrreversibleStageKind::NvsPlaintextValidation,
            stage_name: "段階2(NVS): 案A / NVS暗号化なし確認".to_string(),
            operation_summary_list: vec![
                "NVS Encryption / HMAC eFuse を本番不可逆として実施しない方針を確認".to_string(),
                "secureNvsInit patternA（NVS Encryption not enabled）ログ確認".to_string(),
                "NVS 読書き・設定保持確認".to_string(),
                "AP HTTP 全ロール認証確認".to_string(),
            ],
            required_gate_list: build_common_gate_list("NVS暗号化なし方針と AP 認証健全性確認"),
        },
        IrreversibleStagePlanItem {
            stage_number: 3,
            stage_kind: IrreversibleStageKind::SecureBootV2,
            stage_name: "段階3(SB): Secure Boot v2 系".to_string(),
            operation_summary_list: vec![
                "Secure Boot v2 公開鍵ダイジェスト投入（BLOCK_KEY1）".to_string(),
                "SECURE_BOOT_EN 有効化".to_string(),
                "未使用 slot revoke".to_string(),
                "RD_DIS write-protect（全 burn-key 完了後）".to_string(),
                "署名済みバイナリ書込み".to_string(),
            ],
            required_gate_list: build_common_gate_list("Secure Boot v2 検証成功ログ確認"),
        },
        IrreversibleStagePlanItem {
            stage_number: 4,
            stage_kind: IrreversibleStageKind::FinalLockdown,
            stage_name: "段階4(封鎖): JTAG / Download 最終封鎖".to_string(),
            operation_summary_list: vec![
                "JTAG 無効化（DIS_PAD_JTAG / DIS_USB_JTAG）".to_string(),
                "DIS_USB_SERIAL_JTAG は焼かない方針を再確認".to_string(),
                "ENABLE_SECURITY_DOWNLOAD を最後に実行".to_string(),
            ],
            required_gate_list: build_common_gate_list("最終起動・OTA・AP/STA 安定性確認"),
        },
    ];

    IrreversibleStagePlan {
        plan_name: "ProductionTool owned irreversible secure stage plan".to_string(),
        source_document_label:
            "本番セキュア化出荷準備試験計画書.md §6.4 / 007_本番1台目_実行順チェックリスト.md / 案A(NVS暗号化なし)"
                .to_string(),
        stage_list,
        command_execution_enabled: false,
        audit_label: "irreversible_stage_plan_loaded commandExecution=false owner=ProductionTool".to_string(),
    }
}

/// 各段階共通のゲート一覧を構築します。
///
/// # 引数
/// - `stage_specific_gate`: 段階固有の確認内容です。
///
/// # 戻り値
/// 段階完了前に `ProductionTool` が必ず要求するゲート一覧です。
fn build_common_gate_list(stage_specific_gate: &str) -> Vec<String> {
    vec![
        "espefuse summary before/after を保存".to_string(),
        "対象機再起動とシリアルログ保存".to_string(),
        stage_specific_gate.to_string(),
        "4点証跡（ProductionTool / serial / espefuse / 試験記録書）を確認".to_string(),
        "安定性確認セット（OTA x2 / AP 10分 / STA 10分）を完了".to_string(),
        "NG時は次段階へ進まず isolated または failed として停止".to_string(),
    ]
}
