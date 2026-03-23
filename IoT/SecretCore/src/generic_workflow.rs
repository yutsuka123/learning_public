// generic_workflow.rs - SecretCore の Production workflow 管理。
//
// [重要] `runProductionSecureFlow()` の開始要求と状態取得を Rust 側へ固定する。
// [厳守] LocalServer へ返す情報は workflowId / state / result / errorSummary / 最小補助情報に限定する。
// [禁止] raw key、中間秘密、eFuse 実値を workflow 状態へ保存しない。
// [重要] 現段階では dry-run と事前チェック判定だけを扱い、不可逆処理本体は実行しない。
// 変更日: 2026-03-22 dry-run 段階実行骨格と事前チェック判定を追加。理由: 005-0006 / 005-0007 / 008-0025 の前準備を SecretCore 側へ寄せるため。

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::time::sleep;

const ALLOWED_STAGE_NAME_LIST: [&str; 6] = [
    "precheck",
    "lock_environment",
    "stage_prepare",
    "stage_execute",
    "stage_readback_verify",
    "final_judgement",
];

/// Production workflow の事前チェック観測値。
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProductionWorkflowPrecheckSnapshot {
    pub target_device_matched: Option<bool>,
    pub power_stable: Option<bool>,
    pub firmware_version_approved: Option<bool>,
    pub key_id_verified: Option<bool>,
    pub unsecured_state_confirmed: Option<bool>,
    pub operator_authenticated: Option<bool>,
    pub stack_margin_ok: Option<bool>,
    pub heap_margin_ok: Option<bool>,
    pub measured_free_heap_bytes: Option<u64>,
    pub measured_min_stack_margin_bytes: Option<u64>,
}

/// Production workflow 実行設定。
#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProductionWorkflowSettings {
    pub dry_run: bool,
    pub step_plan: Option<Vec<String>>,
    pub operator_comment: Option<String>,
    pub expected_serial: Option<String>,
    pub expected_mac: Option<String>,
    pub expected_firmware_version: Option<String>,
    pub minimum_free_heap_bytes: Option<u64>,
    pub minimum_stack_margin_bytes: Option<u64>,
    pub ap_base_url: Option<String>,
    pub ap_username: Option<String>,
    pub ap_password: Option<String>,
    pub precheck_snapshot: Option<ProductionWorkflowPrecheckSnapshot>,
}

/// Production workflow 開始要求。
#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProductionWorkflowStartRequest {
    /// 対象デバイス識別子。
    pub target_device_id: String,
    /// 実行識別子。
    pub run_id: String,
    /// dry-run などの実行設定。
    pub production_settings: ProductionWorkflowSettings,
}

/// workflow 状態DTO。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct GenericWorkflowStatusDto {
    /// workflow 識別子。
    workflow_id: String,
    /// workflow 種別。
    workflow_type: String,
    /// 対象デバイス識別子。
    target_device_id: String,
    /// workflow 状態。
    state: String,
    /// 最終結果。
    result: Option<String>,
    /// エラー要約。
    error_summary: Option<String>,
    /// 補助詳細。
    detail: Option<String>,
    /// 開始日時。
    started_at: String,
    /// 最終更新日時。
    updated_at: String,
}

/// Production workflow の段階計画。
#[derive(Debug, Clone)]
struct ProductionExecutionPlan {
    stage_name_list: Vec<String>,
}

/// 事前チェック評価結果。
#[derive(Debug, Clone)]
struct ProductionPrecheckReport {
    ok_item_list: Vec<String>,
    pending_item_list: Vec<String>,
    failed_item_list: Vec<String>,
}

/// AP ログイン応答。
#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct ProductionApLoginResponse {
    result: String,
    token: Option<String>,
}

/// AP 事前チェック応答。
#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct ProductionApPrecheckResponse {
    result: String,
    target_device_matched: Option<bool>,
    firmware_version_approved: Option<bool>,
    stack_margin_ok: Option<bool>,
    heap_margin_ok: Option<bool>,
    measured_free_heap_bytes: Option<u64>,
    measured_min_stack_margin_bytes: Option<u64>,
    observed_firmware_version: Option<String>,
    observed_mac: Option<String>,
    detail: Option<String>,
}

