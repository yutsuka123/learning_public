//! `ProductionTool` 対象機選択・識別確認画面（`PT-003`）の状態モジュール。
//!
//! [重要] 本ファイルは `PT-003` 対象機選択・識別確認画面の状態を定義します。
//! 主な仕様:
//! - `SecretCore` IPC または CLI 入力で対象機識別情報を受け付ける
//! - シリアル番号・MAC アドレスの再入力一致で対象機取り違えを防止する
//! - 不一致時は次画面（PT-004）へ進ませない
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
}

/// 対象機確認の再入力結果を表します。
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum DeviceVerifyResult {
    /// 再入力が一致し、対象機を確認しました。
    Confirmed,
    /// シリアル番号の再入力が一致しませんでした。
    SerialMismatch,
    /// MAC アドレスの再入力が一致しませんでした。
    MacMismatch,
    /// 対象機情報が未取得です。
    DeviceInfoNotAvailable,
}

/// `PT-003` 対象機選択・識別確認画面の状態です。
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceSelectScreenState {
    /// 対象機識別情報です。
    pub device_info: Option<DeviceInfo>,
    /// 再入力したシリアル番号です（ログ保存用）。
    pub entered_serial_number: String,
    /// 再入力した MAC アドレスです（ログ保存用）。
    pub entered_mac_address: String,
    /// 対象機確認結果です。
    pub verify_result: DeviceVerifyResult,
    /// 次画面（PT-004）へ進めるかどうかです。
    pub can_proceed_to_execution_confirm: bool,
    /// 監査ログ用のラベルです。
    pub audit_label: String,
}

/// 対象機識別情報と再入力値を照合し、`PT-003` 画面状態を構築します。
///
/// # 引数
/// - `device_info`: 参照する対象機識別情報
/// - `entered_serial_number`: 操作者が再入力したシリアル番号
/// - `entered_mac_address`: 操作者が再入力した MAC アドレス
///
/// # 戻り値
/// `DeviceSelectScreenState`
pub fn verify_device_selection(
    device_info: Option<&DeviceInfo>,
    entered_serial_number: &str,
    entered_mac_address: &str,
) -> DeviceSelectScreenState {
    let Some(info) = device_info else {
        return DeviceSelectScreenState {
            device_info: None,
            entered_serial_number: entered_serial_number.to_string(),
            entered_mac_address: entered_mac_address.to_string(),
            verify_result: DeviceVerifyResult::DeviceInfoNotAvailable,
            can_proceed_to_execution_confirm: false,
            audit_label: "device_verify_failed:deviceInfoNotAvailable".to_string(),
        };
    };

    // シリアル番号の照合（大文字/小文字・前後空白を正規化して比較）
    let serial_ok = normalize_id(&info.serial_number) == normalize_id(entered_serial_number);
    if !serial_ok {
        return DeviceSelectScreenState {
            device_info: Some(info.clone()),
            entered_serial_number: entered_serial_number.to_string(),
            entered_mac_address: entered_mac_address.to_string(),
            verify_result: DeviceVerifyResult::SerialMismatch,
            can_proceed_to_execution_confirm: false,
            audit_label: format!(
                "device_verify_failed:serialMismatch expected={} entered={}",
                info.serial_number, entered_serial_number
            ),
        };
    }

    // MAC アドレスの照合（大文字/小文字・コロン・ハイフンを正規化して比較）
    let mac_ok = normalize_mac(&info.mac_address) == normalize_mac(entered_mac_address);
    if !mac_ok {
        return DeviceSelectScreenState {
            device_info: Some(info.clone()),
            entered_serial_number: entered_serial_number.to_string(),
            entered_mac_address: entered_mac_address.to_string(),
            verify_result: DeviceVerifyResult::MacMismatch,
            can_proceed_to_execution_confirm: false,
            audit_label: format!(
                "device_verify_failed:macMismatch expected={} entered={}",
                info.mac_address, entered_mac_address
            ),
        };
    }

    DeviceSelectScreenState {
        device_info: Some(info.clone()),
        entered_serial_number: entered_serial_number.to_string(),
        entered_mac_address: entered_mac_address.to_string(),
        verify_result: DeviceVerifyResult::Confirmed,
        can_proceed_to_execution_confirm: true,
        audit_label: format!(
            "device_verify_success serial={} mac={}",
            info.serial_number, info.mac_address
        ),
    }
}

