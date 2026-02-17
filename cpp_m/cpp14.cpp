/**
 * @file cpp14.cpp
 * @brief [重要] C++14 学習用サンプル（単体コンパイル前提）。
 * @details
 * - 目的: C++14 の代表的な改善点を「C++11 と比較しながら」確認する。
 * - 主な題材: ジェネリックラムダ / `std::make_unique` / 返り値型推論 / 桁区切り（digit separators）
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
  const auto messagePtr = std::make_unique<std::string>("Hello from make_unique");
  std::cout << "*messagePtr=" << *messagePtr << "\n";
}

/**
 * @brief ジェネリックラムダ（引数型に auto を使える）を確認します。
 * @return void
 */
void demonstrateGenericLambda() {
  printTitle("generic lambda (auto parameters)");

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
  demonstrateReturnTypeDeduction();
  demonstrateDigitSeparators();
}

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