/// AP production state 応答。
#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct ProductionApStateResponse {
    result: String,
    run_id: Option<String>,
    state: Option<String>,
    result_label: Option<String>,
    target_device_id: Option<String>,
    observed_firmware_version: Option<String>,
    observed_mac: Option<String>,
    measured_free_heap_bytes: Option<u64>,
    measured_min_stack_margin_bytes: Option<u64>,
    detail: Option<String>,
}

/// Production workflow の状態管理器。
pub struct ProductionWorkflowManager {
    workflow_map: Mutex<HashMap<String, GenericWorkflowStatusDto>>,
}

impl ProductionWorkflowManager {
    /// 管理器を初期化する。
    pub fn new() -> Self {
        Self {
            workflow_map: Mutex::new(HashMap::new()),
        }
    }

    /// workflow 状態を取得する。
    pub fn get_workflow_status(&self, workflow_id: &str) -> Result<GenericWorkflowStatusDto, String> {
        let workflow_map_guard = self
            .workflow_map
            .lock()
            .map_err(|e| format!("get_workflow_status failed. workflow-map lock error={} workflow_id={}", e, workflow_id))?;
        workflow_map_guard
            .get(workflow_id)
            .cloned()
            .ok_or_else(|| format!("get_workflow_status failed. workflow_id not found. workflow_id={}", workflow_id))
    }

    /// Production workflow を開始する。
    pub fn start_production_secure_flow(
        self: &Arc<Self>,
        request: ProductionWorkflowStartRequest,
    ) -> Result<GenericWorkflowStatusDto, String> {
        validate_production_request(&request)?;
        let workflow_id = create_workflow_id("production");
        let started_at = chrono::Utc::now().to_rfc3339();
        let execution_plan = build_execution_plan(&request)?;
        let initial_status = GenericWorkflowStatusDto {
            workflow_id: workflow_id.clone(),
            workflow_type: "production".to_string(),
            target_device_id: request.target_device_id.clone(),
            state: "queued".to_string(),
            result: None,
            error_summary: None,
            detail: Some(format!(
                "workflow queued. runId={} dryRun={} stagePlan={}",
                request.run_id,
                request.production_settings.dry_run,
                execution_plan.stage_name_list.join("->")
            )),
            started_at: started_at.clone(),
            updated_at: started_at,
        };
        self.update_workflow_status(initial_status.clone())?;

        let workflow_manager = Arc::clone(self);
        tokio::spawn(async move {
            workflow_manager.execute_production_workflow(workflow_id, request, execution_plan).await;
        });

        Ok(initial_status)
    }