/// 識別子文字列を正規化します（前後空白除去・大文字化）。
fn normalize_id(value: &str) -> String {
    value.trim().to_uppercase()
}

/// MAC アドレス文字列を正規化します（コロン・ハイフン除去・大文字化）。
fn normalize_mac(value: &str) -> String {
    value
        .trim()
        .to_uppercase()
        .replace(':', "")
        .replace('-', "")
}

/// 現フェーズ用のプレースホルダー対象機情報を CLI 入力から構築します。
///
/// [将来対応] 本番実装では SecretCore IPC 経由で自動取得する。
/// 現フェーズは CLI 手動入力で代用する。
pub fn build_placeholder_device_info() -> DeviceInfo {
    use std::io::{self, Write};

    println!("--- PT-003 対象機情報入力（現フェーズ: 手動入力） ---");
    println!("[注意] 現フェーズでは接続中の実機情報を手動入力します。");

    print!("シリアル番号: ");
    io::stdout().flush().unwrap_or_default();
    let mut serial = String::new();
    io::stdin().read_line(&mut serial).unwrap_or_default();

    print!("MAC アドレス (例: AA:BB:CC:DD:EE:FF): ");
    io::stdout().flush().unwrap_or_default();
    let mut mac = String::new();
    io::stdin().read_line(&mut mac).unwrap_or_default();

    print!("public_id (わからない場合は 'unknown' と入力): ");
    io::stdout().flush().unwrap_or_default();
    let mut public_id = String::new();
    io::stdin().read_line(&mut public_id).unwrap_or_default();

    print!("FW 版数 (例: 1.1.0-beta.15): ");
    io::stdout().flush().unwrap_or_default();
    let mut fw_version = String::new();
    io::stdin().read_line(&mut fw_version).unwrap_or_default();

    DeviceInfo {
        serial_number: serial.trim().to_string(),
        mac_address: mac.trim().to_string(),
        public_id: public_id.trim().to_string(),
        firmware_version: fw_version.trim().to_string(),
        secure_status_label: "未確認（現フェーズ手動入力）".to_string(),
    }
}

/// CLI での対象機入力プロンプトを表示し、サンプル識別情報と再入力を受け付けます。
///
/// # 引数
/// - `device_info`: 操作者へ提示する対象機識別情報
///
/// # 戻り値
/// `(entered_serial, entered_mac)`
pub fn prompt_device_verification(device_info: &DeviceInfo) -> (String, String) {
    use std::io::{self, Write};

    println!("--- PT-003 対象機確認 ---");
    println!("  シリアル番号: {}", device_info.serial_number);
    println!("  MAC アドレス: {}", device_info.mac_address);
    println!("  public_id   : {}", device_info.public_id);
    println!("  FW 版数     : {}", device_info.firmware_version);
    println!("  セキュア状態: {}", device_info.secure_status_label);
    println!("上記の対象機を確認してください。シリアル番号と MAC アドレスを再入力してください。");

    print!("シリアル番号 (再入力): ");
    io::stdout().flush().unwrap_or_default();
    let mut serial = String::new();
    io::stdin().read_line(&mut serial).unwrap_or_default();

    print!("MAC アドレス (再入力): ");
    io::stdout().flush().unwrap_or_default();
    let mut mac = String::new();
    io::stdin().read_line(&mut mac).unwrap_or_default();

    (serial.trim().to_string(), mac.trim().to_string())
}
