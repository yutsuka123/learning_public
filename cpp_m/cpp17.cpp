/**
 * @file cpp17.cpp
 * @brief [重要] C++17 学習用サンプル（単体コンパイル前提）。
 * @details
 * - 目的: C++17 の代表的機能を、実務でよく出る形（分岐・パース・戻り値）で確認する。
 * - 主な題材: 構造化束縛 / `if constexpr` / `std::optional` / `std::string_view`
 *
 * @note [厳守] C++17 以上でコンパイルすること（例: clang++ -std=c++17 / g++ -std=c++17）。
 */

#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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
 * @brief 文字列を int に変換します（失敗時は std::nullopt）。
 * @param text std::string_view 変換対象
 * @return std::optional<int> 成功時は値、失敗時は空
 */
std::optional<int> parseInt(const std::string_view text) {
  try {
    std::string owned(text);  // stoi は std::string を受け取るため
    size_t parsedLength = 0;
    const int value = std::stoi(owned, &parsedLength, 10);
    if (parsedLength != owned.size()) {
      return std::nullopt;  // 途中に非数値が混ざっている
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

/**
 * @brief `if constexpr` の例（型に応じた分岐をコンパイル時に行う）。
 * @tparam T 任意型
 * @param value T 値
 * @return std::string カテゴリ文字列
 */
template <typename T>
std::string getTypeCategory(const T& value) {
  (void)value;
  if constexpr (std::is_integral_v<T>) {
    return "integral";
  } else if constexpr (std::is_floating_point_v<T>) {
    return "floating";
  } else {
    return "other";
  }
}

/**
 * @brief 構造化束縛（structured bindings）の例。
 * @return void
 */
void demonstrateStructuredBindings() {
  printTitle("structured bindings");

  const std::pair<int, std::string> user{42, "alice"};
  const auto [id, name] = user;  // C++17
  std::cout << "id=" << id << " name=" << name << "\n";
}

/**
 * @brief `std::optional` と `std::string_view` の組み合わせ例。
 * @return void
 */
void demonstrateOptionalAndStringView() {
  printTitle("optional / string_view");

  const std::vector<std::string> inputs{"123", "45x", "007"};
  for (const auto& input : inputs) {
    const std::optional<int> valueOpt = parseInt(input);
    if (valueOpt.has_value()) {
      std::cout << "input=\"" << input << "\" -> value=" << *valueOpt << "\n";
    } else {
      std::cout << "input=\"" << input << "\" -> parse failed\n";
    }
  }
}

/**
 * @brief `if constexpr` を実感するための出力例。
 * @return void
 */
void demonstrateIfConstexpr() {
  printTitle("if constexpr");
  std::cout << "category(int)=" << getTypeCategory(1) << "\n";
  std::cout << "category(double)=" << getTypeCategory(3.14) << "\n";
  std::cout << "category(string)=" << getTypeCategory(std::string("x")) << "\n";
}

/**
 * @brief C++17 サンプル全体の実行。
 * @return void
 */
void runCpp17Samples() {
  printTitle("C++17 samples");
  std::cout << "reported standard: " << toCppStandardLabel(getReportedCppValue()) << "\n";
  std::cout << "__cplusplus: " << toCppStandardLabel(static_cast<long long>(__cplusplus)) << "\n";
#if defined(_MSVC_LANG)
  std::cout << "_MSVC_LANG: " << toCppStandardLabel(static_cast<long long>(_MSVC_LANG)) << "\n";
#endif

  demonstrateStructuredBindings();
  demonstrateIfConstexpr();
  demonstrateOptionalAndStringView();
}

}  // namespace

/**
 * @brief エントリポイント（C++17）。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return int 終了コード
 */
int main(const int argc, char** argv) {
  try {
    std::cout << "[cpp17] args: " << joinArgs(argc, argv) << "\n";
    runCpp17Samples();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[error] function=main(file=cpp17.cpp)"
              << " message=\"" << ex.what() << "\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 1;
  } catch (...) {
    std::cerr << "[error] function=main(file=cpp17.cpp)"
              << " message=\"unknown exception\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 2;
  }
}

