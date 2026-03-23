// pairing_workflow.rs - SecretCore の Pairing workflow 管理。
//
// [重要] Pairing 開始要求の受理、`createPairingBundle` 相当の内部生成、workflow 状態管理を Rust 側へ集約する。
// [厳守] LocalServer へ返す情報は workflowId / state / result / errorSummary / 最小補助情報に限定する。
// [禁止] raw `k-device`、ECDH 共有秘密、bundle 平文を IPC 応答へ返さない。
// [進捗] 2026-03-16 時点では bundle 生成、AP 到達/認証 precheck、session metadata / bundle summary の placeholder 登録までは実装し、
// ECDH・secure bundle 本体送達・NVS 完了判定は将来実装とする。
// 理由: `runPairingSession()` の内部 helper 境界を先に固定し、以後の実装が TS 側へ漏れないようにするため。

use crate::key_manager::KeyManager;
use aes_gcm::aead::{AeadInPlace, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use base64::prelude::*;
use hmac::{Hmac, Mac};
use p256::ecdh::EphemeralSecret;
use p256::elliptic_curve::rand_core::OsRng;
use p256::elliptic_curve::sec1::ToEncodedPoint;
use p256::PublicKey;
use rand::Rng;
use reqwest::Client;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::env;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::time::sleep;

type HmacSha256 = Hmac<Sha256>;

/// Pairing workflow の Wi-Fi 設定入力。
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PairingRequestedWifiSettings {
    /// 接続先 Wi-Fi SSID。
    pub ssid: String,
    /// 必要時のみ使用する Wi-Fi ユーザー名。
    pub username: Option<String>,
    /// 接続先 Wi-Fi パスワード。
    pub password: String,
}

/// Pairing workflow の MQTT / OTA 接続先設定入力。
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PairingRequestedEndpointSettings {
    /// 接続先ホスト。
    pub host: String,
    /// 証明書名検証や表示用のホスト名。
    pub host_name: Option<String>,
    /// 接続先ポート。
    pub port: u16,
    /// TLS 有効/無効。
    pub tls: bool,
    /// 接続先認証ユーザー名。
    pub username: String,
    /// 接続先認証パスワード。
    pub password: String,
    /// CA 証明書参照名。
    pub ca_cert_ref: Option<String>,
}

/// Pairing workflow の server / ntp 任意設定入力。
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PairingRequestedServerSettings {
    /// 接続先ホスト。
    pub host: String,
    /// 表示・名前検証用ホスト名。
    pub host_name: Option<String>,
    /// 接続先ポート。
    pub port: u16,
    /// TLS 有効/無効。
    pub tls: bool,
}

/// Pairing workflow の認証情報入力。
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PairingRequestedCredentials {
    /// 必要時のみ使用する Wi-Fi ユーザー名。
    pub wifi_username: Option<String>,
    /// Wi-Fi パスワード。
    pub wifi_password: String,
    /// MQTT ユーザー名。
    pub mqtt_username: String,
    /// MQTT パスワード。
    pub mqtt_password: String,
    /// OTA ユーザー名。
    pub ota_username: String,
    /// OTA パスワード。
    pub ota_password: String,
    /// Base64 形式 `k-device`。
    pub key_device: String,
}

/// Pairing workflow の要求設定全体。
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PairingRequestedSettings {
    /// Wi-Fi 設定。
    pub wifi: PairingRequestedWifiSettings,
    /// MQTT 設定。
    pub mqtt: PairingRequestedEndpointSettings,
    /// OTA 設定。
    pub ota: PairingRequestedEndpointSettings,
    /// 認証情報。
    pub credentials: PairingRequestedCredentials,
    /// 任意の server 設定。
    pub server: Option<PairingRequestedServerSettings>,
    /// 任意の NTP 設定。
    pub ntp: Option<PairingRequestedServerSettings>,
}

/// Pairing workflow 開始要求。
#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PairingWorkflowStartRequest {
    /// workflow が扱う対象デバイス識別子。
    pub target_device_id: String,
    /// 再送防止や監査突合に用いるセッションID。
    pub session_id: String,
    /// `k-device` / bundle が従う鍵バージョン。
    pub key_version: String,
    /// Pairing で投入したい設定群。
    pub requested_settings: PairingRequestedSettings,
}

/// Pairing workflow 状態 DTO。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PairingWorkflowStatusDto {
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
    /// 表示・監査向け補助詳細。
    detail: Option<String>,
    /// 開始日時。
    started_at: String,
    /// 最終更新日時。
    updated_at: String,
}

/// `createPairingBundle` 相当で内部生成する成果物。
///
/// LocalServer へは返さず、workflow 実行中の Rust 側だけで保持する。
#[allow(dead_code)]
#[derive(Debug, Clone)]
struct PairingBundle {
    /// 表示や保存に使う公開識別子。
    public_id: String,
    /// Pairing 対象 `k-device` Base64。
    k_device: String,
    /// 鍵バージョン。
    key_version: String,
    /// 投入対象の requestedSettings。
    requested_settings: PairingRequestedSettings,
    /// bundle 識別子。
    bundle_id: String,
    /// セッション識別子。
    session_id: String,
    /// 対象デバイス識別子。
    target_device_id: String,
    /// 再送防止用 nonce。
    nonce: String,
    /// 固定署名対象を HMAC-SHA256(k-user) で署名した値。
    signature: String,
}

/// Pairing bundle 署名対象の固定フィールド。
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct PairingBundleSignaturePayload {
    /// 署名対象コマンド名。
    command: String,
    /// 対象デバイス識別子。
    target: String,
    /// Unix epoch milliseconds。
    timestamp: i64,
    /// 再送防止 nonce。
    nonce: String,
    /// 鍵バージョン。
    key_version: String,
}

/// AP メンテナンス到達前提を表す設定。
///
/// `SecretCore` は `LocalServer` 子プロセスとして起動されるため、
/// `.env` で読み込まれた環境変数を親から継承して利用する。
struct MaintenanceApConfig {
    /// AP 側 HTTP ベース URL。
    base_url: String,
    /// AP ログインに使うユーザー名。
    username: String,
    /// AP ログインに使うパスワード。
    password: String,
}

/// AP ログイン応答。
#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct MaintenanceApLoginResponse {
    /// API 実行結果。
    result: String,
    /// 認証トークン。
    token: String,
    /// AP で認可されたロール。
    role: String,
    /// 失敗時の詳細。
    detail: Option<String>,
}

/// AP 到達・認証 precheck の結果。
struct MaintenanceApPrecheckResult {
    /// ログイン結果ロール。
    role: String,
    /// 現在 `keyDevice` が入っているか。
    has_key_device: bool,
    /// AP 側が返した対象デバイス識別子。
    target_device_id: String,
    /// AP 側 Pairing 状態。
    pairing_state: String,
}

/// AP 側 Pairing session metadata 登録結果。
struct MaintenanceApSessionRegistrationResult {
    /// AP 側 Pairing 状態。
    pairing_state: String,
    /// AP 側対象デバイス識別子。
    target_device_id: String,
}

