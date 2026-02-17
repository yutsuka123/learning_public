//! Rust HelloWorld サンプル
//!
//! [重要] 学習用の最小サンプルです。
//! [厳守] 可読性を優先し、処理を省略しない方針で記述します。理由: 初学者が追いやすくするため。
//! [推奨] エラー時は「関数名・引数」を含む詳細メッセージを出します。理由: デバッグ容易性向上のため。
//!
//! ## 仕様
//! - `cargo run -- [name]` で挨拶を表示します。
//! - 引数が2個以上（name を複数指定）はエラーにします。
//!
//! ## 制限事項
//! - 国際化(i18n)は未対応（学習用のため）。

use std::error::Error;
use std::fmt;

/// 引数が仕様に合わない場合のエラー。
///
/// [重要] Rust の慣習は snake_case ですが、このリポジトリの命名規則（lowerCamelCase）を優先するため
/// `non_snake_case` の警告を抑止します。理由: リポジトリ内の命名を統一するため。
#[allow(non_snake_case)]
#[derive(Debug, Clone)]
pub struct InvalidArgumentsError {
    /// エラーが発生した関数名。
    pub functionName: &'static str,

    /// 受け取った引数一覧（プログラム名を含む）。
    pub args: Vec<String>,

    /// 期待していた引数の形式。
    pub expectedUsage: &'static str,
}

impl fmt::Display for InvalidArgumentsError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Invalid arguments in {functionName}.\n  expected: {expectedUsage}\n  actual args({argc}): {args:?}",
            functionName = self.functionName,
            expectedUsage = self.expectedUsage,
            argc = self.args.len(),
            args = self.args
        )
    }
}

impl Error for InvalidArgumentsError {}

/// コマンドライン引数から表示名(name)を取り出します。
///
/// [重要] name は 0〜1 個のみ許可します。
///
/// ## 引数
/// - `args`: `std::env::args()` の結果を `Vec<String>` にしたもの（プログラム名を含む）
///
/// ## 戻り値
/// - `Ok(String)`: 表示名（未指定なら "World"）
/// - `Err(InvalidArgumentsError)`: 引数が2個以上指定された等、仕様違反
#[allow(non_snake_case)]
pub fn parseNameFromArgs(args: &[String]) -> Result<String, InvalidArgumentsError> {
    const FUNCTION_NAME: &str = "parseNameFromArgs";
    const EXPECTED_USAGE: &str = "cargo run -- [name]";

    // args[0] はプログラム名
    // args[1] が name
    // args[2] 以降がある場合は仕様違反
    if args.len() >= 3 {
        return Err(InvalidArgumentsError {
            functionName: FUNCTION_NAME,
            args: args.to_vec(),
            expectedUsage: EXPECTED_USAGE,
        });
    }

    let name = match args.get(1) {
        Some(value) if !value.trim().is_empty() => value.clone(),
        _ => "World".to_string(),
    };

    Ok(name)
}

/// エントリポイント。
///
/// [重要] エラー発生時は標準エラーに詳細を出力します。
pub fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = std::env::args().collect();

    let name = parseNameFromArgs(&args).map_err(|error| {
        eprintln!("[ERROR] {}", error);
        error
    })?;

    println!("Hello, {}!", name);
    Ok(())
}