    async fn execute_production_workflow(
        self: Arc<Self>,
        workflow_id: String,
        request: ProductionWorkflowStartRequest,
        execution_plan: ProductionExecutionPlan,
    ) {
        let effective_precheck_snapshot = match resolve_effective_precheck_snapshot(&request).await {
            Ok(snapshot) => snapshot,
            Err(error_message) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(format!(
                        "production precheck collection failed. targetDeviceId={} runId={} detail={}",
                        request.target_device_id, request.run_id, error_message
                    )),
                    Some("stage=precheck result=failed. reason=ap precheck fetch or merge failed.".to_string()),
                );
                return;
            }
        };
        let precheck_report = evaluate_precheck_snapshot(Some(&effective_precheck_snapshot));
        let remote_production_state_option = match verify_remote_production_state_after_precheck(&request).await {
            Ok(remote_production_state_option) => remote_production_state_option,
            Err(error_message) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(format!(
                        "production state verification failed. targetDeviceId={} runId={} detail={}",
                        request.target_device_id, request.run_id, error_message
                    )),
                    Some("stage=precheck result=failed. reason=ap production/state verification failed.".to_string()),
                );
                return;
            }
        };
        let expected_serial_text = request
            .production_settings
            .expected_serial
            .clone()
            .unwrap_or_else(|| "(empty)".to_string());
        let expected_mac_text = request
            .production_settings
            .expected_mac
            .clone()
            .unwrap_or_else(|| "(empty)".to_string());

        let _ = self.update_state(
            &workflow_id,
            "running",
            None,
            None,
            Some(format!(
                "stage=precheck runId={} targetDeviceId={} expectedSerial={} expectedMac={} precheckOk={} precheckPending={} precheckFailed={} apState={} apObservedFirmwareVersion={} apObservedMac={}",
                request.run_id,
                request.target_device_id,
                expected_serial_text,
                expected_mac_text,
                join_or_placeholder(&precheck_report.ok_item_list),
                join_or_placeholder(&precheck_report.pending_item_list),
                join_or_placeholder(&precheck_report.failed_item_list),
                remote_production_state_option
                    .as_ref()
                    .and_then(|state| state.state.clone())
                    .unwrap_or_else(|| "(not-used)".to_string()),
                remote_production_state_option
                    .as_ref()
                    .and_then(|state| state.observed_firmware_version.clone())
                    .unwrap_or_else(|| "(not-used)".to_string()),
                remote_production_state_option
                    .as_ref()
                    .and_then(|state| state.observed_mac.clone())
                    .unwrap_or_else(|| "(not-used)".to_string())
            )),
        );
        sleep(Duration::from_millis(60)).await;

        if !precheck_report.failed_item_list.is_empty() {
            let _ = self.update_state(
                &workflow_id,
                "failed",
                Some("NG".to_string()),
                Some(format!(
                    "production precheck failed. targetDeviceId={} runId={} failedItems={}",
                    request.target_device_id,
                    request.run_id,
                    precheck_report.failed_item_list.join(",")
                )),
                Some(format!(
                    "stage=precheck result=failed measuredFreeHeapBytes={} measuredMinStackMarginBytes={}",
                    optional_u64_to_text(
                        effective_precheck_snapshot.measured_free_heap_bytes
                    ),
                    optional_u64_to_text(
                        effective_precheck_snapshot.measured_min_stack_margin_bytes
                    )
                )),
            );
            return;
        }

        let _ = self.update_state(
            &workflow_id,
            "waiting_device",
            None,
            None,
            Some(format!(
                "stage=lock_environment|stage_prepare runId={} dryRun={} pendingItems={} operatorCommentPresent={} apState={} apResultLabel={} apMeasuredFreeHeapBytes={} apMeasuredMinStackMarginBytes={}",
                request.run_id,
                request.production_settings.dry_run,
                join_or_placeholder(&precheck_report.pending_item_list),
                request
                    .production_settings
                    .operator_comment
                    .as_ref()
                    .map(|value| !value.trim().is_empty())
                    .unwrap_or(false),
                remote_production_state_option
                    .as_ref()
                    .and_then(|state| state.state.clone())
                    .unwrap_or_else(|| "(not-used)".to_string()),
                remote_production_state_option
                    .as_ref()
                    .and_then(|state| state.result_label.clone())
                    .unwrap_or_else(|| "(not-used)".to_string()),
                remote_production_state_option
                    .as_ref()
                    .and_then(|state| state.measured_free_heap_bytes)
                    .map(|value| value.to_string())
                    .unwrap_or_else(|| "(not-used)".to_string()),
                remote_production_state_option
                    .as_ref()
                    .and_then(|state| state.measured_min_stack_margin_bytes)
                    .map(|value| value.to_string())
                    .unwrap_or_else(|| "(not-used)".to_string())
            )),
        );
        sleep(Duration::from_millis(60)).await;

        let _ = self.update_state(
            &workflow_id,
            "verifying",
            None,
            None,
            Some(format!(
                "stage=stage_readback_verify|final_judgement runId={} stagePlan={} stopBefore=stage_execute",
                request.run_id,
                execution_plan.stage_name_list.join("->")
            )),
        );
        sleep(Duration::from_millis(60)).await;

        if !request.production_settings.dry_run {
            let _ = self.update_state(
                &workflow_id,
                "failed",
                Some("NG".to_string()),
                Some(format!(
                    "run_production_secure_flow blocked. irreversible execution is disabled in current phase. targetDeviceId={} runId={}",
                    request.target_device_id, request.run_id
                )),
                Some(
                    "stage=final_judgement result=blocked. reason=phase-D only allows dry-run skeleton and precheck validation."
                        .to_string(),
                ),
            );
            return;
        }

        let _ = self.update_state(
            &workflow_id,
            "completed",
            Some("OK".to_string()),
            None,
            Some(format!(
                "dry-run completed. runId={} targetDeviceId={} stagePlan={} precheckPendingItems={} irreversibleExecution=false",
                request.run_id,
                request.target_device_id,
                execution_plan.stage_name_list.join("->"),
                join_or_placeholder(&precheck_report.pending_item_list)
            )),
        );
    }

    fn update_state(
        &self,
        workflow_id: &str,
        state: &str,
        result: Option<String>,
        error_summary: Option<String>,
        detail: Option<String>,
    ) -> Result<(), String> {
        let mut workflow_map_guard = self
            .workflow_map
            .lock()
            .map_err(|e| format!("update_state failed. workflow-map lock error={} workflow_id={}", e, workflow_id))?;
        let existing_status = workflow_map_guard
            .get(workflow_id)
            .cloned()
            .ok_or_else(|| format!("update_state failed. workflow_id not found. workflow_id={}", workflow_id))?;
        workflow_map_guard.insert(
            workflow_id.to_string(),
            GenericWorkflowStatusDto {
                state: state.to_string(),
                result,
                error_summary,
                detail,
                updated_at: chrono::Utc::now().to_rfc3339(),
                ..existing_status
            },
        );
        Ok(())
    }

    fn update_workflow_status(&self, status: GenericWorkflowStatusDto) -> Result<(), String> {
        let mut workflow_map_guard = self
            .workflow_map
            .lock()
            .map_err(|e| format!("update_workflow_status failed. workflow-map lock error={}", e))?;
        workflow_map_guard.insert(status.workflow_id.clone(), status);
        Ok(())
    }
}

