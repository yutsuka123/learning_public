# Rust インストール/ビルド手順

[重要] 本フォルダは Rust の学習用サンプルです。
[推奨] Rust は rustup で導入します。理由: 安定して更新・ツールチェイン管理できるため。

## 前提
- OS: Windows 10/11 または macOS/Linux
- 必要ツール: `cargo`, `rustc`

## インストール

### Windows
1. rustup をインストールします。
- 公式: `https://www.rust-lang.org/tools/install`

2. 端末を開き、次を確認します。

```powershell
rustc --version
cargo --version
```

### macOS/Linux
```sh
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs -o /tmp/rustup-init.sh
sh /tmp/rustup-init.sh -y --default-toolchain stable --profile default
source "$HOME/.cargo/env"   # 現在のシェルにPATHを反映（新しいシェルなら不要）

rustc --version
cargo --version
```

## ビルド

```powershell
cargo build --manifest-path .\hello_world\Cargo.toml
```

```sh
cargo build --manifest-path hello_world/Cargo.toml
```

## 実行

```powershell
cargo run --manifest-path .\hello_world\Cargo.toml
cargo run --manifest-path .\hello_world\Cargo.toml -- YourName
```

```sh
cargo run --manifest-path hello_world/Cargo.toml
cargo run --manifest-path hello_world/Cargo.toml -- YourName
```

## 変更履歴
- 2026-08-03: macOS/Linux向けのrustupインストール手順を追記。`basics_ownership_borrowing`サンプル追加に伴う
  （このマシンにcargo/rustcが未導入だったため、rustup公式スクリプト経由で導入した実績を記録）。
- 2025-12-30: Rust の HelloWorld サンプルを新規追加（学習用の入口を統一するため）。
