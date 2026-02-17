/**
 * @file cpp14.cpp
 * @brief [重要] C++14 学習用サンプル（単体コンパイル前提）。
 * @details
 * - 目的: C++14 の代表的な改善点を「C++11 と比較しながら」確認する。
 * - 主な題材: ジェネリックラムダ / `std::make_unique` / 返り値型推論 / 桁区切り（digit separators）
 * - C言語経験者向け補足:
 *   - C++11 でも `unique_ptr` は使えますが、**C++14 から `std::make_unique`** が入り、生成が安全・簡単になりました。
 *   - 「new を直接書く」よりも「make_* で生成する」方が、例外が絡む場面で安全になりやすいです。
 *
 * @note [厳守] C++14 以上でコンパイルすること（例: clang++ -std=c++14 / g++ -std=c++14）。理由: 学習対象機能が有効になるため。
 */

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

/**
 * @brief コマンドライン引数を連結して表示用文字列にします。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return std::string 連結した引数
 */
std::string joinArgs(const int argc, char** argv) {
  std::ostringstream oss;
  for (int i = 0; i < argc; i++) {
    if (i != 0) {
      oss << ' ';
    }
    oss << (argv[i] != nullptr ? argv[i] : "(null)");
  }
  return oss.str();
}

/**
 * @brief `__cplusplus` 値を人間向けに整形します。
 * @param value long long 値
 * @return std::string ラベル
 */
std::string toCppStandardLabel(const long long value) {
  switch (value) {
    case 201103L:
      return "201103L (C++11)";
    case 201402L:
      return "201402L (C++14)";
    case 201703L:
      return "201703L (C++17)";
    case 202002L:
      return "202002L (C++20)";
    case 202302L:
      return "202302L (C++23)";
    default: {
      std::ostringstream oss;
      oss << value << " (未判定/実装依存)";
      return oss.str();
    }
  }
}

/**
 * @brief コンパイラが報告する標準値を取得します（MSVC では `_MSVC_LANG` を優先）。
 * @return long long 標準値
 */
long long getReportedCppValue() {
#if defined(_MSVC_LANG)
  return static_cast<long long>(_MSVC_LANG);
#else
  return static_cast<long long>(__cplusplus);
#endif
}

/**
 * @brief 見出しを表示します。
 * @param title std::string 見出し
 * @return void
 */
void printTitle(const std::string& title) {
  std::cout << "\n=== " << title << " ===\n";
}

/**
 * @brief C++14 の `std::make_unique` を確認します。
 * @return void
 */
void demonstrateMakeUnique() {
  printTitle("std::make_unique");

  // [重要] C++11 では make_unique が無いので new を書きがちだった。
  // C++14 では make_unique により安全な生成が簡単になる。
  // - C言語経験者向け: 「生成と所有」を一行で書けるので、free忘れ・早期returnでの解放漏れを減らしやすい。
  const auto messagePtr = std::make_unique<std::string>("Hello from make_unique");
  std::cout << "*messagePtr=" << *messagePtr << "\n";
}

/**
 * @brief ジェネリックラムダ（引数型に auto を使える）を確認します。
 * @return void
 */
void demonstrateGenericLambda() {
  printTitle("generic lambda (auto parameters)");

  // ジェネリックラムダ: 引数型に auto を書ける（C++14）。
  // - C言語経験者向け: マクロで型を誤魔化すのではなく、型安全なテンプレートとして扱えるイメージ。
  const auto toString = [](const auto& value) -> std::string {
    std::ostringstream oss;
    oss << value;
    return oss.str();
  };

  std::cout << "toString(123)=" << toString(123) << "\n";
  std::cout << "toString(3.14)=" << toString(3.14) << "\n";
  std::cout << "toString(\"abc\")=" << toString(std::string("abc")) << "\n";
}

/**
 * @brief C++14 のラムダ拡張（初期化キャプチャ/ムーブキャプチャ）を確認します。
 * @details
 * - C++11 までは `[x]` / `[&x]` のような基本キャプチャが中心。
 * - C++14 からは **初期化キャプチャ**により、キャプチャ時に「別名」や「計算結果」を持てます。
 * - さらに `std::move` と組み合わせると **ムーブキャプチャ**が可能で、所有権（unique_ptr等）をラムダへ移せます。
 *
 * 結果の違い（イメージ）:
 * - 初期化キャプチャ: 外側変数が変わっても、キャプチャした値（別名）は変わらない
 * - ムーブキャプチャ: 外側のunique_ptrは空になり、ラムダ側が所有する（= 解放責任が移る）
 * @return void
 */