fn validate_production_request(request: &ProductionWorkflowStartRequest) -> Result<(), String> {
    if request.target_device_id.trim().is_empty() {
        return Err("start_production_secure_flow failed. target_device_id is empty.".to_string());
    }
    if request.run_id.trim().is_empty() {
        return Err("start_production_secure_flow failed. run_id is empty.".to_string());
    }
    validate_stage_plan(request.production_settings.step_plan.as_ref())?;
    if let Some(expected_mac) = request.production_settings.expected_mac.as_ref() {
        let normalized_expected_mac = expected_mac.trim();
        if !normalized_expected_mac.is_empty()
            && !normalized_expected_mac
                .chars()
                .all(|char_value| char_value.is_ascii_hexdigit() || char_value == ':' || char_value == '-')
        {
            return Err(format!(
                "start_production_secure_flow failed. expected_mac format is invalid. expectedMac={}",
                expected_mac
            ));
        }
    }
    validate_remote_precheck_configuration(&request.production_settings)?;
    Ok(())
}

fn validate_remote_precheck_configuration(settings: &ProductionWorkflowSettings) -> Result<(), String> {
    let field_value_list = [
        settings
            .ap_base_url
            .as_ref()
            .map(|value| value.trim())
            .unwrap_or(""),
        settings
            .ap_username
            .as_ref()
            .map(|value| value.trim())
            .unwrap_or(""),
        settings
            .ap_password
            .as_ref()
            .map(|value| value.as_str())
            .unwrap_or(""),
    ];
    let provided_field_count = field_value_list
        .iter()
        .filter(|value| !value.is_empty())
        .count();
    if provided_field_count != 0 && provided_field_count != 3 {
        return Err(
            "start_production_secure_flow failed. ap_base_url/ap_username/ap_password must be provided together."
                .to_string(),
        );
    }
    Ok(())
}

fn validate_stage_plan(step_plan: Option<&Vec<String>>) -> Result<(), String> {
    if let Some(stage_name_list) = step_plan {
        for stage_name in stage_name_list {
            let normalized_stage_name = stage_name.trim().to_lowercase();
            if normalized_stage_name.is_empty() {
                return Err("start_production_secure_flow failed. step_plan contains empty stage name.".to_string());
            }
            if !ALLOWED_STAGE_NAME_LIST.contains(&normalized_stage_name.as_str()) {
                return Err(format!(
                    "start_production_secure_flow failed. unsupported stage name. stageName={}",
                    stage_name
                ));
            }
        }
    }
    Ok(())
}

fn build_execution_plan(request: &ProductionWorkflowStartRequest) -> Result<ProductionExecutionPlan, String> {
    validate_stage_plan(request.production_settings.step_plan.as_ref())?;
    let stage_name_list = match request.production_settings.step_plan.as_ref() {
        Some(stage_name_list) if !stage_name_list.is_empty() => stage_name_list
            .iter()
            .map(|stage_name| stage_name.trim().to_lowercase())
            .collect(),
        _ => ALLOWED_STAGE_NAME_LIST
            .iter()
            .map(|stage_name| stage_name.to_string())
            .collect(),
    };
    Ok(ProductionExecutionPlan { stage_name_list })
}

