# Rust サンプル

[重要] このフォルダは Rust の最小サンプルを置きます。

## サンプル一覧
- `hello_world/`: HelloWorld（引数で名前を指定できます）
- `basics_ownership_borrowing/`: Rustの所有権(ownership)・借用(borrowing)基礎。
  move by default、`&T`/`&mut T`、スライス、構造体とimpl、`Option`/`Result`とmatch、
  `?`演算子、trait、`Vec<T>`、`Box`/`Rc`/`Weak`を、`cpp_m/cpp_basics_class_memory_ownership.cpp`の
  `unique_ptr`/`shared_ptr`/`weak_ptr`と対比しながら確認します。

## クイックスタート

```powershell
cargo build --manifest-path .\hello_world\Cargo.toml
cargo run --manifest-path .\hello_world\Cargo.toml -- World
```

```sh
# basics_ownership_borrowing (macOS/Linux)
cd rust/basics_ownership_borrowing
cargo run
cargo clippy   # 追加の静的解析（任意）
```
