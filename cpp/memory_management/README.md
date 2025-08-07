# C++ メモリ管理 比較ガイド

## 📋 概要
本ディレクトリでは、C++ における **現代的 (C++11 以降)** なメモリ管理手法と、
10〜20 年前に一般的だった **旧式 (手動) メモリ管理** を比較学習するための
サンプルコードを提供します。

| サンプル | 主な内容 | 推奨コンパイラ |
|----------|----------|----------------|
| `modern_memory.cpp` | `std::unique_ptr` / `std::shared_ptr` / `std::weak_ptr` による RAII・例外安全 | MSVC / GCC / Clang (C++17) |
| `legacy_memory.cpp` | `new` / `delete`, `malloc` / `free` を用いた手動管理 & メモリリーク例 | 同上 |

---
## 🌟 現代的メモリ管理 (RAII & スマートポインタ)
- **RAII**: オブジェクトのライフサイクルに合わせてリソース獲得・解放を行う設計思想
- **`std::unique_ptr`**: 単一所有権。\* 自動で `delete` / `delete[]`
- **`std::shared_ptr`**: 参照カウント共有所有権。循環参照に注意
- **`std::weak_ptr`**: 循環参照を断ち切る弱参照
- 例外が発生しても自動解放されるため **例外安全**

### コンパイル例
```powershell
cl /utf-8 /EHsc /std:c++17 modern_memory.cpp /Fe:modern_memory.exe
./modern_memory.exe
```

---
## 🕰️ 旧式メモリ管理 (手動管理)
- **`new` / `delete`**: C++ 独自の動的メモリ確保 / 解放
- **`malloc` / `free`**: C 由来のメモリ管理 (コンストラクタ/デストラクタ非呼び出し)
- 解放忘れによる **メモリリーク**、例外発生時のリーク、二重解放などの危険性

### コンパイル例
```powershell
cl /utf-8 /EHsc /std:c++17 legacy_memory.cpp /Fe:legacy_memory.exe
./legacy_memory.exe
```

---
## 🔍 学習ポイント比較
| 観点 | 現代的 (C++11〜) | 旧式 (C++03 以前) |
|------|------------------|--------------------|
| リソース管理 | RAII + スマートポインタで自動 | 手動 `new` / `delete` / `malloc` / `free` |
| 例外安全性 | 高い (スコープ退出で自動解放) | 低い (catch 前にリークしやすい) |
| コード量 | 少ない | 多い (try/catch + delete など) |
| パフォーマンス | ほぼ同等（最適化によりオーバーヘッド小） | 明示的管理でチューニング可能だが危険 |
| 推奨度 | 🌟🌟🌟🌟🌟 | 🌟 (学習・レガシー保守用) |

---
## 📚 参考資料
- *Effective Modern C++* (Scott Meyers)
- *C++ Core Guidelines* (https://isocpp.github.io/CppCoreGuidelines/)
- *cppreference.com* (https://en.cppreference.com/)