fn evaluate_precheck_snapshot(
    precheck_snapshot_option: Option<&ProductionWorkflowPrecheckSnapshot>,
) -> ProductionPrecheckReport {
    let mut ok_item_list: Vec<String> = Vec::new();
    let mut pending_item_list: Vec<String> = Vec::new();
    let mut failed_item_list: Vec<String> = Vec::new();

    push_precheck_result(
        "PC-001.targetDeviceMatched",
        precheck_snapshot_option.and_then(|snapshot| snapshot.target_device_matched),
        &mut ok_item_list,
        &mut pending_item_list,
        &mut failed_item_list,
    );
    push_precheck_result(
        "PC-002.powerStable",
        precheck_snapshot_option.and_then(|snapshot| snapshot.power_stable),
        &mut ok_item_list,
        &mut pending_item_list,
        &mut failed_item_list,
    );
    push_precheck_result(
        "PC-003.firmwareVersionApproved",
        precheck_snapshot_option.and_then(|snapshot| snapshot.firmware_version_approved),
        &mut ok_item_list,
        &mut pending_item_list,
        &mut failed_item_list,
    );
    push_precheck_result(
        "PC-004.keyIdVerified",
        precheck_snapshot_option.and_then(|snapshot| snapshot.key_id_verified),
        &mut ok_item_list,
        &mut pending_item_list,
        &mut failed_item_list,
    );
    push_precheck_result(
        "PC-005.unsecuredStateConfirmed",
        precheck_snapshot_option.and_then(|snapshot| snapshot.unsecured_state_confirmed),
        &mut ok_item_list,
        &mut pending_item_list,
        &mut failed_item_list,
    );
    push_precheck_result(
        "PC-006.operatorAuthenticated",
        precheck_snapshot_option.and_then(|snapshot| snapshot.operator_authenticated),
        &mut ok_item_list,
        &mut pending_item_list,
        &mut failed_item_list,
    );
    push_precheck_result(
        "PC-007.stackMarginOk",
        precheck_snapshot_option.and_then(|snapshot| snapshot.stack_margin_ok),
        &mut ok_item_list,
        &mut pending_item_list,
        &mut failed_item_list,
    );
    push_precheck_result(
        "PC-008.heapMarginOk",
        precheck_snapshot_option.and_then(|snapshot| snapshot.heap_margin_ok),
        &mut ok_item_list,
        &mut pending_item_list,
        &mut failed_item_list,
    );

    ProductionPrecheckReport {
        ok_item_list,
        pending_item_list,
        failed_item_list,
    }
}

async fn resolve_effective_precheck_snapshot(
    request: &ProductionWorkflowStartRequest,
) -> Result<ProductionWorkflowPrecheckSnapshot, String> {
    let mut effective_snapshot = request.production_settings.precheck_snapshot.clone().unwrap_or_default();
    if !has_remote_precheck_configuration(&request.production_settings) {
        return Ok(effective_snapshot);
    }
    let remote_precheck_response = fetch_remote_precheck_snapshot(request).await?;
    if let Some(target_device_matched) = remote_precheck_response.target_device_matched {
        effective_snapshot.target_device_matched = Some(target_device_matched);
    }
    if let Some(firmware_version_approved) = remote_precheck_response.firmware_version_approved {
        effective_snapshot.firmware_version_approved = Some(firmware_version_approved);
    }
    if let Some(stack_margin_ok) = remote_precheck_response.stack_margin_ok {
        effective_snapshot.stack_margin_ok = Some(stack_margin_ok);
    }
    if let Some(heap_margin_ok) = remote_precheck_response.heap_margin_ok {
        effective_snapshot.heap_margin_ok = Some(heap_margin_ok);
    }
    if let Some(measured_free_heap_bytes) = remote_precheck_response.measured_free_heap_bytes {
        effective_snapshot.measured_free_heap_bytes = Some(measured_free_heap_bytes);
    }
    if let Some(measured_min_stack_margin_bytes) = remote_precheck_response.measured_min_stack_margin_bytes {
        effective_snapshot.measured_min_stack_margin_bytes = Some(measured_min_stack_margin_bytes);
    }
    Ok(effective_snapshot)
}

fn has_remote_precheck_configuration(settings: &ProductionWorkflowSettings) -> bool {
    settings
        .ap_base_url
        .as_ref()
        .map(|value| !value.trim().is_empty())
        .unwrap_or(false)
}

