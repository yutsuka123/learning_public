/**
 * @file cpp23.cpp
 * @brief [重要] C++23 学習用サンプル（単体コンパイル前提）。
 * @details
 * - 目的: 「C++23 を指定してビルドできているか」を確認しつつ、利用可能な C++23 機能を安全に試す。
 * - 方針: **機能テストマクロ**（`__cpp_...`）で対応状況を確認し、未対応でもコンパイルが通るようにする。
 *
 * @note [厳守] C++23（または MSVC の /std:c++latest）でコンパイルすること。
 * @note [推奨] 未対応マクロがあっても焦らず、コンパイラ/標準ライブラリのバージョン差だと理解する。
 */

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
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
    case 202302L:
      return "202302L (C++23)";
    case 202002L:
      return "202002L (C++20)";
    case 201703L:
      return "201703L (C++17)";
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
 * @brief 機能テストマクロを表示します。
 * @param macroName std::string マクロ名
 * @param macroValue long long 値（未定義の場合は -1）
 * @return void
 */
void printFeatureMacro(const std::string& macroName, const long long macroValue) {
  if (macroValue < 0) {
    std::cout << "- " << macroName << ": (not defined)\n";
  } else {
    std::cout << "- " << macroName << ": " << macroValue << "\n";
  }
}

#if defined(__cpp_if_consteval)
/**
 * @brief `if consteval`（C++23）の例。
 * @details
 * - コンパイル時評価のときと、実行時評価のときで挙動を変える。
 * @param x int 入力
 * @return int 出力
 */
constexpr int computeWithIfConsteval(const int x) {
  if consteval {
    return x * 2;
  } else {
    return x * 3;
  }
}
#endif

#if defined(__cpp_explicit_this_parameter)
/**
 * @brief 明示的オブジェクト引数（explicit object parameter）の例。
 * @details `this` を引数として受け取ることで、参照修飾などを明示できる。
 */
struct simpleCounter {
  int value = 0;

  /**
   * @brief カウンタに加算します。
   * @param self simpleCounter& 自分自身（明示的）
   * @param delta int 加算量
   * @return void
   */
  void add(this simpleCounter& self, const int delta) {
    self.value += delta;
  }
};
#endif

/**
 * @brief C++23 サンプルを実行します。
 * @return void
 */
void runCpp23Samples() {
  printTitle("C++23 samples");
  std::cout << "reported standard: " << toCppStandardLabel(getReportedCppValue()) << "\n";
  std::cout << "__cplusplus: " << toCppStandardLabel(static_cast<long long>(__cplusplus)) << "\n";
#if defined(_MSVC_LANG)
  std::cout << "_MSVC_LANG: " << toCppStandardLabel(static_cast<long long>(_MSVC_LANG)) << "\n";
#endif

  printTitle("feature test macros (availability)");
#if defined(__cpp_if_consteval)
  printFeatureMacro("__cpp_if_consteval", static_cast<long long>(__cpp_if_consteval));
#else
  printFeatureMacro("__cpp_if_consteval", -1);
#endif

#if defined(__cpp_explicit_this_parameter)
  printFeatureMacro("__cpp_explicit_this_parameter", static_cast<long long>(__cpp_explicit_this_parameter));
#else
  printFeatureMacro("__cpp_explicit_this_parameter", -1);
#endif

#if defined(__cpp_multidimensional_subscript)
  printFeatureMacro("__cpp_multidimensional_subscript", static_cast<long long>(__cpp_multidimensional_subscript));
#else
  printFeatureMacro("__cpp_multidimensional_subscript", -1);
#endif

  printTitle("demonstrations");

#if defined(__cpp_if_consteval)
  // コンパイル時評価（constexpr）
  constexpr int compileTimeValue = computeWithIfConsteval(10);
  std::cout << "computeWithIfConsteval(10) at compile-time -> " << compileTimeValue << "\n";

  // 実行時評価
  const int runtimeInput = 10;
  const int runtimeValue = computeWithIfConsteval(runtimeInput);
  std::cout << "computeWithIfConsteval(10) at run-time -> " << runtimeValue << "\n";
#else
  std::cout << "[info] if consteval is not available on this compiler.\n";
#endif

#if defined(__cpp_explicit_this_parameter)
  simpleCounter counter;
  counter.add(5);
  counter.add(7);
  std::cout << "simpleCounter.value=" << counter.value << "\n";
#else
  std::cout << "[info] explicit this parameter is not available on this compiler.\n";
#endif
}

}  // namespace

/**
 * @brief エントリポイント（C++23）。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return int 終了コード
 */
int main(const int argc, char** argv) {
  try {
    std::cout << "[cpp23] args: " << joinArgs(argc, argv) << "\n";
    runCpp23Samples();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[error] function=main(file=cpp23.cpp)"
              << " message=\"" << ex.what() << "\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 1;
  } catch (...) {
    std::cerr << "[error] function=main(file=cpp23.cpp)"
              << " message=\"unknown exception\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 2;
  }
}