/// AP 側 Pairing bundle summary 登録結果。
struct MaintenanceApBundleSummaryRegistrationResult {
    /// AP 側 Pairing 状態。
    pairing_state: String,
    /// AP 側対象デバイス識別子。
    target_device_id: String,
}

/// AP 側 Pairing secure transport placeholder 登録結果。
struct MaintenanceApTransportSessionRegistrationResult {
    /// AP 側 Pairing 状態。
    pairing_state: String,
    /// AP 側対象デバイス識別子。
    target_device_id: String,
    /// AP 側が受理した鍵共有方式。
    accepted_key_agreement: String,
    /// AP 側が受理した bundle 保護方式。
    accepted_bundle_protection: String,
}

/// AP 側 Pairing secure transport handshake 結果。
struct MaintenanceApTransportHandshakeResult {
    /// AP 側 Pairing 状態。
    pairing_state: String,
    /// AP 側対象デバイス識別子。
    target_device_id: String,
    /// AP 側が受理した鍵共有方式。
    accepted_key_agreement: String,
    /// AP 側が受理した bundle 保護方式。
    accepted_bundle_protection: String,
    /// AP 側公開鍵。
    server_public_key_base64: String,
    /// AP / Rust 双方が導出した transport 鍵 fingerprint。
    shared_secret_fingerprint: String,
    /// Rust 側で保持する transport session。
    transport_session: PairingTransportSession,
}

/// AP 側 Pairing secure bundle 適用結果。
struct MaintenanceApSecureBundleApplyResult {
    /// AP 側 Pairing 状態。
    pairing_state: String,
    /// AP 側対象デバイス識別子。
    target_device_id: String,
    /// AP 側へ保存した current key version。
    saved_current_key_version: String,
    /// AP 側が返した previous key state。
    previous_key_state: String,
}

/// Pairing workflow 中に保持する transport session。
///
/// [厳守] transport 鍵は workflow 実行中の Rust メモリだけに保持し、IPC 応答やログへ raw 値を出さない。
#[allow(dead_code)]
#[derive(Clone)]
struct PairingTransportSession {
    /// 32byte transport session key。
    session_key_bytes: [u8; 32],
    /// AP 側公開鍵。
    server_public_key_base64: String,
    /// Rust 側公開鍵。
    client_public_key_base64: String,
    /// transport 鍵 fingerprint。
    shared_secret_fingerprint: String,
    /// 鍵共有方式。
    accepted_key_agreement: String,
    /// bundle 保護方式。
    accepted_bundle_protection: String,
}

/// Pairing workflow の状態管理本体。
pub struct PairingWorkflowManager {
    workflow_map: Mutex<HashMap<String, PairingWorkflowStatusDto>>,
    bundle_map: Mutex<HashMap<String, PairingBundle>>,
    transport_session_map: Mutex<HashMap<String, PairingTransportSession>>,
}

impl PairingWorkflowManager {
    /// Pairing workflow 管理を初期化する。
    ///
    /// workflow 状態と bundle は in-memory 保持とし、プロセス再起動時は消える。
    /// 将来 Persistent 化する場合も LocalServer から見える DTO 形状は維持する。
    pub fn new() -> Self {
        Self {
            workflow_map: Mutex::new(HashMap::new()),
            bundle_map: Mutex::new(HashMap::new()),
            transport_session_map: Mutex::new(HashMap::new()),
        }
    }

    /// workflow ID から現在状態を取得する。
    ///
    /// Pairing / OTA は別 manager で保持するため、`main.rs` 側で順次問い合わせる前提で使う。
    pub fn get_workflow_status(&self, workflow_id: &str) -> Result<PairingWorkflowStatusDto, String> {
        let workflow_map_guard = self
            .workflow_map
            .lock()
            .map_err(|e| format!("get_workflow_status failed. pairing workflow-map lock error={} workflow_id={}", e, workflow_id))?;
        workflow_map_guard
            .get(workflow_id)
            .cloned()
            .ok_or_else(|| format!("get_workflow_status failed. pairing workflow_id not found. workflow_id={}", workflow_id))
    }

    /// Pairing workflow を開始する。
    ///
    /// 要求検証後に `createPairingBundle` 相当の内部 helper を呼び、workflow 開始時点で
    /// bundle ID / nonce / signature を持つ内部成果物を生成する。
    pub fn start_pairing_session(
        self: &Arc<Self>,
        key_manager: &Arc<KeyManager>,
        request: PairingWorkflowStartRequest,
    ) -> Result<PairingWorkflowStatusDto, String> {
        validate_pairing_workflow_start_request(&request)?;

        let workflow_id = create_pairing_workflow_id();
        let pairing_bundle = create_pairing_bundle(key_manager.as_ref(), &request)?;
        self.store_pairing_bundle(&workflow_id, pairing_bundle.clone())?;

        let started_at = chrono::Utc::now().to_rfc3339();
        let initial_status = PairingWorkflowStatusDto {
            workflow_id: workflow_id.clone(),
            workflow_type: "pairing".to_string(),
            target_device_id: request.target_device_id.clone(),
            state: "queued".to_string(),
            result: None,
            error_summary: None,
            detail: Some(format!(
                "pairing workflow queued. bundle_id={} public_id={}",
                pairing_bundle.bundle_id, pairing_bundle.public_id
            )),
            started_at: started_at.clone(),
            updated_at: started_at,
        };
        self.update_workflow_status(initial_status.clone())?;

        let workflow_manager = Arc::clone(self);
        tokio::spawn(async move {
            workflow_manager
                .execute_pairing_workflow_placeholder(workflow_id, request)
                .await;
        });

        Ok(initial_status)
    }

    /// KeyRotation workflow を開始する。
    ///
    /// [重要] 現段階では secure bundle の送達・検証経路を Pairing workflow と共通化し、
    /// `workflowType=key-rotation` として状態管理だけを分離する。
    /// [厳守] TS 側は逐次手順を持たず、開始要求と結果表示のみを担当する。
    pub fn start_key_rotation_session(
        self: &Arc<Self>,
        key_manager: &Arc<KeyManager>,
        request: PairingWorkflowStartRequest,
    ) -> Result<PairingWorkflowStatusDto, String> {
        validate_pairing_workflow_start_request(&request)?;

        let workflow_id = create_pairing_workflow_id();
        let pairing_bundle = create_pairing_bundle(key_manager.as_ref(), &request)?;
        self.store_pairing_bundle(&workflow_id, pairing_bundle.clone())?;

        let started_at = chrono::Utc::now().to_rfc3339();
        let initial_status = PairingWorkflowStatusDto {
            workflow_id: workflow_id.clone(),
            workflow_type: "key-rotation".to_string(),
            target_device_id: request.target_device_id.clone(),
            state: "queued".to_string(),
            result: None,
            error_summary: None,
            detail: Some(format!(
                "key rotation workflow queued. bundle_id={} public_id={} key_version={}",
                pairing_bundle.bundle_id, pairing_bundle.public_id, request.key_version
            )),
            started_at: started_at.clone(),
            updated_at: started_at,
        };
        self.update_workflow_status(initial_status.clone())?;

        let workflow_manager = Arc::clone(self);
        tokio::spawn(async move {
            workflow_manager
                .execute_pairing_workflow_placeholder(workflow_id, request)
                .await;
        });

        Ok(initial_status)
    }