async fn fetch_remote_precheck_snapshot(
    request: &ProductionWorkflowStartRequest,
) -> Result<ProductionApPrecheckResponse, String> {
    let ap_base_url = request
        .production_settings
        .ap_base_url
        .as_ref()
        .map(|value| value.trim().trim_end_matches('/').to_string())
        .ok_or_else(|| "fetch_remote_precheck_snapshot failed. ap_base_url is missing.".to_string())?;
    let ap_username = request
        .production_settings
        .ap_username
        .as_ref()
        .map(|value| value.trim().to_string())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| "fetch_remote_precheck_snapshot failed. ap_username is missing.".to_string())?;
    let ap_password = request
        .production_settings
        .ap_password
        .as_ref()
        .cloned()
        .filter(|value| !value.is_empty())
        .ok_or_else(|| "fetch_remote_precheck_snapshot failed. ap_password is missing.".to_string())?;
    let http_client = reqwest::Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| format!("fetch_remote_precheck_snapshot failed. reqwest client build error={}", e))?;
    let login_response = http_client
        .post(format!("{}/api/auth/login", ap_base_url))
        .json(&serde_json::json!({
            "username": ap_username,
            "password": ap_password
        }))
        .send()
        .await
        .map_err(|e| format!("fetch_remote_precheck_snapshot failed. login request error={}", e))?;
    let login_status = login_response.status();
    let login_json = login_response
        .json::<ProductionApLoginResponse>()
        .await
        .map_err(|e| format!("fetch_remote_precheck_snapshot failed. login response decode error={}", e))?;
    if !login_status.is_success() || !login_json.result.eq_ignore_ascii_case("OK") {
        return Err(format!(
            "fetch_remote_precheck_snapshot failed. login rejected. httpStatus={} result={}",
            login_status,
            login_json.result
        ));
    }
    let bearer_token = login_json
        .token
        .filter(|value| !value.trim().is_empty())
        .ok_or_else(|| "fetch_remote_precheck_snapshot failed. login token is empty.".to_string())?;
    let precheck_response = http_client
        .post(format!("{}/api/production/precheck", ap_base_url))
        .bearer_auth(&bearer_token)
        .json(&serde_json::json!({
            "runId": request.run_id,
            "targetDeviceId": request.target_device_id,
            "expectedFirmwareVersion": request.production_settings.expected_firmware_version,
            "expectedMac": request.production_settings.expected_mac,
            "minimumFreeHeapBytes": request.production_settings.minimum_free_heap_bytes,
            "minimumStackMarginBytes": request.production_settings.minimum_stack_margin_bytes
        }))
        .send()
        .await
        .map_err(|e| format!("fetch_remote_precheck_snapshot failed. precheck request error={}", e))?;
    let precheck_status = precheck_response.status();
    let precheck_json = precheck_response
        .json::<ProductionApPrecheckResponse>()
        .await
        .map_err(|e| format!("fetch_remote_precheck_snapshot failed. precheck response decode error={}", e))?;
    if !precheck_status.is_success() || !precheck_json.result.eq_ignore_ascii_case("OK") {
        return Err(format!(
            "fetch_remote_precheck_snapshot failed. precheck rejected. httpStatus={} result={} detail={} observedFirmwareVersion={} observedMac={}",
            precheck_status,
            precheck_json.result,
            precheck_json.detail.clone().unwrap_or_else(|| "(empty)".to_string()),
            precheck_json
                .observed_firmware_version
                .clone()
                .unwrap_or_else(|| "(empty)".to_string()),
            precheck_json.observed_mac.clone().unwrap_or_else(|| "(empty)".to_string())
        ));
    }
    Ok(precheck_json)
}

