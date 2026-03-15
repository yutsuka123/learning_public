// mqtt_transport.rs - SecretCore の MQTT publish / subscribe 補助。
//
// [重要] Stage1 は command publish、Stage2 は notice subscribe を SecretCore 側へ移す。
// [厳守] TLS 利用時は CA 検証を有効にし、検証スキップを行わない。
// [厳守] 受信メッセージの復号・正規化・deviceState統合・offline timeout判定を Rust 側で一元化する。
// [重要] 受信常駐ループは再接続可能とし、接続状態変化をイベントとして TS 側へ通知する。
// 変更日: 2026-03-15 Stage2 subscribe/受信イベント bridge を追加。理由: 003-0012 の通信Rust化を次段階へ進めるため。
// 変更日: 2026-03-15 Stage5 offline timeout判定を追加。理由: `DeviceRegistry` の状態遷移責務をさらに Rust 側へ移すため。

use crate::key_manager::KeyManager;
use rumqttc::{AsyncClient, Event, Incoming, MqttOptions, Outgoing, QoS, TlsConfiguration, Transport};
use serde::Serialize;
use serde_json::Value;
use std::collections::{HashMap, VecDeque};
use std::env;
use std::fs;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::time::{Instant, sleep};

struct MqttRuntimeConfig {
    broker_host_name: String,
    broker_port: u16,
    protocol: String,
    username: String,
    password: String,
    ca_path: PathBuf,
    connect_timeout_ms: u64,
    source_id: String,
    status_offline_timeout_seconds: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum MqttPayloadEncryptionMode {
    Plain,
    Compat,
    Strict,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StatusMessageDto {
    topic: String,
    src_id: String,
    dst_id: String,
    message_id: String,
    public_id: String,
    config_version: String,
    mac_addr: String,
    ip_address: String,
    wifi_ssid: String,
    firmware_version: String,
    firmware_written_at: String,
    running_partition: String,
    boot_partition: String,
    next_update_partition: String,
    online_state: String,
    status_sub: String,
    detail: String,
    received_at: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TrhMessageDto {
    topic: String,
    src_id: String,
    dst_id: String,
    message_id: String,
    result: String,
    detail: String,
    sensor_id: String,
    sensor_address: String,
    temperature_c: Option<f64>,
    humidity_rh: Option<f64>,
    pressure_hpa: Option<f64>,
    received_at: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct OtaProgressMessageDto {
    topic: String,
    src_id: String,
    dst_id: String,
    message_id: String,
    firmware_version: String,
    progress_percent: Option<f64>,
    phase: String,
    detail: String,
    received_at: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SecureEchoMessageDto {
    topic: String,
    src_id: String,
    request_id: String,
    iv_base64: String,
    cipher_base64: String,
    tag_base64: String,
    received_at: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceStateDto {
    device_name: String,
    public_id: String,
    config_version: String,
    src_id: String,
    dst_id: String,
    mac_addr: String,
    ip_address: String,
    wifi_ssid: String,
    firmware_version: String,
    firmware_written_at: String,
    running_partition: String,
    boot_partition: String,
    next_update_partition: String,
    ota_progress_percent: Option<f64>,
    ota_phase: String,
    ota_detail: String,
    ota_updated_at: String,
    online_state: String,
    detail: String,
    last_message_id: String,
    last_status_sub: String,
    last_seen_at: String,
    status_topic: String,
    temperature_c: Option<f64>,
    humidity_rh: Option<f64>,
    pressure_hpa: Option<f64>,
    environment_sensor_id: String,
    environment_sensor_address: String,
    environment_updated_at: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StatusRecoveryResultDto {
    device_name: String,
    public_id: String,
    firmware_version: String,
    config_version: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceStateSummaryDto {
    pub device_name: String,
    pub public_id: String,
    pub config_version: String,
    pub firmware_version: String,
    pub online_state: String,
    pub ota_phase: String,
    pub ota_detail: String,
    pub ota_progress_percent: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MqttInboundEventDto {
    kind: String,
    detail: Option<String>,
    received_at: String,
    status: Option<StatusMessageDto>,
    trh: Option<TrhMessageDto>,
    ota_progress: Option<OtaProgressMessageDto>,
    secure_echo: Option<SecureEchoMessageDto>,
    device_state: Option<DeviceStateDto>,
}

pub struct MqttReceiverManager {
    is_started: Mutex<bool>,
    event_queue: Mutex<VecDeque<MqttInboundEventDto>>,
    device_state_map: Mutex<HashMap<String, DeviceStateDto>>,
    max_queue_len: usize,
}

impl MqttReceiverManager {
    pub fn new() -> Self {
        Self {
            is_started: Mutex::new(false),
            event_queue: Mutex::new(VecDeque::new()),
            device_state_map: Mutex::new(HashMap::new()),
            max_queue_len: 1024,
        }
    }

    pub fn start_if_needed(self: &Arc<Self>, key_mgr: Arc<KeyManager>) -> Result<bool, String> {
        let mut is_started_guard = self
            .is_started
            .lock()
            .map_err(|e| format!("start_if_needed failed. start-state lock error={}", e))?;
        if *is_started_guard {
            return Ok(false);
        }
        *is_started_guard = true;
        let manager = Arc::clone(self);
        tokio::spawn(async move {
            manager.run_receiver_loop(key_mgr).await;
        });
        Ok(true)
    }

    pub fn drain_events(&self) -> Result<Vec<MqttInboundEventDto>, String> {
        let timeout_events = self.collect_offline_timeout_events()?;
        let mut queue_guard = self
            .event_queue
            .lock()
            .map_err(|e| format!("drain_events failed. queue lock error={}", e))?;
        for timeout_event in timeout_events {
            queue_guard.push_back(timeout_event);
        }
        Ok(queue_guard.drain(..).collect())
    }

    pub async fn wait_for_status_recovery(
        &self,
        target_device_name: &str,
        timeout_seconds: u64,
    ) -> Result<StatusRecoveryResultDto, String> {
        let safe_timeout_seconds = timeout_seconds.max(1);
        let deadline_instant = Instant::now() + Duration::from_secs(safe_timeout_seconds);
        loop {
            {
                let state_map_guard = self.device_state_map.lock().map_err(|e| {
                    format!(
                        "wait_for_status_recovery failed. state-map lock error={} target_device_name={} timeout_seconds={}",
                        e, target_device_name, safe_timeout_seconds
                    )
                })?;
                let found_device = if target_device_name.trim().is_empty() {
                    state_map_guard.values().find(|device_state| device_state.online_state == "online")
                } else {
                    state_map_guard
                        .get(target_device_name)
                        .filter(|device_state| device_state.online_state == "online")
                };
                if let Some(device_state) = found_device {
                    return Ok(StatusRecoveryResultDto {
                        device_name: device_state.device_name.clone(),
                        public_id: device_state.public_id.clone(),
                        firmware_version: device_state.firmware_version.clone(),
                        config_version: device_state.config_version.clone(),
                    });
                }
            }

            if Instant::now() >= deadline_instant {
                return Err(format!(
                    "wait_for_status_recovery failed. timeout seconds={} target_device_name={}",
                    safe_timeout_seconds,
                    if target_device_name.trim().is_empty() {
                        "(empty)"
                    } else {
                        target_device_name
                    }
                ));
            }
            sleep(Duration::from_secs(1)).await;
        }
    }

    pub fn get_device_state_summary(&self, target_device_name: &str) -> Result<Option<DeviceStateSummaryDto>, String> {
        let state_map_guard = self.device_state_map.lock().map_err(|e| {
            format!(
                "get_device_state_summary failed. state-map lock error={} target_device_name={}",
                e, target_device_name
            )
        })?;
        let Some(device_state) = state_map_guard.get(target_device_name) else {
            return Ok(None);
        };
        Ok(Some(DeviceStateSummaryDto {
            device_name: device_state.device_name.clone(),
            public_id: device_state.public_id.clone(),
            config_version: device_state.config_version.clone(),
            firmware_version: device_state.firmware_version.clone(),
            online_state: device_state.online_state.clone(),
            ota_phase: device_state.ota_phase.clone(),
            ota_detail: device_state.ota_detail.clone(),
            ota_progress_percent: device_state.ota_progress_percent,
        }))
    }

    async fn run_receiver_loop(self: Arc<Self>, key_mgr: Arc<KeyManager>) {
        loop {
            let runtime_config = match load_runtime_config() {
                Ok(runtime_config) => runtime_config,
                Err(e) => {
                    self.push_error_event(format!("run_receiver_loop config error. detail={}", e));
                    sleep(Duration::from_secs(3)).await;
                    continue;
                }
            };
            let mqtt_options = match create_mqtt_options(&runtime_config, "rx") {
                Ok(mqtt_options) => mqtt_options,
                Err(e) => {
                    self.push_error_event(format!("run_receiver_loop options error. detail={}", e));
                    sleep(Duration::from_secs(3)).await;
                    continue;
                }
            };
            let (client, mut eventloop) = AsyncClient::new(mqtt_options, 100);
            let mut was_connected = false;

            loop {
                match eventloop.poll().await {
                    Ok(Event::Incoming(Incoming::ConnAck(_))) => {
                        was_connected = true;
                        self.push_simple_event("connected", None);
                        if let Err(e) = subscribe_notice_topics(&client).await {
                            self.push_error_event(format!("subscribe_notice_topics failed. detail={}", e));
                        }
                    }
                    Ok(Event::Incoming(Incoming::Publish(publish_packet))) => {
                        let payload_text = String::from_utf8_lossy(publish_packet.payload.as_ref()).to_string();
                        match normalize_inbound_event(&key_mgr, &publish_packet.topic, &payload_text) {
                            Ok(event_dto) => match self.attach_device_state(event_dto) {
                                Ok(enriched_event) => self.push_event(enriched_event),
                                Err(e) => self.push_error_event(format!(
                                    "attach_device_state failed. topic={} detail={}",
                                    publish_packet.topic, e
                                )),
                            },
                            Err(e) => self.push_error_event(format!(
                                "normalize_inbound_event failed. topic={} detail={}",
                                publish_packet.topic, e
                            )),
                        }
                    }
                    Ok(_) => {}
                    Err(e) => {
                        if was_connected {
                            self.push_simple_event("disconnected", Some(format!("broker poll error. detail={}", e)));
                        }
                        self.push_error_event(format!(
                            "run_receiver_loop poll error. broker_host={} port={} detail={}",
                            runtime_config.broker_host_name, runtime_config.broker_port, e
                        ));
                        sleep(Duration::from_secs(3)).await;
                        break;
                    }
                }
            }
        }
    }

    fn push_error_event(&self, detail: String) {
        self.push_event(MqttInboundEventDto {
            kind: "error".to_string(),
            detail: Some(detail),
            received_at: chrono::Utc::now().to_rfc3339(),
            status: None,
            trh: None,
            ota_progress: None,
            secure_echo: None,
            device_state: None,
        });
    }

    fn push_simple_event(&self, kind: &str, detail: Option<String>) {
        self.push_event(MqttInboundEventDto {
            kind: kind.to_string(),
            detail,
            received_at: chrono::Utc::now().to_rfc3339(),
            status: None,
            trh: None,
            ota_progress: None,
            secure_echo: None,
            device_state: None,
        });
    }

    fn attach_device_state(&self, mut event_dto: MqttInboundEventDto) -> Result<MqttInboundEventDto, String> {
        let mut state_map_guard = self
            .device_state_map
            .lock()
            .map_err(|e| format!("attach_device_state failed. state-map lock error={}", e))?;
        if let Some(status) = event_dto.status.as_ref() {
            let next_state = apply_status_to_device_state(&mut state_map_guard, status);
            event_dto.device_state = Some(next_state);
            return Ok(event_dto);
        }
        if let Some(trh) = event_dto.trh.as_ref() {
            let next_state = apply_trh_to_device_state(&mut state_map_guard, trh);
            event_dto.device_state = Some(next_state);
            return Ok(event_dto);
        }
        if let Some(ota_progress) = event_dto.ota_progress.as_ref() {
            let next_state = apply_ota_progress_to_device_state(&mut state_map_guard, ota_progress);
            event_dto.device_state = Some(next_state);
            return Ok(event_dto);
        }
        Ok(event_dto)
    }

    fn collect_offline_timeout_events(&self) -> Result<Vec<MqttInboundEventDto>, String> {
        let offline_timeout_seconds = load_runtime_config()?.status_offline_timeout_seconds;
        let offline_timeout_ms = (offline_timeout_seconds.max(1)) * 1000;
        let current_time = chrono::Utc::now();
        let mut timeout_event_list = Vec::new();
        let mut state_map_guard = self
            .device_state_map
            .lock()
            .map_err(|e| format!("collect_offline_timeout_events failed. state-map lock error={}", e))?;

        for device_state in state_map_guard.values_mut() {
            if device_state.last_seen_at.trim().is_empty() {
                continue;
            }
            if device_state.online_state == "offline" {
                continue;
            }
            let parsed_last_seen_at = chrono::DateTime::parse_from_rfc3339(&device_state.last_seen_at).map_err(|e| {
                format!(
                    "collect_offline_timeout_events failed. last_seen_at parse error. device_name={} last_seen_at={} error={}",
                    device_state.device_name, device_state.last_seen_at, e
                )
            })?;
            let elapsed_ms = current_time
                .signed_duration_since(parsed_last_seen_at.with_timezone(&chrono::Utc))
                .num_milliseconds();
            if elapsed_ms <= offline_timeout_ms as i64 {
                continue;
            }
            device_state.online_state = "offline".to_string();
            device_state.detail = "Offline timeout".to_string();
            device_state.last_status_sub = "timeout".to_string();
            timeout_event_list.push(MqttInboundEventDto {
                kind: "deviceStateUpdated".to_string(),
                detail: Some("offline-timeout".to_string()),
                received_at: current_time.to_rfc3339(),
                status: None,
                trh: None,
                ota_progress: None,
                secure_echo: None,
                device_state: Some(device_state.clone()),
            });
        }

        Ok(timeout_event_list)
    }

    fn push_event(&self, event: MqttInboundEventDto) {
        if let Ok(mut queue_guard) = self.event_queue.lock() {
            queue_guard.push_back(event);
            while queue_guard.len() > self.max_queue_len {
                queue_guard.pop_front();
            }
        }
    }
}

fn get_env_string(env_name: &str, default_value: &str) -> String {
    env::var(env_name)
        .ok()
        .map(|v| v.trim().to_string())
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| default_value.to_string())
}

fn resolve_payload_encryption_mode() -> MqttPayloadEncryptionMode {
    match get_env_string("MQTT_PAYLOAD_ENCRYPTION_MODE", "strict")
        .trim()
        .to_lowercase()
        .as_str()
    {
        "plain" => MqttPayloadEncryptionMode::Plain,
        "compat" => MqttPayloadEncryptionMode::Compat,
        "strict" => MqttPayloadEncryptionMode::Strict,
        _ => MqttPayloadEncryptionMode::Strict,
    }
}

fn get_env_u16(env_name: &str, default_value: u16) -> Result<u16, String> {
    let raw_value = get_env_string(env_name, &default_value.to_string());
    raw_value
        .parse::<u16>()
        .map_err(|e| format!("get_env_u16 failed. env_name={} raw_value={} error={}", env_name, raw_value, e))
}

fn get_env_u64(env_name: &str, default_value: u64) -> Result<u64, String> {
    let raw_value = get_env_string(env_name, &default_value.to_string());
    raw_value
        .parse::<u64>()
        .map_err(|e| format!("get_env_u64 failed. env_name={} raw_value={} error={}", env_name, raw_value, e))
}

fn load_runtime_config() -> Result<MqttRuntimeConfig, String> {
    let mqtt_host_name = get_env_string("MQTT_HOST_NAME", &get_env_string("MQTT_HOST", "mqtt.esplab.home.arpa"));
    let mqtt_protocol = get_env_string("MQTT_PROTOCOL", "mqtts").to_lowercase();
    if mqtt_protocol != "mqtt" && mqtt_protocol != "mqtts" {
        return Err(format!(
            "load_runtime_config failed. MQTT_PROTOCOL must be mqtt or mqtts. actual={}",
            mqtt_protocol
        ));
    }
    Ok(MqttRuntimeConfig {
        broker_host_name: mqtt_host_name,
        broker_port: get_env_u16("MQTT_PORT", 8883)?,
        protocol: mqtt_protocol,
        username: get_env_string("MQTT_USERNAME", "esp32lab_mqtt"),
        password: get_env_string("MQTT_PASSWORD", "esp32lab_mqtt_pass32"),
        ca_path: PathBuf::from(get_env_string("MQTT_TLS_CA_PATH", "../ESP32/src/MQTT/ca.crt")),
        connect_timeout_ms: get_env_u64("MQTT_CONNECT_TIMEOUT_MS", 15_000)?,
        source_id: get_env_string("LOCAL_SERVER_SOURCE_ID", "local-server-001"),
        status_offline_timeout_seconds: get_env_u64("STATUS_OFFLINE_TIMEOUT_SECONDS", 90)?,
    })
}

fn create_mqtt_options(config: &MqttRuntimeConfig, client_role_suffix: &str) -> Result<MqttOptions, String> {
    let client_id = format!(
        "{}-secretcore-{}-{}",
        config.source_id,
        client_role_suffix,
        chrono::Utc::now().timestamp_millis()
    );
    let mut mqtt_options = MqttOptions::new(client_id, config.broker_host_name.clone(), config.broker_port);
    mqtt_options.set_keep_alive(Duration::from_secs(5));
    mqtt_options.set_credentials(config.username.clone(), config.password.clone());
    if config.protocol == "mqtts" {
        let ca_bytes = fs::read(&config.ca_path).map_err(|e| {
            format!(
                "create_mqtt_options failed. ca read error path={} error={}",
                config.ca_path.display(),
                e
            )
        })?;
        mqtt_options.set_transport(Transport::Tls(TlsConfiguration::Simple {
            ca: ca_bytes,
            alpn: None,
            client_auth: None,
        }));
    }

    Ok(mqtt_options)
}

async fn subscribe_notice_topics(client: &AsyncClient) -> Result<(), String> {
    let topic_list = ["esp32lab/notice/status/+", "esp32lab/notice/+/+"];
    for topic_name in topic_list {
        client
            .subscribe(topic_name, QoS::AtLeastOnce)
            .await
            .map_err(|e| format!("subscribe_notice_topics failed. topic={} error={}", topic_name, e))?;
    }
    Ok(())
}

fn normalize_inbound_event(key_mgr: &KeyManager, topic: &str, incoming_payload_text: &str) -> Result<MqttInboundEventDto, String> {
    let source_device_name = resolve_device_name_from_topic(topic);
    let effective_payload_text = decode_incoming_payload(key_mgr, &source_device_name, incoming_payload_text)?;
    if topic.contains("/notice/otaProgress/") {
        return Ok(MqttInboundEventDto {
            kind: "otaProgressUpdated".to_string(),
            detail: None,
            received_at: chrono::Utc::now().to_rfc3339(),
            status: None,
            trh: None,
            ota_progress: Some(parse_ota_progress_message(topic, &effective_payload_text)?),
            secure_echo: None,
            device_state: None,
        });
    }
    if topic.contains("/notice/secureEcho/") {
        return Ok(MqttInboundEventDto {
            kind: "secureEchoUpdated".to_string(),
            detail: None,
            received_at: chrono::Utc::now().to_rfc3339(),
            status: None,
            trh: None,
            ota_progress: None,
            secure_echo: Some(parse_secure_echo_message(topic, &effective_payload_text)?),
            device_state: None,
        });
    }
    if topic.contains("/notice/trh/") {
        return Ok(MqttInboundEventDto {
            kind: "trhUpdated".to_string(),
            detail: None,
            received_at: chrono::Utc::now().to_rfc3339(),
            status: None,
            trh: Some(parse_trh_message(topic, &effective_payload_text)?),
            ota_progress: None,
            secure_echo: None,
            device_state: None,
        });
    }
    Ok(MqttInboundEventDto {
        kind: "statusUpdated".to_string(),
        detail: None,
        received_at: chrono::Utc::now().to_rfc3339(),
        status: Some(parse_status_message(topic, &effective_payload_text)?),
        trh: None,
        ota_progress: None,
        secure_echo: None,
        device_state: None,
    })
}

fn apply_status_to_device_state(
    device_state_map: &mut HashMap<String, DeviceStateDto>,
    status: &StatusMessageDto,
) -> DeviceStateDto {
    let normalized_device_name = resolve_device_name_from_topic(&status.topic);
    let previous_state = device_state_map.get(&normalized_device_name).cloned();
    let normalized_online_state = normalize_online_state(&status.online_state);
    let normalized_status_sub = status.status_sub.trim().to_lowercase();
    let should_finalize_ota_by_startup =
        normalized_online_state == "online" && normalized_status_sub == "start-up" && is_ota_success_startup(previous_state.as_ref());

    let mut next_state = DeviceStateDto {
        device_name: normalized_device_name.clone(),
        public_id: status.public_id.clone(),
        config_version: status.config_version.clone(),
        src_id: status.src_id.clone(),
        dst_id: status.dst_id.clone(),
        mac_addr: status.mac_addr.clone(),
        ip_address: status.ip_address.clone(),
        wifi_ssid: status.wifi_ssid.clone(),
        firmware_version: status.firmware_version.clone(),
        firmware_written_at: status.firmware_written_at.clone(),
        running_partition: status.running_partition.clone(),
        boot_partition: status.boot_partition.clone(),
        next_update_partition: status.next_update_partition.clone(),
        ota_progress_percent: if should_finalize_ota_by_startup {
            Some(100.0)
        } else {
            previous_state.as_ref().and_then(|state| state.ota_progress_percent)
        },
        ota_phase: if should_finalize_ota_by_startup {
            "done".to_string()
        } else {
            previous_state.as_ref().map(|state| state.ota_phase.clone()).unwrap_or_default()
        },
        ota_detail: if should_finalize_ota_by_startup {
            "rebooted after ota".to_string()
        } else {
            previous_state.as_ref().map(|state| state.ota_detail.clone()).unwrap_or_default()
        },
        ota_updated_at: if should_finalize_ota_by_startup {
            previous_state
                .as_ref()
                .map(|state| if state.ota_updated_at.is_empty() { status.received_at.clone() } else { state.ota_updated_at.clone() })
                .unwrap_or_else(|| status.received_at.clone())
        } else {
            previous_state.as_ref().map(|state| state.ota_updated_at.clone()).unwrap_or_default()
        },
        online_state: normalized_online_state,
        detail: status.detail.clone(),
        last_message_id: status.message_id.clone(),
        last_status_sub: status.status_sub.clone(),
        last_seen_at: status.received_at.clone(),
        status_topic: status.topic.clone(),
        temperature_c: previous_state.as_ref().and_then(|state| state.temperature_c),
        humidity_rh: previous_state.as_ref().and_then(|state| state.humidity_rh),
        pressure_hpa: previous_state.as_ref().and_then(|state| state.pressure_hpa),
        environment_sensor_id: previous_state.as_ref().map(|state| state.environment_sensor_id.clone()).unwrap_or_default(),
        environment_sensor_address: previous_state
            .as_ref()
            .map(|state| state.environment_sensor_address.clone())
            .unwrap_or_default(),
        environment_updated_at: previous_state
            .as_ref()
            .map(|state| state.environment_updated_at.clone())
            .unwrap_or_default(),
    };

    if let Some(previous_state) = previous_state.as_ref() {
        if next_state.firmware_version.is_empty() {
            next_state.firmware_version = previous_state.firmware_version.clone();
        }
        if next_state.public_id.is_empty() {
            next_state.public_id = previous_state.public_id.clone();
        }
        if next_state.config_version.is_empty() {
            next_state.config_version = previous_state.config_version.clone();
        }
        if next_state.firmware_written_at.is_empty() {
            next_state.firmware_written_at = previous_state.firmware_written_at.clone();
        }
    }

    device_state_map.insert(normalized_device_name, next_state.clone());
    next_state
}

fn apply_trh_to_device_state(
    device_state_map: &mut HashMap<String, DeviceStateDto>,
    trh: &TrhMessageDto,
) -> DeviceStateDto {
    let normalized_device_name = resolve_device_name_from_topic(&trh.topic);
    let previous_state = device_state_map.get(&normalized_device_name).cloned();
    let is_success = trh.result.trim().eq_ignore_ascii_case("OK");
    let next_state = DeviceStateDto {
        device_name: normalized_device_name.clone(),
        public_id: previous_state.as_ref().map(|state| state.public_id.clone()).unwrap_or_default(),
        config_version: previous_state.as_ref().map(|state| state.config_version.clone()).unwrap_or_default(),
        src_id: trh.src_id.clone(),
        dst_id: trh.dst_id.clone(),
        mac_addr: previous_state.as_ref().map(|state| state.mac_addr.clone()).unwrap_or_default(),
        ip_address: previous_state.as_ref().map(|state| state.ip_address.clone()).unwrap_or_default(),
        wifi_ssid: previous_state.as_ref().map(|state| state.wifi_ssid.clone()).unwrap_or_default(),
        firmware_version: previous_state
            .as_ref()
            .map(|state| state.firmware_version.clone())
            .unwrap_or_default(),
        firmware_written_at: previous_state
            .as_ref()
            .map(|state| state.firmware_written_at.clone())
            .unwrap_or_default(),
        running_partition: previous_state
            .as_ref()
            .map(|state| state.running_partition.clone())
            .unwrap_or_default(),
        boot_partition: previous_state
            .as_ref()
            .map(|state| state.boot_partition.clone())
            .unwrap_or_default(),
        next_update_partition: previous_state
            .as_ref()
            .map(|state| state.next_update_partition.clone())
            .unwrap_or_default(),
        ota_progress_percent: previous_state.as_ref().and_then(|state| state.ota_progress_percent),
        ota_phase: previous_state.as_ref().map(|state| state.ota_phase.clone()).unwrap_or_default(),
        ota_detail: previous_state.as_ref().map(|state| state.ota_detail.clone()).unwrap_or_default(),
        ota_updated_at: previous_state.as_ref().map(|state| state.ota_updated_at.clone()).unwrap_or_default(),
        online_state: previous_state
            .as_ref()
            .map(|state| state.online_state.clone())
            .unwrap_or_else(|| "online".to_string()),
        detail: previous_state.as_ref().map(|state| state.detail.clone()).unwrap_or_default(),
        last_message_id: trh.message_id.clone(),
        last_status_sub: previous_state.as_ref().map(|state| state.last_status_sub.clone()).unwrap_or_default(),
        last_seen_at: trh.received_at.clone(),
        status_topic: previous_state.as_ref().map(|state| state.status_topic.clone()).unwrap_or_default(),
        temperature_c: if is_success { trh.temperature_c } else { previous_state.as_ref().and_then(|state| state.temperature_c) },
        humidity_rh: if is_success { trh.humidity_rh } else { previous_state.as_ref().and_then(|state| state.humidity_rh) },
        pressure_hpa: if is_success { trh.pressure_hpa } else { previous_state.as_ref().and_then(|state| state.pressure_hpa) },
        environment_sensor_id: if !trh.sensor_id.is_empty() {
            trh.sensor_id.clone()
        } else {
            previous_state.as_ref().map(|state| state.environment_sensor_id.clone()).unwrap_or_default()
        },
        environment_sensor_address: if !trh.sensor_address.is_empty() {
            trh.sensor_address.clone()
        } else {
            previous_state
                .as_ref()
                .map(|state| state.environment_sensor_address.clone())
                .unwrap_or_default()
        },
        environment_updated_at: trh.received_at.clone(),
    };
    device_state_map.insert(normalized_device_name, next_state.clone());
    next_state
}

fn apply_ota_progress_to_device_state(
    device_state_map: &mut HashMap<String, DeviceStateDto>,
    ota_progress: &OtaProgressMessageDto,
) -> DeviceStateDto {
    let normalized_device_name = resolve_device_name_from_topic(&ota_progress.topic);
    let previous_state = device_state_map.get(&normalized_device_name).cloned();
    let next_state = DeviceStateDto {
        device_name: normalized_device_name.clone(),
        public_id: previous_state.as_ref().map(|state| state.public_id.clone()).unwrap_or_default(),
        config_version: previous_state.as_ref().map(|state| state.config_version.clone()).unwrap_or_default(),
        src_id: ota_progress.src_id.clone(),
        dst_id: ota_progress.dst_id.clone(),
        mac_addr: previous_state.as_ref().map(|state| state.mac_addr.clone()).unwrap_or_default(),
        ip_address: previous_state.as_ref().map(|state| state.ip_address.clone()).unwrap_or_default(),
        wifi_ssid: previous_state.as_ref().map(|state| state.wifi_ssid.clone()).unwrap_or_default(),
        firmware_version: previous_state
            .as_ref()
            .map(|state| state.firmware_version.clone())
            .unwrap_or_default(),
        firmware_written_at: previous_state
            .as_ref()
            .map(|state| state.firmware_written_at.clone())
            .unwrap_or_default(),
        running_partition: previous_state
            .as_ref()
            .map(|state| state.running_partition.clone())
            .unwrap_or_default(),
        boot_partition: previous_state
            .as_ref()
            .map(|state| state.boot_partition.clone())
            .unwrap_or_default(),
        next_update_partition: previous_state
            .as_ref()
            .map(|state| state.next_update_partition.clone())
            .unwrap_or_default(),
        ota_progress_percent: ota_progress.progress_percent,
        ota_phase: ota_progress.phase.clone(),
        ota_detail: ota_progress.detail.clone(),
        ota_updated_at: ota_progress.received_at.clone(),
        online_state: previous_state
            .as_ref()
            .map(|state| state.online_state.clone())
            .unwrap_or_else(|| "unknown".to_string()),
        detail: previous_state.as_ref().map(|state| state.detail.clone()).unwrap_or_default(),
        last_message_id: ota_progress.message_id.clone(),
        last_status_sub: previous_state.as_ref().map(|state| state.last_status_sub.clone()).unwrap_or_default(),
        last_seen_at: previous_state.as_ref().map(|state| state.last_seen_at.clone()).unwrap_or_default(),
        status_topic: previous_state.as_ref().map(|state| state.status_topic.clone()).unwrap_or_default(),
        temperature_c: previous_state.as_ref().and_then(|state| state.temperature_c),
        humidity_rh: previous_state.as_ref().and_then(|state| state.humidity_rh),
        pressure_hpa: previous_state.as_ref().and_then(|state| state.pressure_hpa),
        environment_sensor_id: previous_state
            .as_ref()
            .map(|state| state.environment_sensor_id.clone())
            .unwrap_or_default(),
        environment_sensor_address: previous_state
            .as_ref()
            .map(|state| state.environment_sensor_address.clone())
            .unwrap_or_default(),
        environment_updated_at: previous_state
            .as_ref()
            .map(|state| state.environment_updated_at.clone())
            .unwrap_or_default(),
    };
    device_state_map.insert(normalized_device_name, next_state.clone());
    next_state
}

fn normalize_online_state(online_state_text: &str) -> String {
    let normalized_text = online_state_text.trim().to_lowercase();
    if normalized_text.contains("online") {
        return "online".to_string();
    }
    if normalized_text.contains("offline") || normalized_text.contains("disconnect") {
        return "offline".to_string();
    }
    "unknown".to_string()
}

fn is_ota_success_startup(previous_state: Option<&DeviceStateDto>) -> bool {
    let Some(previous_state) = previous_state else {
        return false;
    };
    let normalized_phase = previous_state.ota_phase.trim().to_lowercase();
    normalized_phase == "write" || normalized_phase == "verify" || normalized_phase == "done"
}

fn decode_incoming_payload(key_mgr: &KeyManager, source_device_name: &str, incoming_payload_text: &str) -> Result<String, String> {
    let encryption_mode = resolve_payload_encryption_mode();
    if encryption_mode == MqttPayloadEncryptionMode::Plain {
        return Ok(incoming_payload_text.to_string());
    }
    let envelope = try_parse_encrypted_envelope(incoming_payload_text)?;
    if envelope.is_none() {
        if encryption_mode == MqttPayloadEncryptionMode::Strict {
            return Err(format!(
                "decode_incoming_payload failed. encryption is required but envelope is missing. source_device_name={} mode=strict",
                source_device_name
            ));
        }
        return Ok(incoming_payload_text.to_string());
    }
    let (iv_base64, cipher_base64, tag_base64) = envelope.expect("envelope must exist after check");
    key_mgr.decrypt_by_k_device(source_device_name, &iv_base64, &cipher_base64, &tag_base64).map_err(|e| {
        format!(
            "decode_incoming_payload failed. source_device_name={} detail={}",
            source_device_name, e
        )
    })
}

fn try_parse_encrypted_envelope(payload_text: &str) -> Result<Option<(String, String, String)>, String> {
    let parsed_value: Value = match serde_json::from_str(payload_text) {
        Ok(parsed_value) => parsed_value,
        Err(_) => return Ok(None),
    };
    let security_mode = parsed_value
        .get("security")
        .and_then(|security| security.get("mode"))
        .and_then(Value::as_str);
    let algorithm = parsed_value
        .get("enc")
        .and_then(|enc| enc.get("alg"))
        .and_then(Value::as_str);
    if security_mode != Some("k-device-a256gcm-v1") || algorithm != Some("A256GCM") {
        return Ok(None);
    }
    let iv_base64 = get_json_string(
        parsed_value.get("enc").and_then(|enc| enc.get("iv")),
        "",
    );
    let cipher_base64 = get_json_string(
        parsed_value.get("enc").and_then(|enc| enc.get("ct")),
        "",
    );
    let tag_base64 = get_json_string(
        parsed_value.get("enc").and_then(|enc| enc.get("tag")),
        "",
    );
    if iv_base64.is_empty() || cipher_base64.is_empty() || tag_base64.is_empty() {
        return Err("try_parse_encrypted_envelope failed. encrypted fields are invalid.".to_string());
    }
    Ok(Some((iv_base64, cipher_base64, tag_base64)))
}

fn parse_status_message(topic: &str, payload_text: &str) -> Result<StatusMessageDto, String> {
    let parsed_value: Value = serde_json::from_str(payload_text)
        .map_err(|e| format!("parse_status_message failed. json parse error={}", e))?;
    let src_id = get_json_string(parsed_value.get("SrcID"), "unknown-source");
    let firmware_version = {
        let camel = get_json_string(parsed_value.get("fwVersion"), "");
        if camel.is_empty() {
            get_json_string(parsed_value.get("firmwareVersion"), "")
        } else {
            camel
        }
    };
    let firmware_written_at = {
        let camel = get_json_string(parsed_value.get("fwWrittenAt"), "");
        if camel.is_empty() {
            get_json_string(parsed_value.get("firmwareWrittenAt"), "")
        } else {
            camel
        }
    };
    Ok(StatusMessageDto {
        topic: topic.to_string(),
        src_id: src_id.clone(),
        dst_id: get_json_string(parsed_value.get("DstID"), ""),
        message_id: get_json_string(parsed_value.get("id"), &format!("{}-{}", src_id, chrono::Utc::now().timestamp_millis())),
        public_id: {
            let camel = get_json_string(parsed_value.get("publicId"), "");
            if camel.is_empty() {
                get_json_string(parsed_value.get("public_id"), "")
            } else {
                camel
            }
        },
        config_version: {
            let camel = get_json_string(parsed_value.get("configVersion"), "");
            if camel.is_empty() {
                get_json_string(parsed_value.get("configVer"), "")
            } else {
                camel
            }
        },
        mac_addr: get_json_string(parsed_value.get("macAddr"), ""),
        ip_address: get_json_string(parsed_value.get("ipAddress"), ""),
        wifi_ssid: get_json_string(parsed_value.get("wifiSsid"), ""),
        firmware_version,
        firmware_written_at,
        running_partition: get_json_string(parsed_value.get("runningPartition"), ""),
        boot_partition: get_json_string(parsed_value.get("bootPartition"), ""),
        next_update_partition: get_json_string(parsed_value.get("nextUpdatePartition"), ""),
        online_state: get_json_string(parsed_value.get("onlineState"), "unknown"),
        status_sub: get_json_string(parsed_value.get("sub"), "status"),
        detail: get_json_string(parsed_value.get("detail"), ""),
        received_at: chrono::Utc::now().to_rfc3339(),
    })
}

fn parse_ota_progress_message(topic: &str, payload_text: &str) -> Result<OtaProgressMessageDto, String> {
    let parsed_value: Value = serde_json::from_str(payload_text)
        .map_err(|e| format!("parse_ota_progress_message failed. json parse error={}", e))?;
    let src_id = get_json_string(parsed_value.get("SrcID"), "unknown-source");
    Ok(OtaProgressMessageDto {
        topic: topic.to_string(),
        src_id: src_id.clone(),
        dst_id: get_json_string(parsed_value.get("DstID"), ""),
        message_id: get_json_string(parsed_value.get("id"), &format!("{}-{}", src_id, chrono::Utc::now().timestamp_millis())),
        firmware_version: {
            let camel = get_json_string(parsed_value.get("fwVersion"), "");
            if camel.is_empty() {
                get_json_string(parsed_value.get("firmwareVersion"), "")
            } else {
                camel
            }
        },
        progress_percent: get_json_number(parsed_value.get("progressPercent")),
        phase: get_json_string(parsed_value.get("phase"), ""),
        detail: get_json_string(parsed_value.get("detail"), ""),
        received_at: chrono::Utc::now().to_rfc3339(),
    })
}

fn parse_secure_echo_message(topic: &str, payload_text: &str) -> Result<SecureEchoMessageDto, String> {
    let parsed_value: Value = serde_json::from_str(payload_text)
        .map_err(|e| format!("parse_secure_echo_message failed. json parse error={}", e))?;
    let request_id = get_json_string(parsed_value.get("requestId"), "");
    let iv_base64 = get_json_string(parsed_value.get("enc").and_then(|enc| enc.get("iv")), "");
    let cipher_base64 = get_json_string(parsed_value.get("enc").and_then(|enc| enc.get("ct")), "");
    let tag_base64 = get_json_string(parsed_value.get("enc").and_then(|enc| enc.get("tag")), "");
    if request_id.is_empty() {
        return Err("parse_secure_echo_message failed. requestId is empty.".to_string());
    }
    if iv_base64.is_empty() || cipher_base64.is_empty() || tag_base64.is_empty() {
        return Err("parse_secure_echo_message failed. encrypted field is missing.".to_string());
    }
    Ok(SecureEchoMessageDto {
        topic: topic.to_string(),
        src_id: get_json_string(parsed_value.get("SrcID"), "unknown-source"),
        request_id,
        iv_base64,
        cipher_base64,
        tag_base64,
        received_at: chrono::Utc::now().to_rfc3339(),
    })
}

fn parse_trh_message(topic: &str, payload_text: &str) -> Result<TrhMessageDto, String> {
    let parsed_value: Value = serde_json::from_str(payload_text)
        .map_err(|e| format!("parse_trh_message failed. json parse error={}", e))?;
    let src_id = get_json_string(parsed_value.get("SrcID"), "unknown-source");
    let args_object = parsed_value.get("args");
    Ok(TrhMessageDto {
        topic: topic.to_string(),
        src_id: src_id.clone(),
        dst_id: get_json_string(parsed_value.get("DstID"), ""),
        message_id: get_json_string(parsed_value.get("id"), &format!("{}-{}", src_id, chrono::Utc::now().timestamp_millis())),
        result: get_json_string(parsed_value.get("Res"), ""),
        detail: get_json_string(parsed_value.get("detail"), ""),
        sensor_id: get_json_string(args_object.and_then(|args| args.get("sensorId")), ""),
        sensor_address: get_json_string(args_object.and_then(|args| args.get("sensorAddress")), ""),
        temperature_c: get_json_number(args_object.and_then(|args| args.get("temperatureC"))),
        humidity_rh: get_json_number(args_object.and_then(|args| args.get("humidityRh"))),
        pressure_hpa: get_json_number(args_object.and_then(|args| args.get("pressureHpa"))),
        received_at: chrono::Utc::now().to_rfc3339(),
    })
}

fn get_json_string(value_option: Option<&Value>, fallback_value: &str) -> String {
    value_option
        .and_then(Value::as_str)
        .unwrap_or(fallback_value)
        .to_string()
}

fn get_json_number(value_option: Option<&Value>) -> Option<f64> {
    value_option.and_then(Value::as_f64)
}

fn resolve_device_name_from_topic(topic: &str) -> String {
    topic.split('/').last().unwrap_or("").to_string()
}

pub async fn publish_message(topic: &str, payload: &str, qos: u8) -> Result<(), String> {
    let config = load_runtime_config()?;
    let mqtt_options = create_mqtt_options(&config, "tx")?;
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);
    let qos_value = match qos {
        0 => QoS::AtMostOnce,
        1 => QoS::AtLeastOnce,
        2 => QoS::ExactlyOnce,
        _ => {
            return Err(format!(
                "publish_message failed. qos must be 0..2. actual={}",
                qos
            ))
        }
    };

    let publish_topic = topic.to_string();
    let publish_payload = payload.as_bytes().to_vec();
    let mut publish_task = Some(tokio::spawn(async move {
        client
            .publish(publish_topic, qos_value, false, publish_payload)
            .await
            .map_err(|e| format!("publish_message failed. publish enqueue error={}", e))
    }));

    let deadline = Instant::now() + Duration::from_millis(config.connect_timeout_ms);
    let mut enqueue_completed = false;

    loop {
        if Instant::now() >= deadline {
            return Err(format!(
                "publish_message failed. timeout topic={} timeout_ms={}",
                topic, config.connect_timeout_ms
            ));
        }
        if !enqueue_completed && publish_task.as_ref().is_some_and(|task| task.is_finished()) {
            publish_task
                .take()
                .expect("publish_task must exist before completion check")
                .await
                .map_err(|e| format!("publish_message failed. join error={}", e))??;
            enqueue_completed = true;
        }

        match eventloop.poll().await {
            Ok(Event::Incoming(Incoming::PubAck(_))) if qos == 1 => {
                return Ok(());
            }
            Ok(Event::Outgoing(Outgoing::Publish(_))) if qos == 0 => {
                return Ok(());
            }
            Ok(Event::Incoming(Incoming::PubComp(_))) if qos == 2 => {
                return Ok(());
            }
            Ok(_) => {
                sleep(Duration::from_millis(10)).await;
            }
            Err(e) => {
                return Err(format!(
                    "publish_message failed. broker_host={} port={} topic={} error={}",
                    config.broker_host_name, config.broker_port, topic, e
                ));
            }
        }
    }
}