    /// Pairing workflow 骨格の状態遷移を実行する。
    ///
    /// 現段階では bundle 生成済み前提で AP 到達・認証 precheck、session metadata 登録、
    /// bundle summary 登録、secure transport placeholder 交渉を行い、
    /// `waiting_device` と `verifying` まで遷移させる。
    /// その後に「ECDH / bundle 送達本体 / NVS 完了判定未実装」で `failed` へ落とす。
    async fn execute_pairing_workflow_placeholder(
        self: Arc<Self>,
        workflow_id: String,
        request: PairingWorkflowStartRequest,
    ) {
        let pairing_bundle_result = self.get_pairing_bundle(&workflow_id);
        let pairing_bundle = match pairing_bundle_result {
            Ok(bundle) => bundle,
            Err(bundle_error) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(bundle_error),
                    Some("pairing bundle lookup failed".to_string()),
                );
                return;
            }
        };

        let _ = self.update_state(
            &workflow_id,
            "running",
            None,
            None,
            Some(format!(
                "pairing bundle created. bundle_id={} nonce={} target_device_id={} key_version={}",
                pairing_bundle.bundle_id, pairing_bundle.nonce, request.target_device_id, request.key_version
            )),
        );

        let maintenance_precheck_result = run_maintenance_ap_precheck().await;
        let maintenance_precheck = match maintenance_precheck_result {
            Ok(result) => result,
            Err(precheck_error) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(precheck_error),
                    Some("maintenance ap precheck failed".to_string()),
                );
                return;
            }
        };

        sleep(Duration::from_millis(250)).await;

        let _ = self.update_state(
            &workflow_id,
            "waiting_device",
            None,
            None,
            Some(format!(
                "pairing bundle is ready for AP transport. bundle_id={} signature_length={} ap_role={} existing_key_device={} ap_target_device_id={} ap_pairing_state={}",
                pairing_bundle.bundle_id,
                pairing_bundle.signature.len(),
                maintenance_precheck.role,
                maintenance_precheck.has_key_device,
                maintenance_precheck.target_device_id,
                maintenance_precheck.pairing_state
            )),
        );

        let session_registration_result = register_pairing_session_placeholder(&pairing_bundle).await;
        let session_registration = match session_registration_result {
            Ok(result) => result,
            Err(registration_error) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(registration_error),
                    Some("pairing session registration failed".to_string()),
                );
                return;
            }
        };

        let bundle_summary_registration_result =
            register_pairing_bundle_summary_placeholder(&pairing_bundle).await;
        let bundle_summary_registration = match bundle_summary_registration_result {
            Ok(result) => result,
            Err(registration_error) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(registration_error),
                    Some("pairing bundle summary registration failed".to_string()),
                );
                return;
            }
        };

        let transport_session_registration_result =
            register_pairing_transport_session_placeholder(&pairing_bundle).await;
        let transport_session_registration = match transport_session_registration_result {
            Ok(result) => result,
            Err(registration_error) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(registration_error),
                    Some("pairing transport session registration failed".to_string()),
                );
                return;
            }
        };

        let transport_handshake_result = perform_pairing_transport_handshake(&pairing_bundle).await;
        let transport_handshake = match transport_handshake_result {
            Ok(result) => result,
            Err(handshake_error) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(handshake_error),
                    Some("pairing transport handshake failed".to_string()),
                );
                return;
            }
        };
        if let Err(store_error) = self.store_transport_session(&workflow_id, transport_handshake.transport_session.clone()) {
            let _ = self.update_state(
                &workflow_id,
                "failed",
                Some("NG".to_string()),
                Some(store_error),
                Some("pairing transport session store failed".to_string()),
            );
            return;
        }

        sleep(Duration::from_millis(250)).await;

        let _ = self.update_state(
            &workflow_id,
            "verifying",
            None,
            None,
            Some(format!(
                "pairing bundle summary staged. bundle_id={} session_state={} session_target_device_id={} bundle_state={} bundle_target_device_id={} transport_state={} transport_target_device_id={} requested_key_agreement={} requested_bundle_protection={} handshake_state={} accepted_key_agreement={} accepted_bundle_protection={} ap_target_device_id={} transport_fingerprint={} server_public_key_length={}",
                pairing_bundle.bundle_id,
                session_registration.pairing_state,
                session_registration.target_device_id,
                bundle_summary_registration.pairing_state,
                bundle_summary_registration.target_device_id,
                transport_session_registration.pairing_state,
                transport_session_registration.target_device_id,
                transport_session_registration.accepted_key_agreement,
                transport_session_registration.accepted_bundle_protection,
                transport_handshake.pairing_state,
                transport_handshake.accepted_key_agreement,
                transport_handshake.accepted_bundle_protection,
                transport_handshake.target_device_id,
                transport_handshake.shared_secret_fingerprint,
                transport_handshake.server_public_key_base64.len()
            )),
        );

        sleep(Duration::from_millis(250)).await;

        let secure_bundle_apply_result =
            apply_pairing_secure_bundle_to_maintenance_ap(&pairing_bundle, &transport_handshake.transport_session).await;
        let secure_bundle_apply = match secure_bundle_apply_result {
            Ok(result) => result,
            Err(apply_error) => {
                let _ = self.update_state(
                    &workflow_id,
                    "failed",
                    Some("NG".to_string()),
                    Some(apply_error),
                    Some("pairing secure bundle apply failed".to_string()),
                );
                return;
            }
        };

        let _ = self.update_state(
            &workflow_id,
            "completed",
            Some("OK".to_string()),
            None,
            Some(format!(
                "pairing secure bundle applied. bundle_id={} session_id={} ap_target_device_id={} ap_state={} saved_current_key_version={} previous_key_state={} transport_fingerprint={}",
                pairing_bundle.bundle_id,
                pairing_bundle.session_id,
                secure_bundle_apply.target_device_id,
                secure_bundle_apply.pairing_state,
                secure_bundle_apply.saved_current_key_version,
                secure_bundle_apply.previous_key_state,
                transport_handshake.shared_secret_fingerprint
            )),
        );
    }

    /// workflow に紐づく Pairing bundle を保存する。
    ///
    /// ここでは bundle 本文を外へ出さず、workflow ID と内部成果物の対応だけを保持する。
    fn store_pairing_bundle(&self, workflow_id: &str, pairing_bundle: PairingBundle) -> Result<(), String> {
        let mut bundle_map_guard = self
            .bundle_map
            .lock()
            .map_err(|e| format!("store_pairing_bundle failed. pairing bundle-map lock error={} workflow_id={}", e, workflow_id))?;
        bundle_map_guard.insert(workflow_id.to_string(), pairing_bundle);
        Ok(())
    }

    /// workflow に紐づく transport session を保存する。
    fn store_transport_session(&self, workflow_id: &str, transport_session: PairingTransportSession) -> Result<(), String> {
        let mut transport_session_map_guard = self
            .transport_session_map
            .lock()
            .map_err(|e| format!("store_transport_session failed. transport session-map lock error={} workflow_id={}", e, workflow_id))?;
        transport_session_map_guard.insert(workflow_id.to_string(), transport_session);
        Ok(())
    }

    /// workflow に紐づく Pairing bundle を取得する。
    fn get_pairing_bundle(&self, workflow_id: &str) -> Result<PairingBundle, String> {
        let bundle_map_guard = self
            .bundle_map
            .lock()
            .map_err(|e| format!("get_pairing_bundle failed. pairing bundle-map lock error={} workflow_id={}", e, workflow_id))?;
        bundle_map_guard
            .get(workflow_id)
            .cloned()
            .ok_or_else(|| format!("get_pairing_bundle failed. pairing bundle not found. workflow_id={}", workflow_id))
    }

    /// 既存 workflow に対して状態・結果・詳細を更新する。
    ///
    /// workflow ごとの開始日時は維持し、変更差分だけを上書きする。
    fn update_state(
        &self,
        workflow_id: &str,
        state: &str,
        result: Option<String>,
        error_summary: Option<String>,
        detail: Option<String>,
    ) -> Result<(), String> {
        let current_status = self.get_workflow_status(workflow_id)?;
        self.update_workflow_status(PairingWorkflowStatusDto {
            workflow_id: current_status.workflow_id,
            workflow_type: current_status.workflow_type,
            target_device_id: current_status.target_device_id,
            state: state.to_string(),
            result,
            error_summary,
            detail,
            started_at: current_status.started_at,
            updated_at: chrono::Utc::now().to_rfc3339(),
        })
    }

    /// workflow 状態を map へ保存する。
    ///
    /// 呼び出し元は DTO を完成形で渡し、この関数は排他更新だけを担当する。
    fn update_workflow_status(&self, workflow_status: PairingWorkflowStatusDto) -> Result<(), String> {
        let mut workflow_map_guard = self
            .workflow_map
            .lock()
            .map_err(|e| format!("update_workflow_status failed. pairing workflow-map lock error={} workflow_id={}", e, workflow_status.workflow_id))?;
        workflow_map_guard.insert(workflow_status.workflow_id.clone(), workflow_status);
        Ok(())
    }
}