async fn verify_remote_production_state_after_precheck(
    request: &ProductionWorkflowStartRequest,
) -> Result<Option<ProductionApStateResponse>, String> {
    if !has_remote_precheck_configuration(&request.production_settings) {
        return Ok(None);
    }
    let ap_base_url = request
        .production_settings
        .ap_base_url
        .as_ref()
        .map(|value| value.trim().trim_end_matches('/').to_string())
        .ok_or_else(|| "verify_remote_production_state_after_precheck failed. ap_base_url is missing.".to_string())?;
    let ap_username = request
        .production_settings
        .ap_username
        .as_ref()
        .map(|value| value.trim().to_string())
        .filter(|value| !value.is_empty())
        .ok_or_else(|| "verify_remote_production_state_after_precheck failed. ap_username is missing.".to_string())?;
    let ap_password = request
        .production_settings
        .ap_password
        .as_ref()
        .cloned()
        .filter(|value| !value.is_empty())
        .ok_or_else(|| "verify_remote_production_state_after_precheck failed. ap_password is missing.".to_string())?;
    let http_client = reqwest::Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| format!("verify_remote_production_state_after_precheck failed. reqwest client build error={}", e))?;
    let login_response = http_client
        .post(format!("{}/api/auth/login", ap_base_url))
        .json(&serde_json::json!({
            "username": ap_username,
            "password": ap_password
        }))
        .send()
        .await
        .map_err(|e| format!("verify_remote_production_state_after_precheck failed. login request error={}", e))?;
    let login_status = login_response.status();
    let login_json = login_response
        .json::<ProductionApLoginResponse>()
        .await
        .map_err(|e| format!("verify_remote_production_state_after_precheck failed. login response decode error={}", e))?;
    if !login_status.is_success() || !login_json.result.eq_ignore_ascii_case("OK") {
        return Err(format!(
            "verify_remote_production_state_after_precheck failed. login rejected. httpStatus={} result={}",
            login_status,
            login_json.result
        ));
    }
    let bearer_token = login_json
        .token
        .filter(|value| !value.trim().is_empty())
        .ok_or_else(|| "verify_remote_production_state_after_precheck failed. login token is empty.".to_string())?;
    let state_response = http_client
        .get(format!("{}/api/production/state", ap_base_url))
        .bearer_auth(&bearer_token)
        .send()
        .await
        .map_err(|e| format!("verify_remote_production_state_after_precheck failed. state request error={}", e))?;
    let state_status = state_response.status();
    let state_json = state_response
        .json::<ProductionApStateResponse>()
        .await
        .map_err(|e| format!("verify_remote_production_state_after_precheck failed. state response decode error={}", e))?;
    if !state_status.is_success() || !state_json.result.eq_ignore_ascii_case("OK") {
        return Err(format!(
            "verify_remote_production_state_after_precheck failed. state rejected. httpStatus={} result={} detail={}",
            state_status,
            state_json.result,
            state_json.detail.clone().unwrap_or_else(|| "(empty)".to_string())
        ));
    }
    let observed_run_id = state_json
        .run_id
        .as_ref()
        .map(|value| value.trim())
        .unwrap_or("");
    if !observed_run_id.is_empty() && observed_run_id != request.run_id {
        return Err(format!(
            "verify_remote_production_state_after_precheck failed. runId mismatch. expectedRunId={} actualRunId={}",
            request.run_id, observed_run_id
        ));
    }
    let observed_target_device_id = state_json
        .target_device_id
        .as_ref()
        .map(|value| value.trim())
        .unwrap_or("");
    if !observed_target_device_id.is_empty() && observed_target_device_id != request.target_device_id {
        return Err(format!(
            "verify_remote_production_state_after_precheck failed. targetDeviceId mismatch. expectedTargetDeviceId={} actualTargetDeviceId={}",
            request.target_device_id, observed_target_device_id
        ));
    }
    let observed_state = state_json
        .state
        .as_ref()
        .map(|value| value.trim())
        .unwrap_or("");
    if !observed_state.is_empty() && observed_state != "precheck_collected" {
        return Err(format!(
            "verify_remote_production_state_after_precheck failed. unexpected state. expected=precheck_collected actual={}",
            observed_state
        ));
    }
    Ok(Some(state_json))
}

fn push_precheck_result(
    item_name: &str,
    item_value_option: Option<bool>,
    ok_item_list: &mut Vec<String>,
    pending_item_list: &mut Vec<String>,
    failed_item_list: &mut Vec<String>,
) {
    match item_value_option {
        Some(true) => ok_item_list.push(item_name.to_string()),
        Some(false) => failed_item_list.push(item_name.to_string()),
        None => pending_item_list.push(item_name.to_string()),
    }
}

fn join_or_placeholder(item_list: &[String]) -> String {
    if item_list.is_empty() {
        "(none)".to_string()
    } else {
        item_list.join(",")
    }
}

fn optional_u64_to_text(value_option: Option<u64>) -> String {
    value_option
        .map(|value| value.to_string())
        .unwrap_or_else(|| "(unknown)".to_string())
}

fn create_workflow_id(prefix: &str) -> String {
    let random_suffix: u32 = rand::random();
    format!("{}-{}-{:08x}", prefix, chrono::Utc::now().format("%Y%m%d%H%M%S"), random_suffix)
}
