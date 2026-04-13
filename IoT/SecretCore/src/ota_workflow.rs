// ota_workflow.rs - SecretCore の OTA 専用 workflow 管理。
//
// [重要] OTA 開始要求から進捗監視・完了判定までを Rust 側へ集約する。
// [厳守] LocalServer へ返す情報は workflowId / state / result / errorSummary / 最小補助情報に限定する。
// [厳守] MQTT payload 暗号化モードは既存運用（plain / compat / strict）と同じ意味を維持する。
// [禁止] raw key や中間秘密を workflow 状態へ保存しない。
// 変更日: 2026-03-15 Stage7 OTA workflow を追加。理由: 003-0012 の「開始後監視と完了判定」責務を Rust 側へ寄せるため。

use crate::key_manager::KeyManager;
use crate::mqtt_transport::{DeviceStateSummaryDto, MqttReceiverManager};
use rand::RngExt;
use serde::Serialize;
use std::collections::HashMap;
use std::env;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::time::{sleep, Instant};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum MqttPayloadEncryptionMode {
    Plain,
    Compat,
    Strict,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WorkflowStatusDto {
    workflow_id: String,
    workflow_type: String,
    target_device_name: String,
    state: String,
    result: Option<String>,
    error_summary: Option<String>,
    detail: Option<String>,
    started_at: String,
    updated_at: String,
    firmware_version: String,
}

pub struct OtaWorkflowManager {
    workflow_map: Mutex<HashMap<String, WorkflowStatusDto>>,
}

impl OtaWorkflowManager {
    pub fn new() -> Self {
        Self {
            workflow_map: Mutex::new(HashMap::new()),
        }
    }

    pub fn get_workflow_status(&self, workflow_id: &str) -> Result<WorkflowStatusDto, String> {
        let workflow_map_guard = self
            .workflow_map
            .lock()
            .map_err(|e| format!("get_workflow_status failed. workflow-map lock error={} workflow_id={}", e, workflow_id))?;
        workflow_map_guard
            .get(workflow_id)
            .cloned()
            .ok_or_else(|| format!("get_workflow_status failed. workflow_id not found. workflow_id={}", workflow_id))
    }

    pub fn start_signed_ota_command(
        self: &Arc<Self>,
        mqtt_receiver_manager: Arc<MqttReceiverManager>,
        key_manager: Arc<KeyManager>,
        target_device_name: String,
        manifest_url: String,
        firmware_url: String,
        firmware_version: String,
        sha256: String,
        timeout_seconds: u64,
    ) -> Result<WorkflowStatusDto, String> {
        if target_device_name.trim().is_empty() {
            return Err("start_signed_ota_command failed. target_device_name is empty.".to_string());
        }
        if manifest_url.trim().is_empty() || firmware_url.trim().is_empty() || firmware_version.trim().is_empty() || sha256.trim().is_empty()
        {
            return Err(
                "start_signed_ota_command failed. manifest_url / firmware_url / firmware_version / sha256 are required."
                    .to_string(),
            );
        }

        let workflow_id = create_workflow_id();
        let started_at = chrono::Utc::now().to_rfc3339();
        let initial_status = WorkflowStatusDto {
            workflow_id: workflow_id.clone(),
            workflow_type: "signed-ota".to_string(),
            target_device_name: target_device_name.clone(),
            state: "queued".to_string(),
            result: None,
            error_summary: None,
            detail: Some("workflow queued".to_string()),
            started_at: started_at.clone(),
            updated_at: started_at,
            firmware_version: firmware_version.clone(),
        };
        self.update_workflow_status(initial_status.clone())?;

        let workflow_manager = Arc::clone(self);
        tokio::spawn(async move {
            workflow_manager
                .execute_signed_ota_workflow(
                    mqtt_receiver_manager,
                    key_manager,
                    workflow_id,
                    target_device_name,
                    manifest_url,
                    firmware_url,
                    firmware_version,
                    sha256,
                    timeout_seconds,
                )
                .await;
        });

        Ok(initial_status)
    }

    async fn execute_signed_ota_workflow(
        self: Arc<Self>,
        mqtt_receiver_manager: Arc<MqttReceiverManager>,
        key_manager: Arc<KeyManager>,
        workflow_id: String,
        target_device_name: String,
        manifest_url: String,
        firmware_url: String,
        firmware_version: String,
        sha256: String,
        timeout_seconds: u64,
    ) {
        let started_at = chrono::Utc::now().to_rfc3339();
        if let Err(status_error) = self.update_state(
            &workflow_id,
            "running",
            None,
            None,
            Some("publishing otaStart command".to_string()),
        ) {
            eprintln!("{}", status_error);
        }

        let publish_result = publish_ota_command(
            mqtt_receiver_manager.as_ref(),
            &key_manager,
            &target_device_name,
            &manifest_url,
            &firmware_url,
            &firmware_version,
            &sha256,
            timeout_seconds,
        )
        .await;
        if let Err(publish_error) = publish_result {
            let _ = self.update_state(
                &workflow_id,
                "failed",
                Some("NG".to_string()),
                Some(publish_error),
                Some("ota publish failed".to_string()),
            );
            return;
        }

        let _ = self.update_state(
            &workflow_id,
            "waiting_device",
            None,
            None,
            Some("waiting ota progress".to_string()),
        );

        let safe_timeout_seconds = timeout_seconds.max(1);
        let deadline_instant = Instant::now() + Duration::from_secs(safe_timeout_seconds);
        let mut last_state_text = String::new();

        loop {
            if Instant::now() >= deadline_instant {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(format!(
                        "ota workflow timeout. seconds={} target_device_name={} firmware_version={}",
                        safe_timeout_seconds, target_device_name, firmware_version
                    )),
                    Some("workflow timed out".to_string()),
                );
                return;
            }

            match mqtt_receiver_manager.get_device_state_summary(&target_device_name) {
                Ok(Some(device_state)) => {
                    let detail_text = build_workflow_detail_text(&device_state);
                    if detail_text != last_state_text {
                        last_state_text = detail_text.clone();
                        let workflow_state = if is_verifying_phase(&device_state) {
                            "verifying"
                        } else {
                            "waiting_device"
                        };
                        let _ = self.update_state(&workflow_id, workflow_state, None, None, Some(detail_text));
                    }

                    if is_ota_failed(&device_state) {
                        let _ = self.update_state(
                            &workflow_id,
                            "failed",
                            Some("NG".to_string()),
                            Some(format!(
                                "ota failed. target_device_name={} ota_phase={} ota_detail={}",
                                target_device_name, device_state.ota_phase, device_state.ota_detail
                            )),
                            Some("device reported ota failure".to_string()),
                        );
                        return;
                    }

                    if is_ota_completed(&device_state, &firmware_version) {
                        let _ = self.update_workflow_status(WorkflowStatusDto {
                            workflow_id,
                            workflow_type: "signed-ota".to_string(),
                            target_device_name,
                            state: "completed".to_string(),
                            result: Some("OK".to_string()),
                            error_summary: None,
                            detail: Some(format!(
                                "ota completed. firmware_version={} public_id={}",
                                device_state.firmware_version, device_state.public_id
                            )),
                            started_at,
                            updated_at: chrono::Utc::now().to_rfc3339(),
                            firmware_version,
                        });
                        return;
                    }
                }
                Ok(None) => {}
                Err(read_error) => {
                    let _ = self.update_state(
                        &workflow_id,
                        "failed",
                        Some("NG".to_string()),
                        Some(read_error),
                        Some("device state read failed".to_string()),
                    );
                    return;
                }
            }

            sleep(Duration::from_secs(1)).await;
        }
    }

    fn update_state(
        &self,
        workflow_id: &str,
        state: &str,
        result: Option<String>,
        error_summary: Option<String>,
        detail: Option<String>,
    ) -> Result<(), String> {
        let current_status = self.get_workflow_status(workflow_id)?;
        self.update_workflow_status(WorkflowStatusDto {
            workflow_id: current_status.workflow_id,
            workflow_type: current_status.workflow_type,
            target_device_name: current_status.target_device_name,
            state: state.to_string(),
            result,
            error_summary,
            detail,
            started_at: current_status.started_at,
            updated_at: chrono::Utc::now().to_rfc3339(),
            firmware_version: current_status.firmware_version,
        })
    }

    fn update_workflow_status(&self, workflow_status: WorkflowStatusDto) -> Result<(), String> {
        let mut workflow_map_guard = self
            .workflow_map
            .lock()
            .map_err(|e| format!("update_workflow_status failed. workflow-map lock error={} workflow_id={}", e, workflow_status.workflow_id))?;
        workflow_map_guard.insert(workflow_status.workflow_id.clone(), workflow_status);
        Ok(())
    }
}