/// Pairing workflow 開始要求の最低限の整合を検証する。
///
/// TS 側でも検証しているが、SecretCore 側でも境界防御として再確認する。
fn validate_pairing_workflow_start_request(request: &PairingWorkflowStartRequest) -> Result<(), String> {
    validate_required_text("targetDeviceId", &request.target_device_id)?;
    validate_required_text("sessionId", &request.session_id)?;
    validate_required_text("keyVersion", &request.key_version)?;
    validate_pairing_requested_settings(&request.requested_settings)?;
    validate_k_device_base64(&request.requested_settings.credentials.key_device)?;
    Ok(())
}

/// `requestedSettings` 配下の必須項目を検証する。
fn validate_pairing_requested_settings(requested_settings: &PairingRequestedSettings) -> Result<(), String> {
    validate_required_text("requestedSettings.wifi.ssid", &requested_settings.wifi.ssid)?;
    validate_required_text("requestedSettings.mqtt.host", &requested_settings.mqtt.host)?;
    validate_required_text("requestedSettings.ota.host", &requested_settings.ota.host)?;
    validate_required_text(
        "requestedSettings.credentials.keyDevice",
        &requested_settings.credentials.key_device,
    )?;
    Ok(())
}

/// `createPairingBundle` 相当の内部 helper。
///
/// 文書で定義された bundle 構成要素を Rust 側だけで生成し、workflow 実行中の内部成果物として保持する。
fn create_pairing_bundle(
    key_manager: &KeyManager,
    request: &PairingWorkflowStartRequest,
) -> Result<PairingBundle, String> {
    let bundle_id = create_pairing_bundle_id();
    let nonce = create_pairing_bundle_nonce();
    let signature_payload = PairingBundleSignaturePayload {
        command: "pairingBundle".to_string(),
        target: request.target_device_id.clone(),
        timestamp: chrono::Utc::now().timestamp_millis(),
        nonce: nonce.clone(),
        key_version: request.key_version.clone(),
    };
    let signature = create_pairing_bundle_signature(key_manager, &signature_payload)?;

    Ok(PairingBundle {
        // [重要] `public_id` の正式決定ロジックは後続で device_db / 実機状態と統合する。
        // 現段階では workflow 入力の対象識別子を暫定採用し、bundle 生成責務のみ先行固定する。
        public_id: request.target_device_id.clone(),
        k_device: request.requested_settings.credentials.key_device.clone(),
        key_version: request.key_version.clone(),
        requested_settings: request.requested_settings.clone(),
        bundle_id,
        session_id: request.session_id.clone(),
        target_device_id: request.target_device_id.clone(),
        nonce,
        signature,
    })
}

/// Pairing bundle の署名対象 JSON を HMAC-SHA256(k-user) で署名する。
///
/// 正式な固定公開鍵署名へ移行するまでは、Rust 側内部 helper の整合確認用として署名文字列を生成する。
fn create_pairing_bundle_signature(
    key_manager: &KeyManager,
    signature_payload: &PairingBundleSignaturePayload,
) -> Result<String, String> {
    let signature_payload_json = serde_json::to_string(signature_payload)
        .map_err(|e| format!("create_pairing_bundle_signature failed. serialize error={}", e))?;
    let k_user = key_manager.get_k_user();
    let mut direct_hmac = <HmacSha256 as Mac>::new_from_slice(&k_user)
        .map_err(|e| format!("create_pairing_bundle_signature failed. hmac init error={}", e))?;
    direct_hmac.update(signature_payload_json.as_bytes());
    let signature_bytes = direct_hmac.finalize().into_bytes();
    Ok(BASE64_STANDARD.encode(signature_bytes))
}

/// `k-device` Base64 の妥当性を検証する。
fn validate_k_device_base64(key_device_base64: &str) -> Result<(), String> {
    let key_device_bytes = BASE64_STANDARD
        .decode(key_device_base64.trim())
        .map_err(|e| format!("validate_k_device_base64 failed. base64 decode error={}", e))?;
    if key_device_bytes.len() != 32 {
        return Err(format!(
            "validate_k_device_base64 failed. decoded length invalid={}",
            key_device_bytes.len()
        ));
    }
    Ok(())
}

/// 必須文字列が空でないことを検証する。
fn validate_required_text(field_name: &str, value: &str) -> Result<(), String> {
    if value.trim().is_empty() {
        return Err(format!(
            "validate_required_text failed. field_name={} value is empty.",
            field_name
        ));
    }
    Ok(())
}

