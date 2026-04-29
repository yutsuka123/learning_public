//! LocalServer へ `runProductionSecureFlow`（不可逆受理の監査記録）を依頼し、workflow 終端まで追跡するオプション手順（PT-005 後続）。
//!
//! [重要] 本モジュールは **`SecretCore` が本来行う「受理と状態遷移」**を、ProductionTool から HTTP で起動するための橋渡しです。
//! [厳守] **eFuse / espefuse / 暗号化イメージ書込みの本体は実行しません。** 実機の不可逆は `本番セキュア化出荷準備試験計画書.md` §6.4 の手順で別途実施します。
//! [参照] `SecretCore` の `generic_workflow.rs` コメント:
//! 「この管理器は eFuse 書込み本体を実行せず、不可逆段階の受理と監査状態遷移だけを記録する」
//!
//! # 制限事項
//! - `LocalServer` が起動し、`USE_SECRET_CORE=true` 相当で SecretCore が接続可能であること。
//! - 管理者トークンは **環境変数** `PRODUCTION_TOOL_LS_ADMIN_TOKEN` で渡す（平文を監査ログに残さない）。
//! - AP 接続先3点組（`apBaseUrl` / `apUsername` / `apPassword`）は workflow の precheck に利用されます。
//! - workflow は `completed/OK` まで追跡するが、実ヒューズの焼込み証跡は `irreversible_stage_plan` と後続ランナーで扱う。
//!
//! # 変更履歴
//! - 2026-04-29: 初版。
//! - 2026-04-30: 本番実機で不可逆済みのときのスキップ可否と、AP `production/state` と `SecretCore` 照合により失敗し得る旨をプロンプトに追記。**`SecretCore` の検証ロジック自体は変更しない。**
//! - 2026-04-30: `workflowId` を取得し、`GET /api/workflows/{workflowId}` で `completed/OK` まで追跡するよう変更。理由: `ProductionTool` が開始要求だけでなく終端確認まで責任を持つため。

use chrono::Local;
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::env;
use std::io::{self, Write};
use std::time::Duration;

use crate::device_select_screen_state::DeviceInfo;

/// 不可逆 handoff 用の環境設定です。
#[derive(Debug, Clone)]
pub struct IrreversibleHandoffEnv {
    /// `LocalServer` のベース URL（例: `http://127.0.0.1:3200`）。
    pub local_server_base_url: String,
    /// 管理者 API 用バearer トークン（`POST /api/auth/login` の応答と同一形式）。
    pub admin_token: String,
    /// メンテナンス AP の HTTP ベース URL。
    pub ap_base_url: String,
    /// メンテナンス AP のメーカー（または admin）ユーザー名。
    pub ap_username: String,
    /// メンテナンス AP のパスワード。
    pub ap_password: String,
    /// 任意: 最小空きヒープしきい値（省略時は 7041 スクリプト既定に合わせる）。
    pub minimum_free_heap_bytes: u64,
    /// 任意: 最小スタック余裕しきい値（省略時は 7041 スクリプト既定に合わせる）。
    pub minimum_stack_margin_bytes: u64,
}

/// 環境変数から handoff 設定を読み込みます。必須が欠ける場合は `None`（スキップ可能）を返します。
///
/// # 必須環境変数（handoff 実施時）
/// - `PRODUCTION_TOOL_LS_ADMIN_TOKEN`
/// - `PRODUCTION_TOOL_AP_BASE_URL`
/// - `PRODUCTION_TOOL_AP_MFG_USERNAME`（または `PRODUCTION_TOOL_AP_USERNAME`）
/// - `PRODUCTION_TOOL_AP_MFG_PASSWORD`（または `PRODUCTION_TOOL_AP_PASSWORD`）
///
/// # 任意
/// - `PRODUCTION_TOOL_LS_BASE_URL`（既定 `http://127.0.0.1:3200`）
/// - `PRODUCTION_TOOL_MIN_FREE_HEAP_BYTES`（既定 `50000`）
/// - `PRODUCTION_TOOL_MIN_STACK_MARGIN_BYTES`（既定 `4096`）
pub fn load_irreversible_handoff_env() -> Option<IrreversibleHandoffEnv> {
    let admin_token = env::var("PRODUCTION_TOOL_LS_ADMIN_TOKEN").unwrap_or_default();
    let ap_base_url = env::var("PRODUCTION_TOOL_AP_BASE_URL").unwrap_or_default();
    let ap_username = env::var("PRODUCTION_TOOL_AP_MFG_USERNAME")
        .or_else(|_| env::var("PRODUCTION_TOOL_AP_USERNAME"))
        .unwrap_or_default();
    let ap_password = env::var("PRODUCTION_TOOL_AP_MFG_PASSWORD")
        .or_else(|_| env::var("PRODUCTION_TOOL_AP_PASSWORD"))
        .unwrap_or_default();

    if admin_token.trim().is_empty()
        || ap_base_url.trim().is_empty()
        || ap_username.trim().is_empty()
        || ap_password.is_empty()
    {
        return None;
    }

    let local_server_base_url = env::var("PRODUCTION_TOOL_LS_BASE_URL")
        .unwrap_or_else(|_| "http://127.0.0.1:3200".to_string());

    let minimum_free_heap_bytes = env::var("PRODUCTION_TOOL_MIN_FREE_HEAP_BYTES")
        .ok()
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(50000);

    let minimum_stack_margin_bytes = env::var("PRODUCTION_TOOL_MIN_STACK_MARGIN_BYTES")
        .ok()
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(4096);

    Some(IrreversibleHandoffEnv {
        local_server_base_url: local_server_base_url.trim_end_matches('/').to_string(),
        admin_token: admin_token.trim().to_string(),
        ap_base_url: ap_base_url.trim().to_string(),
        ap_username: ap_username.trim().to_string(),
        ap_password,
        minimum_free_heap_bytes,
        minimum_stack_margin_bytes,
    })
}