async fn publish_ota_command(
    mqtt_receiver_manager: &MqttReceiverManager,
    key_manager: &KeyManager,
    target_device_name: &str,
    manifest_url: &str,
    firmware_url: &str,
    firmware_version: &str,
    sha256: &str,
    timeout_seconds: u64,
) -> Result<(), String> {
    let source_id = get_env_string("LOCAL_SERVER_SOURCE_ID", "local-server-001");
    let plain_payload_text = serde_json::to_string(&serde_json::json!({
        "v": 1,
        "DstID": target_device_name,
        "SrcID": source_id,
        "id": create_message_id(),
        "ts": chrono::Utc::now().to_rfc3339(),
        "op": "call",
        "sub": "otaStart",
        "args": {
            "requestType": "otaStart",
            "manifestUrl": manifest_url,
            "firmwareUrl": firmware_url,
            "firmwareVersion": firmware_version,
            "sha256": sha256,
            "timeoutSeconds": timeout_seconds
        }
    }))
    .map_err(|e| format!("publish_ota_command failed. payload serialize error={}", e))?;
    let encoded_payload_text = encode_outgoing_payload(key_manager, target_device_name, &plain_payload_text)?;
    let topic = format!("esp32lab/call/otaStart/{}", target_device_name);
    mqtt_receiver_manager.publish_message(&topic, &encoded_payload_text, 1).await
}