/// Pairing workflow 用の一意な workflow ID を生成する。
fn create_pairing_workflow_id() -> String {
    let random_suffix = u32::from(rand::random::<u16>()) % 100_000;
    format!(
        "workflow-pairing-{}-{:05}",
        chrono::Utc::now().format("%Y%m%d%H%M%S"),
        random_suffix
    )
}

/// Pairing bundle 用の一意な bundle ID を生成する。
fn create_pairing_bundle_id() -> String {
    let random_suffix = u32::from(rand::random::<u16>()) % 100_000;
    format!(
        "bundle-pairing-{}-{:05}",
        chrono::Utc::now().format("%Y%m%d%H%M%S"),
        random_suffix
    )
}

/// Pairing bundle 用 nonce を生成する。
///
/// 現段階では 16 byte ランダムを Base64 化して保持し、今後の AP モード再送防止と bundle 一意性に使う。
fn create_pairing_bundle_nonce() -> String {
    let mut nonce_bytes = [0u8; 16];
    rand::rng().fill_bytes(&mut nonce_bytes);
    BASE64_STANDARD.encode(nonce_bytes)
}

/// AP メンテナンス接続の事前確認を行う。
///
/// Pairing workflow 本番実装前でも、`SecretCore` が AP へ到達し認証できることをここで確認する。
/// これにより通信責務を TS から Rust へ段階移行しつつ、bundle 送達実装前の失敗理由を明確化できる。
async fn run_maintenance_ap_precheck() -> Result<MaintenanceApPrecheckResult, String> {
    let maintenance_ap_config = load_maintenance_ap_config_from_env()?;
    let http_client = Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| format!("run_maintenance_ap_precheck failed. http client build error={}", e))?;
    let login_response = login_to_maintenance_ap(&http_client, &maintenance_ap_config).await?;
    let network_settings_json =
        get_maintenance_ap_network_settings(&http_client, &maintenance_ap_config.base_url, &login_response.token).await?;
    let pairing_state_json =
        get_maintenance_ap_pairing_state(&http_client, &maintenance_ap_config.base_url, &login_response.token).await?;
    let has_key_device = network_settings_json
        .get("keyDevice")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .len()
        > 0;
    let target_device_id = pairing_state_json
        .get("targetDeviceId")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let pairing_state = pairing_state_json
        .get("state")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();

    Ok(MaintenanceApPrecheckResult {
        role: login_response.role,
        has_key_device,
        target_device_id,
        pairing_state,
    })
}

/// 環境変数から AP メンテナンス接続設定を読み込む。
///
/// `LocalServer` の `.env` を親プロセスが読み込んだ後に `SecretCore` を起動する前提で、
/// `AP_HTTP_BASE_URL`、`AP_ROLE_ADMIN_USERNAME`、`AP_ROLE_ADMIN_PASSWORD` を継承利用する。
fn load_maintenance_ap_config_from_env() -> Result<MaintenanceApConfig, String> {
    let base_url = env::var("AP_HTTP_BASE_URL").unwrap_or_else(|_| "http://192.168.4.1".to_string());
    let username = env::var("AP_ROLE_ADMIN_USERNAME").unwrap_or_else(|_| "admin".to_string());
    let password = env::var("AP_ROLE_ADMIN_PASSWORD").unwrap_or_else(|_| "change-me".to_string());
    if base_url.trim().is_empty() {
        return Err("load_maintenance_ap_config_from_env failed. AP_HTTP_BASE_URL is empty.".to_string());
    }
    if username.trim().is_empty() {
        return Err("load_maintenance_ap_config_from_env failed. AP_ROLE_ADMIN_USERNAME is empty.".to_string());
    }
    if password.trim().is_empty() {
        return Err("load_maintenance_ap_config_from_env failed. AP_ROLE_ADMIN_PASSWORD is empty.".to_string());
    }

    Ok(MaintenanceApConfig {
        base_url,
        username,
        password,
    })
}

/// AP メンテナンス API へログインする。
///
/// [現行実装] ESP32 側 `maintenanceApServer.cpp` は `/api/auth/login` を提供しており、
/// 現在の LocalServer と同じ経路で認証トークンを取得する。
async fn login_to_maintenance_ap(
    http_client: &Client,
    maintenance_ap_config: &MaintenanceApConfig,
) -> Result<MaintenanceApLoginResponse, String> {
    let login_url = format!("{}/api/auth/login", maintenance_ap_config.base_url.trim_end_matches('/'));
    let response = http_client
        .post(login_url)
        .json(&serde_json::json!({
            "username": maintenance_ap_config.username,
            "password": maintenance_ap_config.password
        }))
        .send()
        .await
        .map_err(|e| format!("login_to_maintenance_ap failed. send error={}", e))?;
    let status_code = response.status();
    let body_text = response
        .text()
        .await
        .map_err(|e| format!("login_to_maintenance_ap failed. body read error={}", e))?;
    if !status_code.is_success() {
        return Err(format!(
            "login_to_maintenance_ap failed. status={} body={}",
            status_code.as_u16(),
            body_text
        ));
    }

    let login_response: MaintenanceApLoginResponse = serde_json::from_str(&body_text)
        .map_err(|e| format!("login_to_maintenance_ap failed. json parse error={} body={}", e, body_text))?;
    if login_response.result != "OK" {
        return Err(format!(
            "login_to_maintenance_ap failed. result={} detail={}",
            login_response.result,
            login_response.detail.unwrap_or_default()
        ));
    }
    if login_response.token.trim().is_empty() {
        return Err("login_to_maintenance_ap failed. token is empty.".to_string());
    }
    Ok(login_response)
}

/// AP メンテナンス API から現在の network 設定を取得する。
///
/// ここでは Pairing 送達前の認証成功確認が主目的であり、取得した JSON は最小限の存在確認だけに使う。
async fn get_maintenance_ap_network_settings(
    http_client: &Client,
    base_url: &str,
    token: &str,
) -> Result<serde_json::Value, String> {
    let settings_url = format!("{}/api/settings/network", base_url.trim_end_matches('/'));
    let response = http_client
        .get(settings_url)
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .await
        .map_err(|e| format!("get_maintenance_ap_network_settings failed. send error={}", e))?;
    let status_code = response.status();
    let body_text = response
        .text()
        .await
        .map_err(|e| format!("get_maintenance_ap_network_settings failed. body read error={}", e))?;
    if !status_code.is_success() {
        return Err(format!(
            "get_maintenance_ap_network_settings failed. status={} body={}",
            status_code.as_u16(),
            body_text
        ));
    }

    serde_json::from_str::<serde_json::Value>(&body_text)
        .map_err(|e| format!("get_maintenance_ap_network_settings failed. json parse error={} body={}", e, body_text))
}