void demonstrateLambdaInitAndMoveCapture() {
  printTitle("lambda init-capture / move-capture (C++14)");

  // (1) 初期化キャプチャ: computed という名前で「計算結果」を保持する
  int base = 7;
  const auto getComputed = [computed = base * 10]() { return computed; };
  base = 999;
  // expected: computed は 70 のまま（baseの変更は影響しない）
  std::cout << "getComputed()=" << getComputed() << " (expected: 70)\n";

  // (2) ムーブキャプチャ: unique_ptr の所有権をラムダへ移す
  auto ptr = std::make_unique<std::string>("owned by lambda");
  const auto useMoved = [moved = std::move(ptr)]() -> std::string {
    return moved ? *moved : "(null)";
  };
  std::cout << "after move: ptr is " << (ptr ? "not null" : "null") << " (expected: null)\n";
  std::cout << "useMoved()=" << useMoved() << " (expected: owned by lambda)\n";
}

/**
 * @brief 返り値型推論（関数の戻り値型を auto で書ける）を確認します。
 * @return void
 */
void demonstrateReturnTypeDeduction() {
  printTitle("return type deduction (auto)");

  // [推奨] 学習では「何が返るのか」をコメントで明示すると理解が早い。
  const auto makeNumbers = []() {
    // return type: std::vector<int>
    return std::vector<int>{1, 2, 3};
  };

  const auto numbers = makeNumbers();
  std::cout << "numbers size=" << numbers.size() << "\n";
}

/**
 * @brief 桁区切り（digit separators）を確認します。
 * @return void
 */
void demonstrateDigitSeparators() {
  printTitle("digit separators");

  // C++14: 123'456 のように読みやすく区切れる。
  const int largeNumber = 123'456;
  std::cout << "largeNumber=" << largeNumber << "\n";
}

/**
 * @brief C++14 サンプル全体の実行。
 * @return void
 */
void runCpp14Samples() {
  printTitle("C++14 samples");
  std::cout << "reported standard: " << toCppStandardLabel(getReportedCppValue()) << "\n";
  std::cout << "__cplusplus: " << toCppStandardLabel(static_cast<long long>(__cplusplus)) << "\n";
#if defined(_MSVC_LANG)
  std::cout << "_MSVC_LANG: " << toCppStandardLabel(static_cast<long long>(_MSVC_LANG)) << "\n";
#endif

  demonstrateMakeUnique();
  demonstrateGenericLambda();
  demonstrateLambdaInitAndMoveCapture();
  demonstrateReturnTypeDeduction();
  demonstrateDigitSeparators();
}

#if 0
// ---------------------------------------------------------------------------
// [悪い例/良い例] よくある間違いの対比（ビルドが通らない例は #if 0 に閉じ込める）
// ---------------------------------------------------------------------------
//
// (A) C++14 機能を C++11 でビルドしてしまう
// [悪い例] C++11 でコンパイルすると `std::make_unique` が無い（多くの環境でコンパイルエラー）
// auto p = std::make_unique<std::string>("x");  // <- C++14 以降
//
// [良い例] `cpp14.cpp` は C++14 でビルドする（`INSTALL.md` の /std:c++14）
//
// (B) ジェネリックラムダを C++11 で使う
// [悪い例] 引数に auto を書けるのは C++14 から（C++11 だとコンパイルエラー）
// auto f = [](const auto& x) { return x; };
//
// [良い例] C++11 ではテンプレート関数にする（C++14 ならジェネリックラムダでOK）
// template <typename T>
// T f(const T& x) { return x; }
//
// (C) 「短く書く（コード量優先）」のやり過ぎ
// [悪い例] 何をしているかが伝わらず、保守が辛くなる
// auto v = []{ return std::vector<int>{1,2,3}; }();  // 短いが意図が薄い
//
// [良い例] 名前を付けて意図を残す（メンテ優先）
// const auto numbers = makeNumbers();
#endif

}  // namespace

/**
 * @brief エントリポイント（C++14）。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return int 終了コード
 */
int main(const int argc, char** argv) {
  try {
    std::cout << "[cpp14] args: " << joinArgs(argc, argv) << "\n";
    runCpp14Samples();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[error] function=main(file=cpp14.cpp)"
              << " message=\"" << ex.what() << "\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 1;
  } catch (...) {
    std::cerr << "[error] function=main(file=cpp14.cpp)"
              << " message=\"unknown exception\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 2;
  }
}

