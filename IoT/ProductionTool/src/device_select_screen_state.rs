//! `ProductionTool` 対象機選択・識別確認画面（`PT-003`）の状態モジュール。
//!
//! [重要] 本ファイルは `PT-003` 対象機選択・識別確認画面の状態を定義します。
//! 主な仕様:
//! - `SecretCore` IPC または CLI 入力で対象機識別情報を受け付ける
//! - 一覧からの選択方式で対象機取り違えを防止する
//! - 無効な行選択時は次画面（PT-004）へ進ませない
//! 制限事項:
//! - 現フェーズは CLI 上での手動入力で識別情報を確認する
//! - 実機との自動照合（Named Pipe 経由 `SecretCore` 問い合わせ）は後続フェーズで追加する
//! - `PT-003` で不可逆処理を直接開始するボタンは置かない

use serde::Serialize;

/// 対象機識別情報を表します。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceInfo {
    /// シリアル番号です。
    pub serial_number: String,
    /// MAC アドレスです（コロン区切り、大文字）。
    pub mac_address: String,
    /// `public_id`（`k-device` の公開識別子）です。
    pub public_id: String,
    /// FW 版数です。
    pub firmware_version: String,
    /// セキュア化状態ラベルです。
    pub secure_status_label: String,
    /// 接続状態ラベルです（例: MQTT接続中 / APモード）。
    pub connection_status_label: String,
}

/// 対象機確認の結果を表します。
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum DeviceVerifyResult {
    /// 一覧選択が有効で、対象機を確認しました。
    Confirmed,
    /// 一覧選択が無効（範囲外・未入力）です。
    InvalidSelection,
    /// 対象機一覧が空です。
    DeviceListEmpty,
}

/// `PT-003` 対象機選択・識別確認画面の状態です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceSelectScreenState {
    /// 対象機一覧です。
    pub device_list: Vec<DeviceInfo>,
    /// 選択された行番号（0始まり）です。
    pub selected_index: Option<usize>,
    /// 選択された対象機識別情報です。
    pub selected_device: Option<DeviceInfo>,
    /// 対象機確認結果です。
    pub verify_result: DeviceVerifyResult,
    /// 次画面（PT-004）へ進めるかどうかです。
    pub can_proceed_to_execution_confirm: bool,
    /// 監査ログ用のラベルです。
    pub audit_label: String,
}

/// 対象機一覧と選択行を照合し、`PT-003` 画面状態を構築します。
///
/// # 引数
/// - `device_list`: 参照する対象機一覧
/// - `selected_index`: 操作者が選択した行番号（0始まり）
///
/// # 戻り値
/// `DeviceSelectScreenState`
pub fn verify_device_selection(
    device_list: &[DeviceInfo],
    selected_index: Option<usize>,
) -> DeviceSelectScreenState {
    if device_list.is_empty() {
        return DeviceSelectScreenState {
            device_list: Vec::new(),
            selected_index,
            selected_device: None,
            verify_result: DeviceVerifyResult::DeviceListEmpty,
            can_proceed_to_execution_confirm: false,
            audit_label: "device_verify_failed:deviceListEmpty".to_string(),
        };
    }

    let Some(index) = selected_index else {
        return DeviceSelectScreenState {
            device_list: device_list.to_vec(),
            selected_index: None,
            selected_device: None,
            verify_result: DeviceVerifyResult::InvalidSelection,
            can_proceed_to_execution_confirm: false,
            audit_label: "device_verify_failed:emptySelection".to_string(),
        };
    };

    if index >= device_list.len() {
        return DeviceSelectScreenState {
            device_list: device_list.to_vec(),
            selected_index: Some(index),
            selected_device: None,
            verify_result: DeviceVerifyResult::InvalidSelection,
            can_proceed_to_execution_confirm: false,
            audit_label: format!(
                "device_verify_failed:indexOutOfRange selectedIndex={} listLen={}",
                index,
                device_list.len()
            ),
        };
    }

    let selected_device = device_list[index].clone();
    DeviceSelectScreenState {
        device_list: device_list.to_vec(),
        selected_index: Some(index),
        selected_device: Some(selected_device.clone()),
        verify_result: DeviceVerifyResult::Confirmed,
        can_proceed_to_execution_confirm: true,
        audit_label: format!(
            "device_verify_success selectedIndex={} serial={} mac={} publicId={}",
            index,
            selected_device.serial_number,
            selected_device.mac_address,
            selected_device.public_id
        ),
    }
}