/// AP メンテナンス API から Pairing 状態を取得する。
///
/// `waiting_device` 以降の監視導線を先に固定するため、現段階では placeholder 応答でも Rust 側で読む。
async fn get_maintenance_ap_pairing_state(
    http_client: &Client,
    base_url: &str,
    token: &str,
) -> Result<serde_json::Value, String> {
    let state_url = format!("{}/api/pairing/state", base_url.trim_end_matches('/'));
    let response = http_client
        .get(state_url)
        .header("Authorization", format!("Bearer {}", token))
        .send()
        .await
        .map_err(|e| format!("get_maintenance_ap_pairing_state failed. send error={}", e))?;
    let status_code = response.status();
    let body_text = response
        .text()
        .await
        .map_err(|e| format!("get_maintenance_ap_pairing_state failed. body read error={}", e))?;
    if !status_code.is_success() {
        return Err(format!(
            "get_maintenance_ap_pairing_state failed. status={} body={}",
            status_code.as_u16(),
            body_text
        ));
    }

    serde_json::from_str::<serde_json::Value>(&body_text)
        .map_err(|e| format!("get_maintenance_ap_pairing_state failed. json parse error={} body={}", e, body_text))
}

/// AP 側へ Pairing session metadata を登録する。
///
/// [現段階] 秘密 payload 本文は送らず、`sessionId` / `bundleId` / `targetDeviceId` / `keyVersion` のみ登録する。
/// これにより `waiting_device` の次段として AP 側の Pairing 状態が `bundle_received` へ進む導線を先に固定する。
async fn register_pairing_session_placeholder(
    pairing_bundle: &PairingBundle,
) -> Result<MaintenanceApSessionRegistrationResult, String> {
    let maintenance_ap_config = load_maintenance_ap_config_from_env()?;
    let http_client = Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| format!("register_pairing_session_placeholder failed. http client build error={}", e))?;
    let login_response = login_to_maintenance_ap(&http_client, &maintenance_ap_config).await?;

    let registration_url = format!(
        "{}/api/pairing/session",
        maintenance_ap_config.base_url.trim_end_matches('/')
    );
    let registration_response = http_client
        .post(registration_url)
        .header("Authorization", format!("Bearer {}", login_response.token))
        .json(&serde_json::json!({
            "targetDeviceId": pairing_bundle.target_device_id,
            "sessionId": pairing_bundle.session_id,
            "bundleId": pairing_bundle.bundle_id,
            "keyVersion": pairing_bundle.key_version
        }))
        .send()
        .await
        .map_err(|e| format!("register_pairing_session_placeholder failed. send error={}", e))?;
    let registration_status_code = registration_response.status();
    let registration_body_text = registration_response
        .text()
        .await
        .map_err(|e| format!("register_pairing_session_placeholder failed. body read error={}", e))?;
    if !registration_status_code.is_success() {
        return Err(format!(
            "register_pairing_session_placeholder failed. status={} body={}",
            registration_status_code.as_u16(),
            registration_body_text
        ));
    }

    let pairing_state_json =
        get_maintenance_ap_pairing_state(&http_client, &maintenance_ap_config.base_url, &login_response.token).await?;
    let target_device_id = pairing_state_json
        .get("targetDeviceId")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let pairing_state = pairing_state_json
        .get("state")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();

    Ok(MaintenanceApSessionRegistrationResult {
        pairing_state,
        target_device_id,
    })
}

/// AP 側へ Pairing bundle summary を登録する。
///
/// [現段階] `createPairingBundle` の秘密本体は送らず、bundle 識別と監査に必要な summary のみを渡す。
/// これにより AP 側が `bundle_staged` まで進んだことを `runPairingSession()` の `verifying` 手前で確認できる。
async fn register_pairing_bundle_summary_placeholder(
    pairing_bundle: &PairingBundle,
) -> Result<MaintenanceApBundleSummaryRegistrationResult, String> {
    let maintenance_ap_config = load_maintenance_ap_config_from_env()?;
    let http_client = Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| format!("register_pairing_bundle_summary_placeholder failed. http client build error={}", e))?;
    let login_response = login_to_maintenance_ap(&http_client, &maintenance_ap_config).await?;
    let requested_settings_sha256 = create_requested_settings_sha256_hex(&pairing_bundle.requested_settings)?;

    let registration_url = format!(
        "{}/api/pairing/bundle-summary",
        maintenance_ap_config.base_url.trim_end_matches('/')
    );
    let registration_response = http_client
        .post(registration_url)
        .header("Authorization", format!("Bearer {}", login_response.token))
        .json(&serde_json::json!({
            "targetDeviceId": pairing_bundle.target_device_id,
            "sessionId": pairing_bundle.session_id,
            "bundleId": pairing_bundle.bundle_id,
            "publicId": pairing_bundle.public_id,
            "keyVersion": pairing_bundle.key_version,
            "nonce": pairing_bundle.nonce,
            "signature": pairing_bundle.signature,
            "requestedSettingsSha256": requested_settings_sha256
        }))
        .send()
        .await
        .map_err(|e| format!("register_pairing_bundle_summary_placeholder failed. send error={}", e))?;
    let registration_status_code = registration_response.status();
    let registration_body_text = registration_response
        .text()
        .await
        .map_err(|e| format!("register_pairing_bundle_summary_placeholder failed. body read error={}", e))?;
    if !registration_status_code.is_success() {
        return Err(format!(
            "register_pairing_bundle_summary_placeholder failed. status={} body={}",
            registration_status_code.as_u16(),
            registration_body_text
        ));
    }

    let pairing_state_json =
        get_maintenance_ap_pairing_state(&http_client, &maintenance_ap_config.base_url, &login_response.token).await?;
    let target_device_id = pairing_state_json
        .get("targetDeviceId")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let pairing_state = pairing_state_json
        .get("state")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();

    Ok(MaintenanceApBundleSummaryRegistrationResult {
        pairing_state,
        target_device_id,
    })
}

