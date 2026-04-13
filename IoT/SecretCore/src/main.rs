// main.rs - SecretCore IPCサーバー本体。
//
// [重要] Named Pipe で LocalServer からの鍵関連要求を受け付ける。
// [厳守] raw k-user をIPC応答へ返さない。
// [厳守] k-user バックアップは export/import ともにパスワード暗号化ファイルで取り扱う。
// [厳守] LocalServer 起動時は IPC メッセージを AES-256-GCM + requestId + timestamp で保護する。
// [旧仕様] 環境変数未指定の手動起動時のみ、互換の平文IPCを許可する。
// 変更日: 2026-03-15 IPC 保護を追加。理由: 003-0014 対応のため。
mod dpapi;
mod generic_workflow;
mod key_manager;
mod mqtt_transport;
mod ota_workflow;
mod pairing_workflow;

use aes_gcm::aead::{AeadInPlace, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use key_manager::KeyManager;
use rand::Rng;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::env;
use std::io;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::windows::named_pipe::ServerOptions;

const DEFAULT_PIPE_NAME: &str = r"\\.\pipe\iot-secret-core-ipc";
const IPC_SCHEMA_VERSION: u32 = 1;
const IPC_ALLOWED_SKEW_MS: i64 = 30_000;
const IPC_REPLAY_CACHE_TTL_MS: i64 = 120_000;

#[derive(Debug, Serialize, Deserialize)]
struct IpcRequest {
    command: String,
    payload: Option<serde_json::Value>,
}

#[derive(Debug, Serialize, Deserialize)]
struct IpcResponse {
    status: String,
    data: Option<serde_json::Value>,
    error: Option<String>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct IpcProtectedRequestEnvelope {
    version: u32,
    request_id: String,
    timestamp: i64,
    nonce_base64: String,
    cipher_base64: String,
    tag_base64: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct IpcProtectedResponseEnvelope {
    version: u32,
    response_to_request_id: String,
    timestamp: i64,
    nonce_base64: String,
    cipher_base64: String,
    tag_base64: String,
}

struct IpcSecurityContext {
    pipe_name: String,
    session_key: Option<[u8; 32]>,
    replay_cache: Mutex<HashMap<String, i64>>,
}

/// SecretCore の IPC サーバー本体を起動する。
///
/// 起動時に Pipe 名、IPC 保護鍵、MQTT 受信管理、workflow 管理器を初期化し、
/// 以後は接続ごとに非同期タスクへ処理を委譲する。
#[tokio::main]
async fn main() -> io::Result<()> {
    let ipc_security_context = Arc::new(IpcSecurityContext {
        pipe_name: env::var("SECRET_CORE_PIPE_NAME").unwrap_or_else(|_| DEFAULT_PIPE_NAME.to_string()),
        session_key: load_ipc_session_key_from_env()?,
        replay_cache: Mutex::new(HashMap::new()),
    });
    let mqtt_receiver_manager = Arc::new(mqtt_transport::MqttReceiverManager::new());
    let ota_workflow_manager = Arc::new(ota_workflow::OtaWorkflowManager::new());
    let pairing_workflow_manager = Arc::new(pairing_workflow::PairingWorkflowManager::new());
    let production_workflow_manager = Arc::new(generic_workflow::ProductionWorkflowManager::new());
    println!("SecretCore started. Listening on {}", ipc_security_context.pipe_name);
    if ipc_security_context.session_key.is_some() {
        println!("SecretCore IPC protection mode=secure");
    } else {
        println!("SecretCore IPC protection mode=legacy-compatible");
    }

    // KeyManager 初期化
    let key_mgr = match KeyManager::new() {
        Ok(km) => Arc::new(km),
        Err(e) => {
            eprintln!("Failed to initialize KeyManager: {}", e);
            return Err(io::Error::new(io::ErrorKind::Other, "KeyManager Init Error"));
        }
    };

    loop {
        let server = match ServerOptions::new()
            .first_pipe_instance(true)
            .create(&ipc_security_context.pipe_name)
        {
            Ok(server) => server,
            Err(e) if e.kind() == io::ErrorKind::AddrInUse || e.raw_os_error() == Some(5) => {
                // 既に存在するかアクセス拒否の場合は、first_pipe_instance(false) で作成
                ServerOptions::new()
                    .first_pipe_instance(false)
                    .create(&ipc_security_context.pipe_name)?
            }
            Err(e) => return Err(e),
        };

        // クライアント接続待ち
        if let Err(e) = server.connect().await {
            eprintln!("Failed to connect: {}", e);
            continue;
        }

        let key_mgr_clone = Arc::clone(&key_mgr);
        let ipc_security_context_clone = Arc::clone(&ipc_security_context);
        let mqtt_receiver_manager_clone = Arc::clone(&mqtt_receiver_manager);
        let ota_workflow_manager_clone = Arc::clone(&ota_workflow_manager);
        let pairing_workflow_manager_clone = Arc::clone(&pairing_workflow_manager);
        let production_workflow_manager_clone = Arc::clone(&production_workflow_manager);
        // 各接続を別タスクで処理
        tokio::spawn(async move {
            if let Err(e) = handle_client(
                server,
                key_mgr_clone,
                ipc_security_context_clone,
                mqtt_receiver_manager_clone,
                ota_workflow_manager_clone,
                pairing_workflow_manager_clone,
                production_workflow_manager_clone,
            )
            .await
            {
                eprintln!("Client handling error: {}", e);
            }
        });
    }
}

/// 1 本の IPC 接続から要求を受け取り、コマンド実行と応答返却を繰り返す。
///
/// すべての high-risk workflow はこの入口で受理され、各 manager へ処理を委譲する。
async fn handle_client(
    mut stream: tokio::net::windows::named_pipe::NamedPipeServer,
    key_mgr: Arc<KeyManager>,
    ipc_security_context: Arc<IpcSecurityContext>,
    mqtt_receiver_manager: Arc<mqtt_transport::MqttReceiverManager>,
    ota_workflow_manager: Arc<ota_workflow::OtaWorkflowManager>,
    pairing_workflow_manager: Arc<pairing_workflow::PairingWorkflowManager>,
    production_workflow_manager: Arc<generic_workflow::ProductionWorkflowManager>,
) -> io::Result<()> {
    let mut buf = [0u8; 8192];
    loop {
        let n = stream.read(&mut buf).await?;
        if n == 0 {
            break;
        }

        let raw_request_text = String::from_utf8_lossy(&buf[0..n]);
        let (req_str, request_id_option) = if let Some(session_key) = ipc_security_context.session_key {
            match decrypt_ipc_request_text(&raw_request_text, &session_key, &ipc_security_context) {
                Ok((plain_text, request_id)) => (plain_text, Some(request_id)),
                Err(e) => {
                    let error_response = IpcResponse {
                        status: "error".to_string(),
                        data: None,
                        error: Some(format!("IPC protection error: {}", e)),
                    };
                    if let Some(response_request_id) = extract_request_id_from_envelope(&raw_request_text) {
                        let protected_error = encrypt_ipc_response_text(
                            &serde_json::to_string(&error_response)?,
                            &response_request_id,
                            &session_key,
                        )?;
                        stream.write_all(protected_error.as_bytes()).await?;
                    } else {
                        stream.write_all(serde_json::to_string(&error_response)?.as_bytes()).await?;
                    }
                    continue;
                }
            }
        } else {
            (raw_request_text.to_string(), None)
        };
        
        let response = match serde_json::from_str::<IpcRequest>(&req_str) {
            Ok(req) => {
                match req.command.as_str() {
                    "health" => IpcResponse {
                        status: "ok".to_string(),
                        data: Some(serde_json::json!({ "version": "0.1.0" })),
                        error: None,
                    },
                    "echo" => IpcResponse {
                        status: "ok".to_string(),
                        data: req.payload,
                        error: None,
                    },
                    "issue_k_user" => {
                        match key_mgr.issue_new_k_user() {
                            Ok(key_fingerprint) => {
                                let source = key_mgr.get_k_user_source();
                                IpcResponse {
                                    status: "ok".to_string(),
                                    data: Some(serde_json::json!({
                                        "isIssued": true,
                                        "source": source,
                                        "keyFingerprint": key_fingerprint
                                    })),
                                    error: None,
                                }
                            }
                            Err(e) => IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some(format!("issue_k_user failed. {}", e)),
                            },
                        }
                    },
                    "get_k_user_status" => {
                        let source = key_mgr.get_k_user_source();
                        let key_fingerprint = key_mgr.get_k_user_fingerprint();
                        IpcResponse {
                            status: "ok".to_string(),
                            data: Some(serde_json::json!({
                                "isIssued": true,
                                "issuedAt": "",
                                "keyFingerprint": key_fingerprint,
                                "deviceKeyCount": 0,
                                "source": source
                            })),
                            error: None,
                        }
                    },
                    "import_k_user" => {
                        if let Some(p) = req.payload {
                            if let Some(k_user_b64) = p.get("kUserBase64").and_then(|v| v.as_str()) {
                                use base64::prelude::*;
                                match BASE64_STANDARD.decode(k_user_b64) {
                                    Ok(k_user_bytes) => {
                                        match key_mgr.import_k_user(&k_user_bytes) {
                                            Ok(()) => {
                                                let source = key_mgr.get_k_user_source();
                                                IpcResponse {
                                                    status: "ok".to_string(),
                                                    data: Some(serde_json::json!({ "imported": true, "source": source })),
                                                    error: None,
                                                }
                                            },
                                            Err(e) => IpcResponse { status: "error".to_string(), data: None, error: Some(e) }
                                        }
                                    },
                                    Err(e) => IpcResponse { status: "error".to_string(), data: None, error: Some(format!("Base64 decode error: {}", e)) }
                                }
                            } else {
                                IpcResponse { status: "error".to_string(), data: None, error: Some("Missing kUserBase64".into()) }
                            }
                        } else {
                            IpcResponse { status: "error".to_string(), data: None, error: Some("Missing payload".into()) }
                        }
                    },
                    "export_k_user" => {
                        if let Some(p) = req.payload {
                            let backup_password = p.get("backupPassword").and_then(|v| v.as_str()).unwrap_or("");
                            let backup_file_path = p.get("backupFilePath").and_then(|v| v.as_str());
                            match key_mgr.export_k_user_backup_file(backup_password, backup_file_path) {
                                Ok((output_path, key_fingerprint)) => {
                                    IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!({
                                            "exported": true,
                                            "backupFilePath": output_path,
                                            "keyFingerprint": key_fingerprint,
                                            "source": key_mgr.get_k_user_source(),
                                            "format": "k-user-backup-v1"
                                        })),
                                        error: None,
                                    }
                                }
                                Err(e) => IpcResponse {
                                    status: "error".to_string(),
                                    data: None,
                                    error: Some(format!("export_k_user failed. {}", e)),
                                },
                            }
                        } else {
                            IpcResponse { status: "error".to_string(), data: None, error: Some("export_k_user failed. Missing payload".into()) }
                        }
                    },
                    "import_k_user_backup" => {
                        if let Some(p) = req.payload {
                            let backup_password = p.get("backupPassword").and_then(|v| v.as_str()).unwrap_or("");
                            let backup_file_path = p.get("backupFilePath").and_then(|v| v.as_str());
                            match key_mgr.import_k_user_backup_file(backup_password, backup_file_path) {
                                Ok(key_fingerprint) => {
                                    IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!({
                                            "imported": true,
                                            "keyFingerprint": key_fingerprint,
                                            "source": key_mgr.get_k_user_source()
                                        })),
                                        error: None,
                                    }
                                }
                                Err(e) => IpcResponse {
                                    status: "error".to_string(),
                                    data: None,
                                    error: Some(format!("import_k_user_backup failed. {}", e)),
                                },
                            }
                        } else {
                            IpcResponse { status: "error".to_string(), data: None, error: Some("import_k_user_backup failed. Missing payload".into()) }
                        }
                    },
                    "get_k_device" => {
                        if let Some(p) = req.payload {
                            if let Some(device) = p.get("targetDeviceName").and_then(|v| v.as_str()) {
                                let k_device = key_mgr.get_k_device(device);
                                use base64::prelude::*;
                                let k_device_b64 = BASE64_STANDARD.encode(&k_device);
                                IpcResponse {
                                    status: "ok".to_string(),
                                    data: Some(serde_json::json!({
                                        "targetDeviceName": device,
                                        "keyDeviceBase64": k_device_b64
                                    })),
                                    error: None,
                                }
                            } else {
                                IpcResponse { status: "error".to_string(), data: None, error: Some("Missing targetDeviceName".into()) }
                            }
                        } else {
                            IpcResponse { status: "error".to_string(), data: None, error: Some("Missing payload".into()) }
                        }
                    },
                    "mqtt_publish" => {
                        if let Some(p) = req.payload {
                            let topic = p.get("topic").and_then(|v| v.as_str()).unwrap_or("");
                            let payload = p.get("payload").and_then(|v| v.as_str()).unwrap_or("");
                            let qos = p.get("qos").and_then(|v| v.as_u64()).unwrap_or(1) as u8;
                            if topic.is_empty() {
                                IpcResponse {
                                    status: "error".to_string(),
                                    data: None,
                                    error: Some("mqtt_publish failed. Missing topic".into()),
                                }
                            } else {
                                match mqtt_receiver_manager.publish_message(topic, payload, qos).await {
                                    Ok(()) => IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!({
                                            "published": true,
                                            "topic": topic,
                                            "qos": qos,
                                            "payloadLength": payload.len()
                                        })),
                                        error: None,
                                    },
                                    Err(e) => IpcResponse {
                                        status: "error".to_string(),
                                        data: None,
                                        error: Some(format!("mqtt_publish failed. {}", e)),
                                    },
                                }
                            }
                        } else {
                            IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some("mqtt_publish failed. Missing payload".into()),
                            }
                        }
                    },
                    "mqtt_start_receiver" => {
                        match mqtt_receiver_manager.start_if_needed(Arc::clone(&key_mgr)) {
                            Ok(is_started_now) => IpcResponse {
                                status: "ok".to_string(),
                                data: Some(serde_json::json!({
                                    "started": is_started_now,
                                    "alreadyRunning": !is_started_now
                                })),
                                error: None,
                            },
                            Err(e) => IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some(format!("mqtt_start_receiver failed. {}", e)),
                            },
                        }
                    },
                    "mqtt_drain_events" => {
                        match mqtt_receiver_manager.drain_events() {
                            Ok(events) => IpcResponse {
                                status: "ok".to_string(),
                                data: Some(serde_json::json!({
                                    "events": events
                                })),
                                error: None,
                            },
                            Err(e) => IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some(format!("mqtt_drain_events failed. {}", e)),
                            },
                        }
                    },
                    "wait_for_status_recovery" => {
                        if let Some(p) = req.payload {
                            let target_device_name = p.get("targetDeviceName").and_then(|v| v.as_str()).unwrap_or("");
                            let timeout_seconds = p.get("timeoutSeconds").and_then(|v| v.as_u64()).unwrap_or(120);
                            match mqtt_receiver_manager
                                .wait_for_status_recovery(target_device_name, timeout_seconds)
                                .await
                            {
                                Ok(status_recovery_result) => IpcResponse {
                                    status: "ok".to_string(),
                                    data: Some(serde_json::json!(status_recovery_result)),
                                    error: None,
                                },
                                Err(e) => IpcResponse {
                                    status: "error".to_string(),
                                    data: None,
                                    error: Some(format!("wait_for_status_recovery failed. {}", e)),
                                },
                            }
                        } else {
                            IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some("wait_for_status_recovery failed. Missing payload".into()),
                            }
                        }
                    },
                    "run_signed_ota_command" => {
                        if let Some(p) = req.payload {
                            let target_device_name = p.get("targetDeviceName").and_then(|v| v.as_str()).unwrap_or("").trim().to_string();
                            let manifest_url = p.get("manifestUrl").and_then(|v| v.as_str()).unwrap_or("").trim().to_string();
                            let firmware_url = p.get("firmwareUrl").and_then(|v| v.as_str()).unwrap_or("").trim().to_string();
                            let firmware_version = p.get("firmwareVersion").and_then(|v| v.as_str()).unwrap_or("").trim().to_string();
                            let sha256 = p.get("sha256").and_then(|v| v.as_str()).unwrap_or("").trim().to_string();
                            let timeout_seconds = p.get("timeoutSeconds").and_then(|v| v.as_u64()).unwrap_or(120);
                            match ota_workflow_manager.start_signed_ota_command(
                                Arc::clone(&mqtt_receiver_manager),
                                Arc::clone(&key_mgr),
                                target_device_name,
                                manifest_url,
                                firmware_url,
                                firmware_version,
                                sha256,
                                timeout_seconds,
                            ) {
                                Ok(workflow_status) => IpcResponse {
                                    status: "ok".to_string(),
                                    data: Some(serde_json::json!(workflow_status)),
                                    error: None,
                                },
                                Err(e) => IpcResponse {
                                    status: "error".to_string(),
                                    data: None,
                                    error: Some(format!("run_signed_ota_command failed. {}", e)),
                                },
                            }
                        } else {
                            IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some("run_signed_ota_command failed. Missing payload".into()),
                            }
                        }
                    },
                    "run_pairing_session" => {
                        if let Some(p) = req.payload {
                            match serde_json::from_value::<pairing_workflow::PairingWorkflowStartRequest>(p) {
                                Ok(pairing_request) => match pairing_workflow_manager.start_pairing_session(&key_mgr, pairing_request) {
                                    Ok(workflow_status) => IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!(workflow_status)),
                                        error: None,
                                    },
                                    Err(e) => IpcResponse {
                                        status: "error".to_string(),
                                        data: None,
                                        error: Some(format!("run_pairing_session failed. {}", e)),
                                    },
                                },
                                Err(e) => IpcResponse {
                                    status: "error".to_string(),
                                    data: None,
                                    error: Some(format!("run_pairing_session failed. invalid payload. {}", e)),
                                },
                            }
                        } else {
                            IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some("run_pairing_session failed. Missing payload".into()),
                            }
                        }
                    },
                    "run_key_rotation_session" => {
                        if let Some(p) = req.payload {
                            match serde_json::from_value::<pairing_workflow::PairingWorkflowStartRequest>(p) {
                                Ok(key_rotation_request) => match pairing_workflow_manager.start_key_rotation_session(&key_mgr, key_rotation_request) {
                                    Ok(workflow_status) => IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!(workflow_status)),
                                        error: None,
                                    },
                                    Err(e) => IpcResponse {
                                        status: "error".to_string(),
                                        data: None,
                                        error: Some(format!("run_key_rotation_session failed. {}", e)),
                                    },
                                },
                                Err(e) => IpcResponse {
                                    status: "error".to_string(),
                                    data: None,
                                    error: Some(format!("run_key_rotation_session failed. invalid payload. {}", e)),
                                },
                            }
                        } else {
                            IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some("run_key_rotation_session failed. Missing payload".into()),
                            }
                        }
                    },
                    "run_production_secure_flow" => {
                        if let Some(p) = req.payload {
                            match serde_json::from_value::<generic_workflow::ProductionWorkflowStartRequest>(p) {
                                Ok(production_request) => match production_workflow_manager.start_production_secure_flow(production_request) {
                                    Ok(workflow_status) => IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!(workflow_status)),
                                        error: None,
                                    },
                                    Err(e) => IpcResponse {
                                        status: "error".to_string(),
                                        data: None,
                                        error: Some(format!("run_production_secure_flow failed. {}", e)),
                                    },
                                },
                                Err(e) => IpcResponse {
                                    status: "error".to_string(),
                                    data: None,
                                    error: Some(format!("run_production_secure_flow failed. invalid payload. {}", e)),
                                },
                            }
                        } else {
                            IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some("run_production_secure_flow failed. Missing payload".into()),
                            }
                        }
                    },
                    "get_workflow_status" => {
                        if let Some(p) = req.payload {
                            let workflow_id = p.get("workflowId").and_then(|v| v.as_str()).unwrap_or("").trim();
                            match ota_workflow_manager.get_workflow_status(workflow_id) {
                                Ok(workflow_status) => IpcResponse {
                                    status: "ok".to_string(),
                                    data: Some(serde_json::json!(workflow_status)),
                                    error: None,
                                },
                                Err(ota_error) => match pairing_workflow_manager.get_workflow_status(workflow_id) {
                                    Ok(workflow_status) => IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!(workflow_status)),
                                        error: None,
                                    },
                                    Err(pairing_error) => match production_workflow_manager.get_workflow_status(workflow_id) {
                                        Ok(workflow_status) => IpcResponse {
                                            status: "ok".to_string(),
                                            data: Some(serde_json::json!(workflow_status)),
                                            error: None,
                                        },
                                        Err(production_error) => IpcResponse {
                                            status: "error".to_string(),
                                            data: None,
                                            error: Some(format!(
                                                "get_workflow_status failed. ota_error={} pairing_error={} production_error={}",
                                                ota_error, pairing_error, production_error
                                            )),
                                        },
                                    },
                                },
                            }
                        } else {
                            IpcResponse {
                                status: "error".to_string(),
                                data: None,
                                error: Some("get_workflow_status failed. Missing payload".into()),
                            }
                        }
                    },
                    "encrypt" => {
                        if let Some(p) = req.payload {
                            if let (Some(device), Some(text)) = (p.get("targetDeviceName").and_then(|v| v.as_str()), p.get("plainText").and_then(|v| v.as_str())) {
                                match key_mgr.encrypt_by_k_device(device, text) {
                                    Ok((iv, ct, tag)) => IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!({
                                            "alg": "A256GCM",
                                            "ivBase64": iv,
                                            "cipherBase64": ct,
                                            "tagBase64": tag
                                        })),
                                        error: None,
                                    },
                                    Err(e) => IpcResponse {
                                        status: "error".to_string(),
                                        data: None,
                                        error: Some(e),
                                    }
                                }
                            } else {
                                IpcResponse { status: "error".to_string(), data: None, error: Some("Missing fields".into()) }
                            }
                        } else {
                            IpcResponse { status: "error".to_string(), data: None, error: Some("Missing payload".into()) }
                        }
                    },
                    "decrypt" => {
                        if let Some(p) = req.payload {
                            let device = p.get("targetDeviceName").and_then(|v| v.as_str());
                            let enc = p.get("encrypted");
                            if let (Some(device), Some(enc)) = (device, enc) {
                                let iv = enc.get("ivBase64").and_then(|v| v.as_str()).unwrap_or("");
                                let ct = enc.get("cipherBase64").and_then(|v| v.as_str()).unwrap_or("");
                                let tag = enc.get("tagBase64").and_then(|v| v.as_str()).unwrap_or("");
                                match key_mgr.decrypt_by_k_device(device, iv, ct, tag) {
                                    Ok(plain) => IpcResponse {
                                        status: "ok".to_string(),
                                        data: Some(serde_json::json!({ "plainText": plain })),
                                        error: None,
                                    },
                                    Err(e) => IpcResponse {
                                        status: "error".to_string(),
                                        data: None,
                                        error: Some(e),
                                    }
                                }
                            } else {
                                IpcResponse { status: "error".to_string(), data: None, error: Some("Missing fields".into()) }
                            }
                        } else {
                            IpcResponse { status: "error".to_string(), data: None, error: Some("Missing payload".into()) }
                        }
                    },
                    _ => IpcResponse {
                        status: "error".to_string(),
                        data: None,
                        error: Some(format!("Unknown command: {}", req.command)),
                    },
                }
            }
            Err(e) => IpcResponse {
                status: "error".to_string(),
                data: None,
                error: Some(format!("Invalid JSON: {}", e)),
            },
        };

        let res_str = serde_json::to_string(&response)?;
        if let (Some(session_key), Some(request_id)) = (ipc_security_context.session_key, request_id_option) {
            let protected_response = encrypt_ipc_response_text(&res_str, &request_id, &session_key)?;
            stream.write_all(protected_response.as_bytes()).await?;
        } else {
            stream.write_all(res_str.as_bytes()).await?;
        }
    }
    Ok(())
}