/// handoff 依頼の監査用メタ情報（トークン・パスワードは含めません）。
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IrreversibleHandoffAuditRecord {
    pub started_at_local: String,
    pub local_server_base_url: String,
    pub target_device_id: String,
    pub run_id: String,
    pub workflow_id: String,
    pub final_state: String,
    pub final_result: Option<String>,
    pub result_label: String,
    pub detail_summary: String,
}

/// LocalServer の workflow 応答です。
#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
struct LocalServerWorkflowResponse {
    /// API レベルの結果です。
    result: String,
    /// Rust 側 workflow 状態です。
    workflow: WorkflowStatusDto,
}

/// `SecretCore` workflow の最小状態 DTO です。
#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
struct WorkflowStatusDto {
    /// workflow ID です。
    workflow_id: String,
    /// workflow 種別です。
    workflow_type: String,
    /// 対象機 ID です。
    target_device_id: Option<String>,
    /// 進捗状態です。
    state: String,
    /// 結果です。
    result: Option<String>,
    /// エラー要約です。
    error_summary: Option<String>,
    /// 監査詳細です。
    detail: Option<String>,
}

/// 操作者に確認し、承認時に LocalServer へ POST します。
///
/// # 引数
/// - `selected_device`: PT-003 で確定した対象機。`targetDeviceId` には `public_id` を使用します（LocalServer の `targetName` と一致させること）。
/// - `work_order_id`: 作業指示番号（operatorComment に含める）。
/// - `operator_id`: 操作者 ID（空可）。
///
/// # 戻り値
/// - `Ok(Some(audit))` 成功
/// - `Ok(None)` スキップ
/// - `Err` 失敗
pub async fn prompt_and_post_irreversible_workflow_acceptance(
    selected_device: &DeviceInfo,
    work_order_id: &str,
    operator_id: &str,
) -> Result<Option<IrreversibleHandoffAuditRecord>, String> {
    if env::var("PRODUCTION_TOOL_DISABLE_IRREVERSIBLE_HANDOFF")
        .unwrap_or_default()
        .eq_ignore_ascii_case("1")
    {
        println!("  [handoff] PRODUCTION_TOOL_DISABLE_IRREVERSIBLE_HANDOFF=1 のためスキップします。");
        return Ok(None);
    }

    let Some(env_cfg) = load_irreversible_handoff_env() else {
        println!("  [handoff] 環境変数不足のためスキップします。");
        println!("  [handoff] 必要: PRODUCTION_TOOL_LS_ADMIN_TOKEN, PRODUCTION_TOOL_AP_BASE_URL,");
        println!("            PRODUCTION_TOOL_AP_MFG_USERNAME, PRODUCTION_TOOL_AP_MFG_PASSWORD");
        println!("  [handoff] 任意: PRODUCTION_TOOL_LS_BASE_URL, PRODUCTION_TOOL_MIN_FREE_HEAP_BYTES, ...");
        return Ok(None);
    };

    println!("\n=== PT-005a 不可逆監査受理（LocalServer / SecretCore 記録） ===");
    println!("  [厳守] ここでは eFuse を焼きません。`007-B` の実 espefuse は計画書 §6.4 に従い別作業です。");
    println!("  [参照] 本番1台目で実機の不可逆（§6.4）を**既に完了**している場合も、ここはあくまで**サーバー側の監査記録**です。");
    println!("          当日に重複記録が不要なら**空欄でスキップ**可。`PRODUCTION_TOOL_DISABLE_IRREVERSIBLE_HANDOFF=1` でも抑止可。");
    println!("  [注意] SecretCore は AP `GET /api/production/state` の `state` が空でないとき **precheck_collected** を要求します。");
    println!("          実機作業後に AP 状態が `idle` 等へ戻っていると workflow は**失敗**します。その場合は**スキップ**してください（ハードは焼かれません）。");
    let target_device_hint = env::var("PRODUCTION_TOOL_TARGET_DEVICE_ID")
        .unwrap_or_else(|_| selected_device.public_id.clone());
    println!(
        "  [重要] targetDeviceId は public_id または PRODUCTION_TOOL_TARGET_DEVICE_ID: {}",
        target_device_hint.trim()
    );
    print!("  続行する場合は大文字で IRREVERSIBLE と入力（空欄でスキップ）: ");
    io::stdout().flush().map_err(|e| format!("stdout flush failed. detail={}", e))?;
    let mut confirm = String::new();
    io::stdin()
        .read_line(&mut confirm)
        .map_err(|e| format!("stdin read failed. detail={}", e))?;
    if confirm.trim() != "IRREVERSIBLE" {
        println!("  [handoff] スキップしました。");
        return Ok(None);
    }

    let run_id = format!(
        "production-pt-{}",
        Local::now().format("%Y%m%d-%H%M%S")
    );
    let target_device_id = env::var("PRODUCTION_TOOL_TARGET_DEVICE_ID")
        .unwrap_or_else(|_| selected_device.public_id.trim().to_string());
    let target_device_id = target_device_id.trim().to_string();
    if target_device_id.is_empty() {
        return Err("targetDeviceId is empty. Set public_id in PT-003 or PRODUCTION_TOOL_TARGET_DEVICE_ID.".to_string());
    }

    let mac_normalized = normalize_mac_for_api(&selected_device.mac_address)?;
    let fw_version = selected_device.firmware_version.trim().to_string();
    if fw_version.is_empty() {
        return Err("firmware_version is empty. PT-003 の FW 版を入力してください。".to_string());
    }

    let operator_comment = format!(
        "ProductionTool_PT005a_irreversible_handoff workOrder={} operatorId={}",
        work_order_id,
        operator_id.trim()
    );

    let payload = json!({
        "targetDeviceId": target_device_id,
        "runId": run_id,
        "productionSettings": {
            "dryRun": false,
            "allowIrreversibleExecution": true,
            "expectedMac": mac_normalized,
            "expectedFirmwareVersion": fw_version,
            "minimumFreeHeapBytes": env_cfg.minimum_free_heap_bytes,
            "minimumStackMarginBytes": env_cfg.minimum_stack_margin_bytes,
            "apBaseUrl": env_cfg.ap_base_url,
            "apUsername": env_cfg.ap_username,
            "apPassword": env_cfg.ap_password,
            "operatorComment": operator_comment
        }
    });

    let url = format!(
        "{}/api/workflows/production/start",
        env_cfg.local_server_base_url
    );

    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(180))
        .build()
        .map_err(|e| format!("reqwest Client build failed. detail={}", e))?;

    let response = client
        .post(&url)
        .header("Authorization", format!("Bearer {}", env_cfg.admin_token))
        .header("Content-Type", "application/json")
        .json(&payload)
        .send()
        .await
        .map_err(|e| format!("POST production/start failed. url={} detail={}", url, e))?;

    let status = response.status();
    let body_text = response
        .text()
        .await
        .map_err(|e| format!("read response body failed. detail={}", e))?;

    if !status.is_success() {
        return Err(format!(
            "production workflow start NG. httpStatus={} body={}",
            status, body_text
        ));
    }

    let start_response: LocalServerWorkflowResponse = serde_json::from_str(&body_text)
        .map_err(|e| format!("decode production/start response failed. detail={} body={}", e, body_text))?;
    if !start_response.result.eq_ignore_ascii_case("OK") {
        return Err(format!(
            "production workflow start result NG. result={} body={}",
            start_response.result, body_text
        ));
    }

    println!("  [handoff] OK。HTTP {} 応答を受信しました。", status);
    println!(
        "  [handoff] workflowId={} state={} を追跡します。",
        start_response.workflow.workflow_id,
        start_response.workflow.state
    );

    let final_workflow = poll_workflow_until_finished(
        &client,
        &env_cfg.local_server_base_url,
        &env_cfg.admin_token,
        &start_response.workflow.workflow_id,
    )
    .await?;

    let final_summary = format!(
        "workflowId={} workflowType={} targetDeviceId={} state={} result={} detail={} errorSummary={}",
        final_workflow.workflow_id,
        final_workflow.workflow_type,
        final_workflow.target_device_id.clone().unwrap_or_else(|| "(empty)".to_string()),
        final_workflow.state,
        final_workflow.result.clone().unwrap_or_else(|| "(empty)".to_string()),
        final_workflow.detail.clone().unwrap_or_else(|| "(empty)".to_string()),
        final_workflow.error_summary.clone().unwrap_or_else(|| "(empty)".to_string())
    );

    if final_workflow.state != "completed"
        || final_workflow
            .result
            .as_ref()
            .map(|value| !value.eq_ignore_ascii_case("OK"))
            .unwrap_or(true)
    {
        return Err(format!(
            "production workflow finished without OK. {}",
            final_summary
        ));
    }

    println!("  [handoff] workflow 終端確認 OK: {}", final_summary);

    Ok(Some(IrreversibleHandoffAuditRecord {
        started_at_local: Local::now().to_rfc3339(),
        local_server_base_url: env_cfg.local_server_base_url,
        target_device_id,
        run_id,
        workflow_id: final_workflow.workflow_id,
        final_state: final_workflow.state,
        final_result: final_workflow.result,
        result_label: "workflow_completed_ok".to_string(),
        detail_summary: final_summary,
    }))
}