/// AP 側へ Pairing secure transport placeholder を登録する。
///
/// [現段階] ECDH や AES-GCM 本体はまだ実行せず、どの保護方式で secure bundle を運ぶ想定かだけを AP 側へ固定する。
/// これにより secure transport 実装時に Rust / AP の責務境界を変えずに後続APIを差し込める。
async fn register_pairing_transport_session_placeholder(
    pairing_bundle: &PairingBundle,
) -> Result<MaintenanceApTransportSessionRegistrationResult, String> {
    let maintenance_ap_config = load_maintenance_ap_config_from_env()?;
    let http_client = Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| format!("register_pairing_transport_session_placeholder failed. http client build error={}", e))?;
    let login_response = login_to_maintenance_ap(&http_client, &maintenance_ap_config).await?;
    let requested_key_agreement = "ecdh-p256-v1";
    let requested_bundle_protection = "aes-256-gcm-v1";

    let registration_url = format!(
        "{}/api/pairing/transport-session",
        maintenance_ap_config.base_url.trim_end_matches('/')
    );
    let registration_response = http_client
        .post(registration_url)
        .header("Authorization", format!("Bearer {}", login_response.token))
        .json(&serde_json::json!({
            "targetDeviceId": pairing_bundle.target_device_id,
            "sessionId": pairing_bundle.session_id,
            "bundleId": pairing_bundle.bundle_id,
            "requestedKeyAgreement": requested_key_agreement,
            "requestedBundleProtection": requested_bundle_protection
        }))
        .send()
        .await
        .map_err(|e| format!("register_pairing_transport_session_placeholder failed. send error={}", e))?;
    let registration_status_code = registration_response.status();
    let registration_body_text = registration_response
        .text()
        .await
        .map_err(|e| format!("register_pairing_transport_session_placeholder failed. body read error={}", e))?;
    if !registration_status_code.is_success() {
        return Err(format!(
            "register_pairing_transport_session_placeholder failed. status={} body={}",
            registration_status_code.as_u16(),
            registration_body_text
        ));
    }
    let registration_json = serde_json::from_str::<serde_json::Value>(&registration_body_text)
        .map_err(|e| format!("register_pairing_transport_session_placeholder failed. json parse error={} body={}", e, registration_body_text))?;
    let accepted_key_agreement = registration_json
        .get("acceptedKeyAgreement")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let accepted_bundle_protection = registration_json
        .get("acceptedBundleProtection")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();

    let pairing_state_json =
        get_maintenance_ap_pairing_state(&http_client, &maintenance_ap_config.base_url, &login_response.token).await?;
    let target_device_id = pairing_state_json
        .get("targetDeviceId")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let pairing_state = pairing_state_json
        .get("state")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();

    Ok(MaintenanceApTransportSessionRegistrationResult {
        pairing_state,
        target_device_id,
        accepted_key_agreement,
        accepted_bundle_protection,
    })
}

/// AP 側と P-256 ECDH transport handshake を実行する。
///
/// [重要] raw 共有秘密は外へ返さず、workflow 内の transport session と fingerprint だけを保持する。
async fn perform_pairing_transport_handshake(
    pairing_bundle: &PairingBundle,
) -> Result<MaintenanceApTransportHandshakeResult, String> {
    let maintenance_ap_config = load_maintenance_ap_config_from_env()?;
    let http_client = Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| format!("perform_pairing_transport_handshake failed. http client build error={}", e))?;
    let login_response = login_to_maintenance_ap(&http_client, &maintenance_ap_config).await?;

    let local_secret = EphemeralSecret::random(&mut OsRng);
    let local_public_key = PublicKey::from(&local_secret);
    let local_public_key_bytes = local_public_key.to_encoded_point(false).as_bytes().to_vec();
    let local_public_key_base64 = BASE64_STANDARD.encode(&local_public_key_bytes);

    let handshake_url = format!(
        "{}/api/pairing/transport-handshake",
        maintenance_ap_config.base_url.trim_end_matches('/')
    );
    let handshake_response = http_client
        .post(handshake_url)
        .header("Authorization", format!("Bearer {}", login_response.token))
        .json(&serde_json::json!({
            "targetDeviceId": pairing_bundle.target_device_id,
            "sessionId": pairing_bundle.session_id,
            "bundleId": pairing_bundle.bundle_id,
            "clientPublicKeyBase64": local_public_key_base64
        }))
        .send()
        .await
        .map_err(|e| format!("perform_pairing_transport_handshake failed. send error={}", e))?;
    let handshake_status_code = handshake_response.status();
    let handshake_body_text = handshake_response
        .text()
        .await
        .map_err(|e| format!("perform_pairing_transport_handshake failed. body read error={}", e))?;
    if !handshake_status_code.is_success() {
        return Err(format!(
            "perform_pairing_transport_handshake failed. status={} body={}",
            handshake_status_code.as_u16(),
            handshake_body_text
        ));
    }
    let handshake_json = serde_json::from_str::<serde_json::Value>(&handshake_body_text)
        .map_err(|e| format!("perform_pairing_transport_handshake failed. json parse error={} body={}", e, handshake_body_text))?;
    let server_public_key_base64 = handshake_json
        .get("serverPublicKeyBase64")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let accepted_key_agreement = handshake_json
        .get("acceptedKeyAgreement")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let accepted_bundle_protection = handshake_json
        .get("acceptedBundleProtection")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let target_device_id = handshake_json
        .get("targetDeviceId")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let pairing_state = handshake_json
        .get("state")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    if server_public_key_base64.is_empty() {
        return Err("perform_pairing_transport_handshake failed. serverPublicKeyBase64 is empty.".to_string());
    }

    let server_public_key_bytes = BASE64_STANDARD
        .decode(server_public_key_base64.as_bytes())
        .map_err(|e| format!("perform_pairing_transport_handshake failed. server public key base64 decode error={}", e))?;
    let server_public_key = PublicKey::from_sec1_bytes(&server_public_key_bytes)
        .map_err(|e| format!("perform_pairing_transport_handshake failed. server public key decode error={}", e))?;
    let shared_secret = local_secret.diffie_hellman(&server_public_key);
    let transport_session_key_bytes =
        derive_pairing_transport_session_key(shared_secret.raw_secret_bytes().as_slice(), &pairing_bundle.session_id, &pairing_bundle.bundle_id);
    let shared_secret_fingerprint = create_sha256_hex(transport_session_key_bytes.as_slice());

    let transport_session = PairingTransportSession {
        session_key_bytes: transport_session_key_bytes,
        server_public_key_base64: server_public_key_base64.clone(),
        client_public_key_base64: BASE64_STANDARD.encode(local_public_key_bytes),
        shared_secret_fingerprint: shared_secret_fingerprint.clone(),
        accepted_key_agreement: accepted_key_agreement.clone(),
        accepted_bundle_protection: accepted_bundle_protection.clone(),
    };

    Ok(MaintenanceApTransportHandshakeResult {
        pairing_state,
        target_device_id,
        accepted_key_agreement,
        accepted_bundle_protection,
        server_public_key_base64,
        shared_secret_fingerprint,
        transport_session,
    })
}