fn load_ipc_session_key_from_env() -> io::Result<Option<[u8; 32]>> {
    let session_key_b64 = match env::var("SECRET_CORE_IPC_SESSION_KEY_B64") {
        Ok(v) => v,
        Err(_) => return Ok(None),
    };
    use base64::prelude::*;
    let session_key_vec = BASE64_STANDARD.decode(session_key_b64).map_err(|e| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("SECRET_CORE_IPC_SESSION_KEY_B64 decode failed: {}", e),
        )
    })?;
    if session_key_vec.len() != 32 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!(
                "SECRET_CORE_IPC_SESSION_KEY_B64 length invalid. expected=32 actual={}",
                session_key_vec.len()
            ),
        ));
    }
    let mut session_key = [0u8; 32];
    session_key.copy_from_slice(&session_key_vec);
    Ok(Some(session_key))
}

fn get_current_time_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|v| v.as_millis() as i64)
        .unwrap_or(0)
}

fn decrypt_ipc_request_text(
    raw_request_text: &str,
    session_key: &[u8; 32],
    ipc_security_context: &IpcSecurityContext,
) -> Result<(String, String), String> {
    let envelope: IpcProtectedRequestEnvelope = serde_json::from_str(raw_request_text)
        .map_err(|e| format!("invalid protected request envelope: {}", e))?;
    if envelope.version != IPC_SCHEMA_VERSION {
        return Err(format!(
            "unsupported protected request version={}",
            envelope.version
        ));
    }
    let now_ms = get_current_time_ms();
    if (now_ms - envelope.timestamp).abs() > IPC_ALLOWED_SKEW_MS {
        return Err(format!(
            "timestamp skew too large. requestId={} timestamp={} now={}",
            envelope.request_id, envelope.timestamp, now_ms
        ));
    }
    {
        let mut replay_cache = ipc_security_context.replay_cache.lock().unwrap();
        replay_cache.retain(|_, stored_at| (now_ms - *stored_at).abs() <= IPC_REPLAY_CACHE_TTL_MS);
        if replay_cache.contains_key(&envelope.request_id) {
            return Err(format!("replay detected. requestId={}", envelope.request_id));
        }
        replay_cache.insert(envelope.request_id.clone(), now_ms);
    }
    use base64::prelude::*;
    let nonce_vec = BASE64_STANDARD
        .decode(&envelope.nonce_base64)
        .map_err(|e| format!("nonce decode failed: {}", e))?;
    if nonce_vec.len() != 12 {
        return Err(format!("nonce length invalid. actual={}", nonce_vec.len()));
    }
    let mut cipher_vec = BASE64_STANDARD
        .decode(&envelope.cipher_base64)
        .map_err(|e| format!("cipher decode failed: {}", e))?;
    let tag_vec = BASE64_STANDARD
        .decode(&envelope.tag_base64)
        .map_err(|e| format!("tag decode failed: {}", e))?;
    let cipher = Aes256Gcm::new(session_key.as_ref().into());
    let nonce = Nonce::from_slice(&nonce_vec);
    let aad_text = create_request_aad_text(&envelope.request_id, envelope.timestamp);
    cipher
        .decrypt_in_place_detached(nonce, aad_text.as_bytes(), &mut cipher_vec, tag_vec.as_slice().into())
        .map_err(|e| format!("request decrypt failed: {:?}", e))?;
    let plain_text = String::from_utf8(cipher_vec).map_err(|e| format!("utf8 decode failed: {}", e))?;
    Ok((plain_text, envelope.request_id))
}