fn encode_outgoing_payload(key_manager: &KeyManager, target_device_name: &str, plain_payload_text: &str) -> Result<String, String> {
    match resolve_payload_encryption_mode() {
        MqttPayloadEncryptionMode::Plain => Ok(plain_payload_text.to_string()),
        MqttPayloadEncryptionMode::Compat => match key_manager.encrypt_by_k_device(target_device_name, plain_payload_text) {
            Ok((iv_base64, cipher_base64, tag_base64)) => Ok(build_encrypted_envelope_text(iv_base64, cipher_base64, tag_base64)?),
            Err(_) => Ok(plain_payload_text.to_string()),
        },
        MqttPayloadEncryptionMode::Strict => {
            let (iv_base64, cipher_base64, tag_base64) = key_manager.encrypt_by_k_device(target_device_name, plain_payload_text)?;
            build_encrypted_envelope_text(iv_base64, cipher_base64, tag_base64)
        }
    }
}

fn build_encrypted_envelope_text(
    iv_base64: String,
    cipher_base64: String,
    tag_base64: String,
) -> Result<String, String> {
    serde_json::to_string(&serde_json::json!({
        "v": 1,
        "security": {
            "mode": "k-device-a256gcm-v1"
        },
        "enc": {
            "alg": "A256GCM",
            "iv": iv_base64,
            "ct": cipher_base64,
            "tag": tag_base64
        }
    }))
    .map_err(|e| format!("build_encrypted_envelope_text failed. serialize error={}", e))
}

fn resolve_payload_encryption_mode() -> MqttPayloadEncryptionMode {
    match get_env_string("MQTT_PAYLOAD_ENCRYPTION_MODE", "strict").trim().to_lowercase().as_str() {
        "plain" => MqttPayloadEncryptionMode::Plain,
        "compat" => MqttPayloadEncryptionMode::Compat,
        _ => MqttPayloadEncryptionMode::Strict,
    }
}

fn is_ota_failed(device_state: &DeviceStateSummaryDto) -> bool {
    device_state.ota_phase.trim().eq_ignore_ascii_case("error")
}

fn is_ota_completed(device_state: &DeviceStateSummaryDto, expected_firmware_version: &str) -> bool {
    device_state.online_state == "online"
        && !expected_firmware_version.trim().is_empty()
        && device_state.firmware_version.trim() == expected_firmware_version.trim()
}

fn is_verifying_phase(device_state: &DeviceStateSummaryDto) -> bool {
    let normalized_phase = device_state.ota_phase.trim().to_lowercase();
    normalized_phase == "verify"
        || normalized_phase == "done"
        || device_state.ota_detail.to_lowercase().contains("rebooted after ota")
}

fn build_workflow_detail_text(device_state: &DeviceStateSummaryDto) -> String {
    format!(
        "onlineState={} otaPhase={} otaProgressPercent={} otaDetail={}",
        device_state.online_state,
        if device_state.ota_phase.trim().is_empty() {
            "(empty)"
        } else {
            device_state.ota_phase.as_str()
        },
        device_state
            .ota_progress_percent
            .map(|value| value.to_string())
            .unwrap_or_else(|| "(null)".to_string()),
        if device_state.ota_detail.trim().is_empty() {
            "(empty)"
        } else {
            device_state.ota_detail.as_str()
        }
    )
}

fn create_workflow_id() -> String {
    let mut random_generator = rand::rng();
    format!(
        "workflow-signed-ota-{}-{:05}",
        chrono::Utc::now().timestamp_millis(),
        random_generator.random_range(0..100000)
    )
}

fn create_message_id() -> String {
    let mut random_generator = rand::rng();
    let source_id = get_env_string("LOCAL_SERVER_SOURCE_ID", "local-server-001");
    format!(
        "{}-{}-{}",
        source_id,
        chrono::Utc::now().timestamp_millis(),
        random_generator.random_range(0..100000)
    )
}

fn get_env_string(env_name: &str, default_value: &str) -> String {
    env::var(env_name)
        .ok()
        .map(|value| value.trim().to_string())
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| default_value.to_string())
}