/// 現フェーズ用のプレースホルダー対象機一覧を CLI 入力から構築します。
///
/// [将来対応] 本番実装では SecretCore IPC 経由で自動取得する。
/// 現フェーズは CLI 手動入力で代用する。
pub fn build_placeholder_device_list() -> Vec<DeviceInfo> {
    use std::io::{self, Write};

    println!("--- PT-003 対象機一覧入力（現フェーズ: 手動入力） ---");
    println!("[注意] 現フェーズでは接続中の実機情報を手動入力して一覧化します。");
    print!("対象機数（空欄時は 1）: ");
    io::stdout().flush().unwrap_or_default();
    let mut device_count_raw = String::new();
    io::stdin()
        .read_line(&mut device_count_raw)
        .unwrap_or_default();

    let mut device_count = device_count_raw.trim().parse::<usize>().unwrap_or(1);
    if device_count == 0 {
        device_count = 1;
    }

    let mut device_list = Vec::with_capacity(device_count);
    for row in 0..device_count {
        println!("\n  対象機 {} / {}", row + 1, device_count);

        print!("  シリアル番号: ");
        io::stdout().flush().unwrap_or_default();
        let mut serial = String::new();
        io::stdin().read_line(&mut serial).unwrap_or_default();

        print!("  MAC アドレス (例: AA:BB:CC:DD:EE:FF): ");
        io::stdout().flush().unwrap_or_default();
        let mut mac = String::new();
        io::stdin().read_line(&mut mac).unwrap_or_default();

        print!("  public_id (わからない場合は 'unknown'): ");
        io::stdout().flush().unwrap_or_default();
        let mut public_id = String::new();
        io::stdin().read_line(&mut public_id).unwrap_or_default();

        print!("  FW 版数 (例: 1.1.0-beta.15): ");
        io::stdout().flush().unwrap_or_default();
        let mut fw_version = String::new();
        io::stdin().read_line(&mut fw_version).unwrap_or_default();

        print!("  接続状態 (MQTT/AP, 空欄時は MQTT): ");
        io::stdout().flush().unwrap_or_default();
        let mut connection_status = String::new();
        io::stdin()
            .read_line(&mut connection_status)
            .unwrap_or_default();

        let connection_status_label = if connection_status.trim().eq_ignore_ascii_case("AP") {
            "APモード".to_string()
        } else {
            "MQTT接続中".to_string()
        };

        device_list.push(DeviceInfo {
            serial_number: serial.trim().to_string(),
            mac_address: mac.trim().to_string(),
            public_id: public_id.trim().to_string(),
            firmware_version: fw_version.trim().to_string(),
            secure_status_label: "未確認（現フェーズ手動入力）".to_string(),
            connection_status_label,
        });
    }

    device_list
}

/// CLI で対象機一覧を表示し、選択行番号を受け付けます。
///
/// # 引数
/// - `device_list`: 操作者へ提示する対象機一覧
///
/// # 戻り値
/// `Some(index)`（0始まり）または `None`（未選択）
pub fn prompt_device_selection(device_list: &[DeviceInfo]) -> Option<usize> {
    use std::io::{self, Write};

    println!("--- PT-003 対象機一覧（行選択） ---");
    for (index, device) in device_list.iter().enumerate() {
        println!(
            "  [{}] serial={} | mac={} | publicId={} | fw={} | secure={} | connection={}",
            index + 1,
            device.serial_number,
            device.mac_address,
            device.public_id,
            device.firmware_version,
            device.secure_status_label,
            device.connection_status_label
        );
    }
    println!("上記一覧から対象機の行番号を選択してください（MAC 再入力は不要）。");
    print!("選択行番号（1..{}）: ", device_list.len());
    io::stdout().flush().unwrap_or_default();
    let mut selected_row = String::new();
    io::stdin()
        .read_line(&mut selected_row)
        .unwrap_or_default();

    let parsed_row = selected_row.trim().parse::<usize>().ok()?;
    if parsed_row == 0 {
        return None;
    }
    Some(parsed_row - 1)
}