fn encrypt_ipc_response_text(
    response_plain_text: &str,
    request_id: &str,
    session_key: &[u8; 32],
) -> io::Result<String> {
    let timestamp = get_current_time_ms();
    let mut nonce_bytes = [0u8; 12];
    rand::rng().fill_bytes(&mut nonce_bytes);
    let cipher = Aes256Gcm::new(session_key.as_ref().into());
    let nonce = Nonce::from_slice(&nonce_bytes);
    let aad_text = create_response_aad_text(request_id, timestamp);
    let mut buffer = response_plain_text.as_bytes().to_vec();
    let tag = cipher
        .encrypt_in_place_detached(nonce, aad_text.as_bytes(), &mut buffer)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, format!("response encrypt failed: {:?}", e)))?;
    use base64::prelude::*;
    let envelope = IpcProtectedResponseEnvelope {
        version: IPC_SCHEMA_VERSION,
        response_to_request_id: request_id.to_string(),
        timestamp,
        nonce_base64: BASE64_STANDARD.encode(nonce_bytes),
        cipher_base64: BASE64_STANDARD.encode(buffer),
        tag_base64: BASE64_STANDARD.encode(tag),
    };
    serde_json::to_string(&envelope)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, format!("response serialize failed: {}", e)))
}

fn extract_request_id_from_envelope(raw_request_text: &str) -> Option<String> {
    serde_json::from_str::<IpcProtectedRequestEnvelope>(raw_request_text)
        .ok()
        .map(|v| v.request_id)
}

fn create_request_aad_text(request_id: &str, timestamp: i64) -> String {
    format!("v=1|rid={}|ts={}", request_id, timestamp)
}

fn create_response_aad_text(response_to_request_id: &str, timestamp: i64) -> String {
    format!("v=1|rrid={}|ts={}", response_to_request_id, timestamp)
}
