/**
 * @file cpp11.cpp
 * @brief [重要] C++11 学習用サンプル（単体コンパイル前提）。
 * @details
 * - 目的: C++11 の代表的な言語機能を「小さく・確実に」確認する。
 * - 主な題材: `auto` / 範囲for / `nullptr` / `enum class` / `unique_ptr` / ラムダ / move
 *
 * @note [厳守] C++11 以上でコンパイルすること（例: clang++ -std=c++11 / g++ -std=c++11）。理由: 学習対象機能が有効になるため。
 * @note [推奨] 例外発生時の出力を読み、どの関数で失敗したかを追う。理由: 実務でのデバッグに直結するため。
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
 * @brief 代表的な `__cplusplus` 値をラベル化します。
 * @param value long long `__cplusplus` または `_MSVC_LANG` の値
 * @return std::string ラベル（例: "201103L (C++11)"）
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
 * @brief MSVC を考慮して「報告される標準値」を取得します。
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
 * @brief `enum class` の例（スコープ付き列挙型）。
 */
enum class colorKind {
  red = 1,
  green = 2,
  blue = 3,
};

/**
 * @brief `enum class` を文字列へ変換します。
 * @param kind colorKind 色種別
 * @return std::string 表示名
 */
std::string toString(const colorKind kind) {
  switch (kind) {
    case colorKind::red:
      return "red";
    case colorKind::green:
      return "green";
    case colorKind::blue:
      return "blue";
    default:
      return "unknown";
  }
}

/**
 * @brief `unique_ptr` と move の基本を確認します。
 * @return void
 */
void demonstrateUniquePtrAndMove() {
  printTitle("unique_ptr / move");

  std::unique_ptr<std::string> messagePtr(new std::string("Hello from unique_ptr"));
  std::cout << "messagePtr points to: " << *messagePtr << "\n";

  // [重要] unique_ptr はコピーできない（所有権が一意）。
  // [推奨] 所有権移動を明示するため std::move を使う。
  std::unique_ptr<std::string> movedPtr = std::move(messagePtr);

  std::cout << "after move:\n";
  std::cout << "- messagePtr is " << (messagePtr ? "not null" : "null") << "\n";
  std::cout << "- movedPtr is " << (movedPtr ? "not null" : "null") << "\n";
  if (!movedPtr) {
    throw std::runtime_error("demonstrateUniquePtrAndMove: movedPtr is null (unexpected)");
  }
  std::cout << "- movedPtr value: " << *movedPtr << "\n";
}

/**
 * @brief `auto` / 範囲for / 初期化子リストの例を確認します。
 * @return void
 */
void demonstrateAutoAndRangeFor() {
  printTitle("auto / range-for / initializer_list");

  const std::vector<int> numbers{1, 2, 3, 4, 5};

  // [重要] auto は「型推論」だが、読みやすさを損なう場合は明示型を優先する。
  auto sum = 0;
  for (const auto value : numbers) {  // 範囲for
    sum += value;
  }
  std::cout << "sum=" << sum << "\n";
}

/**
 * @brief `nullptr` とオーバーロード解決の例。
 * @return void
 */
void demonstrateNullptr() {
  printTitle("nullptr");

  // 例: 0 や NULL は整数/ポインタで曖昧になる可能性がある。
  // nullptr は「ヌルポインタリテラル」として安全。
  std::string* ptr = nullptr;
  std::cout << "ptr is " << (ptr == nullptr ? "nullptr" : "not null") << "\n";
}

/**
 * @brief ラムダ式の例（キャプチャ、引数、戻り値）。
 * @return void
 */
void demonstrateLambda() {
  printTitle("lambda");

  const int baseValue = 10;
  const auto addBase = [baseValue](const int x) -> int { return baseValue + x; };

  std::cout << "addBase(5)=" << addBase(5) << "\n";
}

/**
 * @brief C++11 サンプル全体の実行。
 * @return void
 */
void runCpp11Samples() {
  printTitle("C++11 samples");
  std::cout << "reported standard: " << toCppStandardLabel(getReportedCppValue()) << "\n";
  std::cout << "__cplusplus: " << toCppStandardLabel(static_cast<long long>(__cplusplus)) << "\n";
#if defined(_MSVC_LANG)
  std::cout << "_MSVC_LANG: " << toCppStandardLabel(static_cast<long long>(_MSVC_LANG)) << "\n";
#endif

  demonstrateAutoAndRangeFor();
  demonstrateNullptr();
  demonstrateLambda();
  demonstrateUniquePtrAndMove();

  printTitle("enum class");
  const colorKind favorite = colorKind::green;
  std::cout << "favorite=" << toString(favorite) << "\n";
}

}  // namespace

/**
 * @brief エントリポイント（C++11）。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return int 終了コード
 */
int main(const int argc, char** argv) {
  try {
    (void)argc;
    (void)argv;
    std::cout << "[cpp11] args: " << joinArgs(argc, argv) << "\n";
    runCpp11Samples();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[error] function=main(file=cpp11.cpp)"
              << " message=\"" << ex.what() << "\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 1;
  } catch (...) {
    std::cerr << "[error] function=main(file=cpp11.cpp)"
              << " message=\"unknown exception\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 2;
  }
}

