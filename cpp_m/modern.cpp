/**
 * @file modern.cpp
 * @brief [重要] modern（最新の総合）サンプル。
 * @details
 * - 目的: C++17〜C++23 の「読みやすさ・安全性・実用性」に寄与する要素を、1つの小さな題材で統合的に練習する。
 * - 題材: `--numbers` で与えた整数列の統計（合計/平均/最小/最大）を計算して表示する。
 *
 * @note [厳守] 原則 C++23 でコンパイルすること（C++20 でも動く可能性はあるが、学習目的として最新を推奨）。
 * @note [推奨] 実行時エラーは握りつぶさず、メッセージ（関数名/引数）から原因を特定する。
 * @note [禁止] 入力バリデーションを省略すること。理由: 学習でも実務でも不具合の温床になるため。
 */

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
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
    std::string owned(text);
    size_t parsedLength = 0;
    const int value = std::stoi(owned, &parsedLength, 10);
    if (parsedLength != owned.size()) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

/**
 * @brief 統計結果（合計/平均/最小/最大）。
 */
struct statisticsResult {
  long long sum = 0;
  double average = 0.0;
  int minValue = 0;
  int maxValue = 0;
};

/**
 * @brief 数列の統計を計算します。
 * @param numbers std::span<const int> 入力数列（空は不可）
 * @return statisticsResult 結果
 * @throws std::invalid_argument numbers が空のとき
 */
statisticsResult computeStatistics(const std::span<const int> numbers) {
  if (numbers.empty()) {
    throw std::invalid_argument("computeStatistics: numbers is empty");
  }

  statisticsResult result;
  result.minValue = std::numeric_limits<int>::max();
  result.maxValue = std::numeric_limits<int>::min();

  for (const int v : numbers) {
    result.sum += static_cast<long long>(v);
    result.minValue = std::min(result.minValue, v);
    result.maxValue = std::max(result.maxValue, v);
  }
  result.average = static_cast<double>(result.sum) / static_cast<double>(numbers.size());
  return result;
}

/**
 * @brief `--numbers` オプション以降の引数を整数配列として読み取ります。
 * @param args std::vector<std::string> コマンドライン引数（argv をコピーしたもの）
 * @return std::vector<int> 読み取った整数列（空の場合あり）
 * @throws std::runtime_error 変換に失敗した場合
 */
std::vector<int> parseNumbersOption(const std::vector<std::string>& args) {
  std::vector<int> numbers;

  // 例: modern.exe --numbers 1 2 3
  auto it = std::find(args.begin(), args.end(), "--numbers");
  if (it == args.end()) {
    return numbers;
  }

  for (auto jt = std::next(it); jt != args.end(); ++jt) {
    const std::optional<int> valueOpt = parseInt(*jt);
    if (!valueOpt.has_value()) {
      std::ostringstream oss;
      oss << "parseNumbersOption: failed to parse int"
          << " token=\"" << *jt << "\""
          << " (expected: decimal integer)";
      throw std::runtime_error(oss.str());
    }
    numbers.push_back(*valueOpt);
  }
  return numbers;
}

/**
 * @brief 使い方を表示します。
 * @param programName std::string 実行ファイル名
 * @return void
 */
void printHelp(const std::string& programName) {
  std::cout << "modern.cpp (総合サンプル)\n";
  std::cout << "\n";
  std::cout << "使い方:\n";
  std::cout << "  " << programName << " --help\n";
  std::cout << "  " << programName << " --numbers 1 2 3 4 5\n";
  std::cout << "\n";
  std::cout << "説明:\n";
  std::cout << "- --numbers の後ろに整数を並べると、統計（sum/avg/min/max）を計算して表示します。\n";
  std::cout << "- --numbers が無い場合はデフォルトの数列で実行します。\n";
}

}  // namespace

/**
 * @brief エントリポイント（modern）。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return int 終了コード
 */
int main(const int argc, char** argv) {
  try {
    const std::vector<std::string> args(argv, argv + argc);
    if (argc >= 2 && args[1] == "--help") {
      printHelp(args[0]);
      return 0;
    }

    printTitle("environment");
    std::cout << "- args: " << joinArgs(argc, argv) << "\n";
    std::cout << "- reported standard: " << toCppStandardLabel(getReportedCppValue()) << "\n";
    std::cout << "- __cplusplus: " << toCppStandardLabel(static_cast<long long>(__cplusplus)) << "\n";
#if defined(_MSVC_LANG)
    std::cout << "- _MSVC_LANG: " << toCppStandardLabel(static_cast<long long>(_MSVC_LANG)) << "\n";
#endif

    std::vector<int> numbers = parseNumbersOption(args);
    if (numbers.empty()) {
      // [推奨] 入力が無い場合でも「動く」デフォルトを用意し、学習の入口を軽くする。
      numbers = {1, 2, 3, 4, 5};
    }

    printTitle("compute statistics");
    const auto startTime = std::chrono::steady_clock::now();
    const statisticsResult stats = computeStatistics(std::span<const int>(numbers.data(), numbers.size()));
    const auto endTime = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    std::cout << "numbers size=" << numbers.size() << "\n";
    std::cout << "sum=" << stats.sum << "\n";
    std::cout << "average=" << stats.average << "\n";
    std::cout << "min=" << stats.minValue << "\n";
    std::cout << "max=" << stats.maxValue << "\n";
    std::cout << "elapsedMs=" << elapsedMs << "\n";

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[error] function=main(file=modern.cpp)"
              << " message=\"" << ex.what() << "\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 1;
  } catch (...) {
    std::cerr << "[error] function=main(file=modern.cpp)"
              << " message=\"unknown exception\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 2;
  }
}