/// AP 側へ暗号化済み Pairing bundle を送達し、復号・保存状態を確認する。
///
/// [厳守] 送達 payload は AES-256-GCM で保護し、平文設定は Rust 内メモリでのみ扱う。
async fn apply_pairing_secure_bundle_to_maintenance_ap(
    pairing_bundle: &PairingBundle,
    transport_session: &PairingTransportSession,
) -> Result<MaintenanceApSecureBundleApplyResult, String> {
    let maintenance_ap_config = load_maintenance_ap_config_from_env()?;
    let http_client = Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|e| format!("apply_pairing_secure_bundle_to_maintenance_ap failed. http client build error={}", e))?;
    let login_response = login_to_maintenance_ap(&http_client, &maintenance_ap_config).await?;
    let payload_plain_bytes = create_pairing_secure_bundle_payload_json_bytes(pairing_bundle)?;
    let payload_sha256 = create_sha256_hex(payload_plain_bytes.as_slice());
    let aad_bytes = create_pairing_secure_bundle_aad(pairing_bundle);
    let (iv_base64, cipher_base64, tag_base64) = encrypt_pairing_secure_bundle_payload(
        &transport_session.session_key_bytes,
        payload_plain_bytes.as_slice(),
        aad_bytes.as_slice(),
    )?;

    let apply_url = format!(
        "{}/api/pairing/secure-bundle",
        maintenance_ap_config.base_url.trim_end_matches('/')
    );
    let apply_response = http_client
        .post(apply_url)
        .header("Authorization", format!("Bearer {}", login_response.token))
        .json(&serde_json::json!({
            "targetDeviceId": pairing_bundle.target_device_id,
            "sessionId": pairing_bundle.session_id,
            "bundleId": pairing_bundle.bundle_id,
            "keyVersion": pairing_bundle.key_version,
            "ivBase64": iv_base64,
            "cipherBase64": cipher_base64,
            "tagBase64": tag_base64,
            "payloadSha256": payload_sha256
        }))
        .send()
        .await
        .map_err(|e| format!("apply_pairing_secure_bundle_to_maintenance_ap failed. send error={}", e))?;
    let apply_status_code = apply_response.status();
    let apply_body_text = apply_response
        .text()
        .await
        .map_err(|e| format!("apply_pairing_secure_bundle_to_maintenance_ap failed. body read error={}", e))?;
    if !apply_status_code.is_success() {
        return Err(format!(
            "apply_pairing_secure_bundle_to_maintenance_ap failed. status={} body={}",
            apply_status_code.as_u16(),
            apply_body_text
        ));
    }

    let pairing_state_json =
        get_maintenance_ap_pairing_state(&http_client, &maintenance_ap_config.base_url, &login_response.token).await?;
    let target_device_id = pairing_state_json
        .get("targetDeviceId")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let pairing_state = pairing_state_json
        .get("state")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let saved_current_key_version = pairing_state_json
        .get("savedCurrentKeyVersion")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    let previous_key_state = pairing_state_json
        .get("previousKeyState")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim()
        .to_string();
    if !pairing_state.eq_ignore_ascii_case("applied") {
        return Err(format!(
            "apply_pairing_secure_bundle_to_maintenance_ap failed. unexpected state={} targetDeviceId={}",
            pairing_state, target_device_id
        ));
    }
    if saved_current_key_version != pairing_bundle.key_version {
        return Err(format!(
            "apply_pairing_secure_bundle_to_maintenance_ap failed. savedCurrentKeyVersion mismatch. expected={} actual={}",
            pairing_bundle.key_version, saved_current_key_version
        ));
    }

    Ok(MaintenanceApSecureBundleApplyResult {
        pairing_state,
        target_device_id,
        saved_current_key_version,
        previous_key_state,
    })
}

/// Pairing secure bundle payload の JSON bytes を生成する。
fn create_pairing_secure_bundle_payload_json_bytes(pairing_bundle: &PairingBundle) -> Result<Vec<u8>, String> {
    #[derive(Serialize)]
    #[serde(rename_all = "camelCase")]
    struct PairingSecureBundlePayload<'a> {
        target_device_id: &'a str,
        session_id: &'a str,
        bundle_id: &'a str,
        key_version: &'a str,
        requested_settings: &'a PairingRequestedSettings,
    }

    let payload = PairingSecureBundlePayload {
        target_device_id: pairing_bundle.target_device_id.as_str(),
        session_id: pairing_bundle.session_id.as_str(),
        bundle_id: pairing_bundle.bundle_id.as_str(),
        key_version: pairing_bundle.key_version.as_str(),
        requested_settings: &pairing_bundle.requested_settings,
    };
    serde_json::to_vec(&payload)
        .map_err(|e| format!("create_pairing_secure_bundle_payload_json_bytes failed. serialize error={}", e))
}

/// Pairing secure bundle の AAD を生成する。
fn create_pairing_secure_bundle_aad(pairing_bundle: &PairingBundle) -> Vec<u8> {
    format!(
        "pairing-secure-bundle-v1|{}|{}|{}|{}",
        pairing_bundle.target_device_id, pairing_bundle.session_id, pairing_bundle.bundle_id, pairing_bundle.key_version
    )
    .into_bytes()
}

/// AES-256-GCM で Pairing secure bundle payload を暗号化する。
fn encrypt_pairing_secure_bundle_payload(
    session_key_bytes: &[u8; 32],
    payload_plain_bytes: &[u8],
    aad_bytes: &[u8],
) -> Result<(String, String, String), String> {
    let aes256gcm = Aes256Gcm::new_from_slice(session_key_bytes)
        .map_err(|e| format!("encrypt_pairing_secure_bundle_payload failed. cipher init error={}", e))?;
    let mut nonce_bytes = [0u8; 12];
    rand::rng().fill_bytes(&mut nonce_bytes);
    let nonce = Nonce::from_slice(&nonce_bytes);
    let mut cipher_bytes = payload_plain_bytes.to_vec();
    let tag = aes256gcm
        .encrypt_in_place_detached(nonce, aad_bytes, &mut cipher_bytes)
        .map_err(|e| format!("encrypt_pairing_secure_bundle_payload failed. encrypt error={}", e))?;

    Ok((
        BASE64_STANDARD.encode(nonce_bytes),
        BASE64_STANDARD.encode(cipher_bytes),
        BASE64_STANDARD.encode(tag),
    ))
}

/// `requestedSettings` の canonical JSON から SHA-256（16進小文字）を生成する。
///
/// [重要] 現段階では AP 側へ設定平文を送らず、summary placeholder の整合確認だけに使う。
fn create_requested_settings_sha256_hex(
    requested_settings: &PairingRequestedSettings,
) -> Result<String, String> {
    let requested_settings_json = serde_json::to_vec(requested_settings)
        .map_err(|e| format!("create_requested_settings_sha256_hex failed. serialize error={}", e))?;
    let mut sha256_hasher = Sha256::new();
    sha256_hasher.update(requested_settings_json);
    let hash_bytes = sha256_hasher.finalize();
    let hash_text: String = hash_bytes.iter().map(|byte| format!("{:02x}", byte)).collect();
    Ok(hash_text)
}

/// ECDH 共有秘密と workflow 識別子から transport session key を導出する。
fn derive_pairing_transport_session_key(shared_secret_bytes: &[u8], session_id: &str, bundle_id: &str) -> [u8; 32] {
    let mut sha256_hasher = Sha256::new();
    sha256_hasher.update(shared_secret_bytes);
    sha256_hasher.update(session_id.as_bytes());
    sha256_hasher.update(b"|");
    sha256_hasher.update(bundle_id.as_bytes());
    let hash_bytes = sha256_hasher.finalize();
    let mut session_key_bytes = [0u8; 32];
    session_key_bytes.copy_from_slice(hash_bytes.as_slice());
    session_key_bytes
}

/// 任意バイト列の SHA-256（16進小文字）を返す。
fn create_sha256_hex(input_bytes: &[u8]) -> String {
    let mut sha256_hasher = Sha256::new();
    sha256_hasher.update(input_bytes);
    let hash_bytes = sha256_hasher.finalize();
    hash_bytes.iter().map(|byte| format!("{:02x}", byte)).collect()
}
