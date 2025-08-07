# C++ ラムダ式 vs 従来の関数オブジェクト比較ガイド

## 📋 概要
このサンプルでは、C/C++03 時代に一般的だった **関数ポインタ / ファンクタ** と、
C++11 以降で導入された **ラムダ式** の書き方・柔軟性・可読性を比較します。

| テクニック | 記述量 | 状態保持 | 型推論 | 例外安全 | 推奨度 |
|-------------|--------|----------|--------|-----------|--------|
| 関数ポインタ | 少 | ✕ (外部状態を持てない) | N/A | △ | 🌟 |
| ファンクタ(関数オブジェクト) | 中 | ○ (メンバで保持) | ✕ | ○ | 🌟🌟 |
| ラムダ式 + `std::function` | 最少 | ◎ (キャプチャ) | ◎ | ○ | 🌟🌟🌟🌟🌟 |

## 📂 ファイル一覧
- `lambda_vs_function.cpp` : ソースコード本体

## 🛠️ コンパイル例 (MSVC x64)
```powershell
cl /utf-8 /EHsc /std:c++17 lambda_vs_function.cpp /Fe:lambda_vs_function.exe
./lambda_vs_function.exe
```

## 🔍 実験項目
1. **関数ポインタ**: `bool isEven(int)` → 偶数抽出
2. **ファンクタ**: `struct IsMultipleOf` → 任意の倍数抽出
3. **ラムダ + std::function**: キャプチャ無し (`>= 5`)
4. **ジェネリックラムダ**: `auto` パラメータ (`偶数 && >=4`)
5. **キャプチャリスト**: 外部変数 `threshold` を参照キャプチャ

コードを編集し、ラムダのキャプチャモード (`[=]`, `[&]`, `[x]`) や
`std::function` を排除してテンプレートにするとパフォーマンスがどう変わるか等、
様々な観点で試してみてください。

## 📚 参考リンク
- *Effective Modern C++* – Item 31「ラムダ式の使い方」
- *C++ Core Guidelines* – F.2, F.4, C.46 など
- cppreference – 〈https://en.cppreference.com/w/cpp/language/lambda〉