/// LocalServer workflow を終端状態まで監視します。
///
/// # 引数
/// - `client`: HTTP client。
/// - `local_server_base_url`: LocalServer ベース URL。
/// - `admin_token`: 管理者 bearer token。
/// - `workflow_id`: 追跡対象 workflow ID。
///
/// # 戻り値
/// `completed` または `failed` など、終端到達時の workflow 状態です。
async fn poll_workflow_until_finished(
    client: &reqwest::Client,
    local_server_base_url: &str,
    admin_token: &str,
    workflow_id: &str,
) -> Result<WorkflowStatusDto, String> {
    let timeout_seconds = env::var("PRODUCTION_TOOL_WORKFLOW_POLL_TIMEOUT_SECONDS")
        .ok()
        .and_then(|value| value.parse::<u64>().ok())
        .unwrap_or(60);
    let interval_millis = env::var("PRODUCTION_TOOL_WORKFLOW_POLL_INTERVAL_MILLIS")
        .ok()
        .and_then(|value| value.parse::<u64>().ok())
        .unwrap_or(1000);
    let max_attempts = std::cmp::max(1, (timeout_seconds * 1000) / interval_millis);
    let status_url = format!("{}/api/workflows/{}", local_server_base_url, workflow_id);

    for attempt_number in 1..=max_attempts {
        let response = client
            .get(&status_url)
            .header("Authorization", format!("Bearer {}", admin_token))
            .send()
            .await
            .map_err(|e| format!("poll workflow failed. url={} attempt={} detail={}", status_url, attempt_number, e))?;
        let status = response.status();
        let body_text = response
            .text()
            .await
            .map_err(|e| format!("poll workflow body read failed. detail={}", e))?;
        if !status.is_success() {
            return Err(format!(
                "poll workflow NG. httpStatus={} attempt={} body={}",
                status, attempt_number, body_text
            ));
        }
        let status_response: LocalServerWorkflowResponse = serde_json::from_str(&body_text)
            .map_err(|e| format!("decode workflow status failed. attempt={} detail={} body={}", attempt_number, e, body_text))?;
        let workflow = status_response.workflow;
        println!(
            "  [handoff] workflow poll {}/{}: state={} result={}",
            attempt_number,
            max_attempts,
            workflow.state,
            workflow.result.clone().unwrap_or_else(|| "(empty)".to_string())
        );
        if workflow.state == "completed" || workflow.state == "failed" {
            return Ok(workflow);
        }
        tokio::time::sleep(Duration::from_millis(interval_millis)).await;
    }

    Err(format!(
        "poll workflow timeout. workflowId={} timeoutSeconds={} intervalMillis={}",
        workflow_id, timeout_seconds, interval_millis
    ))
}

/// MAC を API 検証に通しやすい形式へ正規化します（コロン区切り小文字）。
fn normalize_mac_for_api(raw_mac: &str) -> Result<String, String> {
    let trimmed = raw_mac.trim();
    if trimmed.is_empty() {
        return Err("mac_address is empty.".to_string());
    }
    let compact: String = trimmed
        .chars()
        .filter(|c| c.is_ascii_hexdigit())
        .collect::<String>()
        .to_lowercase();
    if compact.len() != 12 {
        return Err(format!(
            "mac_address must normalize to 12 hex digits. raw={}",
            raw_mac
        ));
    }
    let with_colons = compact
        .as_bytes()
        .chunks(2)
        .map(|chunk| std::str::from_utf8(chunk).unwrap_or(""))
        .collect::<Vec<_>>()
        .join(":");
    Ok(with_colons)
}
