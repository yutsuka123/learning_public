/**
 * @file cpp20.cpp
 * @brief [重要] C++20 学習用サンプル（単体コンパイル前提）。
 * @details
 * - 目的: C++20 の中でも「読みやすさに直結」する機能を最小例で確認する。
 * - 主な題材: `concepts` / `std::span` / `constinit`
 *
 * @note [厳守] C++20 以上でコンパイルすること（例: clang++ -std=c++20 / g++ -std=c++20）。
 */

#include <array>
#include <concepts>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <span>
#include <type_traits>
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

// [重要] constinit は「静的記憶域の初期化がコンパイル時に行える」ことを保証する。
// [推奨] 初期化順問題の対策として有用（ただし万能ではない）。
constinit int globalCounter = 0;

/**
 * @brief 足し算可能な「整数型」を表す concept。
 * @tparam T 任意型
 */
template <typename T>
concept AddableIntegral = std::integral<T>;

/**
 * @brief concept を使った関数（型制約を読みやすく表現）。
 * @tparam T AddableIntegral を満たす型
 * @param a T 値1
 * @param b T 値2
 * @return T 和
 */
template <AddableIntegral T>
T addValues(const T a, const T b) {
  return static_cast<T>(a + b);
}

/**
 * @brief std::span を使って「配列/ベクタなど連続領域」を同じAPIで扱います。
 * @param values std::span<const int> 参照する連続領域
 * @return int 合計
 */
int sumSpan(const std::span<const int> values) {
  int sum = 0;
  for (const int v : values) {
    sum += v;
  }
  return sum;
}

/**
 * @brief C++20 サンプルを実行します。
 * @return void
 */
void runCpp20Samples() {
  printTitle("C++20 samples");
  std::cout << "reported standard: " << toCppStandardLabel(getReportedCppValue()) << "\n";
  std::cout << "__cplusplus: " << toCppStandardLabel(static_cast<long long>(__cplusplus)) << "\n";
#if defined(_MSVC_LANG)
  std::cout << "_MSVC_LANG: " << toCppStandardLabel(static_cast<long long>(_MSVC_LANG)) << "\n";
#endif

  printTitle("constinit");
  std::cout << "globalCounter(before)=" << globalCounter << "\n";
  globalCounter += 1;
  std::cout << "globalCounter(after)=" << globalCounter << "\n";

  printTitle("concepts");
  const int x = addValues<int>(10, 20);
  std::cout << "addValues<int>(10,20)=" << x << "\n";

  printTitle("std::span");
  const std::array<int, 5> arrayValues{1, 2, 3, 4, 5};
  const std::vector<int> vectorValues{10, 20, 30};

  std::cout << "sumSpan(array)=" << sumSpan(std::span<const int>(arrayValues)) << "\n";
  std::cout << "sumSpan(vector)=" << sumSpan(std::span<const int>(vectorValues.data(), vectorValues.size())) << "\n";
}

}  // namespace

/**
 * @brief エントリポイント（C++20）。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return int 終了コード
 */
int main(const int argc, char** argv) {
  try {
    std::cout << "[cpp20] args: " << joinArgs(argc, argv) << "\n";
    runCpp20Samples();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[error] function=main(file=cpp20.cpp)"
              << " message=\"" << ex.what() << "\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 1;
  } catch (...) {
    std::cerr << "[error] function=main(file=cpp20.cpp)"
              << " message=\"unknown exception\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 2;
  }
}

