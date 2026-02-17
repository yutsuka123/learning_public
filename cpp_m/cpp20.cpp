/**
 * @file cpp20.cpp
 * @brief [重要] C++20 学習用サンプル（単体コンパイル前提）。
 * @details
 * - 目的: C++20 の中でも「読みやすさに直結」する機能を最小例で確認する。
 * - 主な題材: `concepts` / `std::span` / `constinit`
 * - C言語経験者向け補足:
 *   - `std::span<T>` は「ポインタ + 長さ」を安全に束ねた参照ビューです。
 *     Cでよくある `int* p, size_t n` の組を、型としてまとめるイメージ。
 *   - `concepts` はテンプレートの「受け付ける型」を入口で明示します。
 *     これにより、C++の難しいテンプレートエラーが **早い段階で読みやすく**なりやすいです。
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
 * @details
 * - `std::integral<T>` は「整数型」を表す既存のconceptです。
 * - ここでは「整数型だけを受け付ける add 関数」を作りたいので、これを利用します。
 *
 * C言語経験者向け:
 * - Cはテンプレートが無いため「何を受け付けるか」は関数の引数型で自然に決まりますが、
 *   C++テンプレートは何でも受け付けられるため、入口で制約するとエラーが読みやすくなります。
 * @tparam T 任意型
 */
template <typename T>
concept AddableIntegral = std::integral<T>;

/**
 * @brief concept を使った関数（型制約を読みやすく表現）。
 * @details
 * - `template <AddableIntegral T>` と書くことで、呼び出し側に「整数型しか受け付けない」ことを明示できます。
 * - 例えば `addValues<int>(10,20)` はOKです。
 * - 一方、`addValues<double>(1.0,2.0)` のような呼び出しは **コンパイル時に弾かれる**想定です。
 *
 * 結果の例:
 * - addValues<int>(10,20) -> 30
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
 * @details
 * - `std::span<const int>` は「int配列を参照する（所有しない）」型です。
 * - Cの `const int* p` と `size_t n` を1つの引数にまとめたイメージ。
 *
 * 入力と結果の例:
 * - [1,2,3,4,5] -> 15
 * - [10,20,30]  -> 60
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

#if 0
// ---------------------------------------------------------------------------
// [悪い例] concepts が無いと起きやすい「読みにくいエラー」の例（ビルドが通らない）
// ---------------------------------------------------------------------------
//
// template <typename T>
// T addBad(const T a, const T b) { return a + b; }
//
// // [悪い例] addBad は「何でも受ける」ので、想定外の型でも入口で止まらない。
// // その結果、エラーが深い場所で出て理解しにくいことがあります。
// //
// // 例: const char* の + は「文字列連結」ではなく「ポインタ演算」なので、意図と違う/エラーになりやすい。
// // auto x = addBad("a", "b"); // <- コンパイルエラーや意図しない動作の原因
//
// [良い例] concept で入口を制約する（このファイルの addValues / AddableIntegral）
#endif

#if 0
// ---------------------------------------------------------------------------
// [優先事項別] span を使うかどうか（メンテ/速度/コード量）
// ---------------------------------------------------------------------------
//
// [メンテ優先] span:
// - 引数が1つになり、渡し忘れ（sizeの渡し忘れ）や取り違えを減らせる
//
// [速度優先] 生ポインタ + size:
// - 関数呼び出し境界を跨ぐときに最適化が効く/効かないはケースバイケース
// - ただし「速いと思い込む」のは禁止。測って決める。
//
// Cスタイル例:
// int sumCStyle(const int* p, const size_t n) {
//   int sum = 0;
//   for (size_t i = 0; i < n; i++) { sum += p[i]; }
//   return sum;
// }
#endif

/**
 * @brief C++20 サンプルを実行します。
 * @details
 * 実行順:
 * - (1) constinit（静的初期化の保証）
 * - (2) concepts（テンプレートの入口制約）
 * - (3) span（ポインタ+長さを1つの型で扱う）
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

#if 0
// ---------------------------------------------------------------------------
// [悪い例/良い例] よくある間違いの対比（ビルドが通らない例は #if 0 に閉じ込める）
// ---------------------------------------------------------------------------
//
// (A) span の寿命: ビルドは通っても危険、またはビルドが通らない
// [悪い例] 一時オブジェクト（temporary）の vector から span を作って返す/使う（寿命が短い）
// auto bad = std::span<const int>(std::vector<int>{1, 2, 3});  // <- 多くの実装でコンパイルエラー or 非推奨
//
// [良い例] 参照対象（vector/array）の寿命を span より長くする
// std::vector<int> values{1, 2, 3};
// auto ok = std::span<const int>(values.data(), values.size());
//
// (B) concepts を使わずに「何でも受ける」テンプレートにする
// [悪い例] 想定外の型でも通ってしまい、エラーが深い場所で出て読みにくい
// template <typename T>
// T addBad(const T a, const T b) { return a + b; }
//
// [良い例] concept で「受け付ける型」を入口で制約する（このファイルの AddableIntegral）
#endif

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

