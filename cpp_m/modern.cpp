/**
 * @file modern.cpp
 * @brief [重要] modern（最新の総合）サンプル。
 * @details
 * - 目的: C++17〜C++23 の「読みやすさ・安全性・実用性」に寄与する要素を、1つの小さな題材で統合的に練習する。
 * - 題材: `--numbers` で与えた整数列の統計（合計/平均/最小/最大）を計算して表示する。
 * - C言語経験者向け補足:
 *   - `std::vector<int>` は「可変長配列」。Cの `int*` + 要素数 + malloc/free をまとめて扱う。
 *   - `std::span<const int>` は「ポインタ+長さ」を安全に束ねた参照（所有しない）。
 *   - `std::optional<int>` は「値がある/ない」を型で表現（Cのセンチネル値より安全）。
 *   - 例外は「失敗を上位へ伝える」仕組み。組み込み等では方針により禁止されることもある。
 *
 * @note [厳守] 原則 C++23 でコンパイルすること（C++20 でも動く可能性はあるが、学習目的として最新を推奨）。
 * @note [推奨] 実行時エラーは握りつぶさず、メッセージ（関数名/引数）から原因を特定する。
 * @note [禁止] 入力バリデーションを省略すること。理由: 学習でも実務でも不具合の温床になるため。
 */

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
#include <utility>
#include <vector>

namespace {

/**
 * @brief コマンドライン引数を連結して表示用文字列にします。
 * @details
 * - 目的: 例外時ログなどで「どんな引数で実行したか」を一目で分かるようにする。
 * - C言語で言うと: `argv` をループして printf で並べるのと同じ。
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
 * @details
 * - `__cplusplus` は「その翻訳単位がどのC++標準でコンパイルされたか」を表す値です。
 * - ただし MSVC は歴史的事情で `__cplusplus` が古い値になりやすい（`/Zc:__cplusplus` で改善）。
 * - そのため本プロジェクトでは、MSVCの場合は `_MSVC_LANG` も併記します。
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
 * @details
 * - 学習上の目的は「自分が想定した標準でビルドできているか」を確認することです。
 * - 例: `/std:c++20` なのにC++17相当でビルドされていると、機能が使えず混乱します。
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
 * @details
 * - 出力が長くなる学習用サンプルでは、セクション区切りを明確にするのが重要です。
 * @param title std::string 見出し
 * @return void
 */
void printTitle(const std::string& title) {
  std::cout << "\n=== " << title << " ===\n";
}

/**
 * @brief 文字列を int に変換します（失敗時は std::nullopt）。
 * @details
 * - 成功した場合: `std::optional<int>` に値を入れて返す（例: "007" -> 7）
 * - 失敗した場合: `std::nullopt` を返す（例: "12x" / "" / " " など）
 *
 * [重要] ここでは `std::stoi` を使うため、
 * - 内部で例外が投げられる可能性がある（それをcatchして nullopt に変換）
 * - 変換の都合上、一度 `std::string` にコピーしている（速度より可読性を優先）
 *
 * C言語経験者向け:
 * - Cの `strtol` で `endptr` を見て判定するのと似ています。
 * - C++では「成功/失敗」を `optional` で表し、呼び出し側に判断させます。
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
 * @details
 * - 入力が空なら計算できないため例外を投げます（呼び出し側で捕捉）。
 * - 合計はオーバーフロー回避のため `long long`。
 *
 * 処理手順:
 * - min/max を「十分大きい/十分小さい」初期値にする
 * - すべての要素を1回ずつ走査し、sum/min/max を更新
 * - 最後に average を計算
 *
 * 結果のイメージ:
 * - 入力: [1,2,3,4,5]
 * - sum=15, average=3, min=1, max=5
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

  // [メンテ優先] まずは素直な for ループで意図を明確にする例。
  // - 速度が必要なら、計測してから `computeStatisticsRefined`（アルゴリズム）や、
  //   さらに低レベルな最適化へ進む。
  for (const int v : numbers) {
    result.sum += static_cast<long long>(v);
    result.minValue = std::min(result.minValue, v);
    result.maxValue = std::max(result.maxValue, v);
  }
  result.average = static_cast<double>(result.sum) / static_cast<double>(numbers.size());
  return result;
}

/**
 * @brief 数列の統計を計算します（洗練した書き方の例：標準アルゴリズム中心）。
 * @details
 * - [推奨] 実務では「意図が明確な標準アルゴリズム」を使うと読みやすくなります。
 * - 一方で、学習段階では `computeStatistics()` のような素直な for ループも理解しやすいです。
 * - ここでは「コード量優先（短め）」の方向に寄せていますが、短縮しすぎない範囲に留めています。
 *
 * 処理手順:
 * - sum: `std::accumulate` で合計（初期値 0LL を指定して long long 合計にする）
 * - min/max: `std::minmax_element` で一度に取得
 * - average: sum / size
 *
 * 結果は `computeStatistics()` と一致するはずなので、呼び出し側で一致チェックしています。
 * @param numbers std::span<const int> 入力数列（空は不可）
 * @return statisticsResult 結果
 * @throws std::invalid_argument numbers が空のとき
 */
statisticsResult computeStatisticsRefined(const std::span<const int> numbers) {
  if (numbers.empty()) {
    throw std::invalid_argument("computeStatisticsRefined: numbers is empty");
  }

  statisticsResult result;
  result.sum = std::accumulate(numbers.begin(), numbers.end(), 0LL);

  const auto [minIt, maxIt] = std::minmax_element(numbers.begin(), numbers.end());
  result.minValue = *minIt;
  result.maxValue = *maxIt;
  result.average = static_cast<double>(result.sum) / static_cast<double>(numbers.size());
  return result;
}

/**
 * @brief `--numbers` オプション以降の引数を整数配列として読み取ります。
 * @details
 * - コマンドライン例: `modern.exe --numbers 1 2 3 4 5`
 * - 実装方針: `--numbers` を見つけたら、その「後ろのトークンを最後まで」すべて整数として解釈します。
 *
 * [制限事項]
 * - `--numbers` の後ろに別オプション（例: `--help`）を置くと「数値として解釈」しようとして失敗します。
 *   （学習用に実装を単純化しています）
 *
 * エラー時の方針:
 * - 失敗したトークンを message に含めて例外を投げる（原因特定を容易にする）
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
 * @details
 * - 学習用サンプルでは、まず「どう実行するか」が分かることが重要です。
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

/**
 * @brief ラムダ式（lambda）のキャプチャ方式をまとめて確認します。
 * @details
 * - 目的: 「キャプチャの仕方で結果がどう変わるか」を、出力で体感する。
 *
 * ここで扱うキャプチャ例（代表）:
 * - `[]`                : キャプチャなし（外側の変数にアクセス不可）
 * - `[x]`               : x を値キャプチャ（作成時の値をコピー）
 * - `[&x]`              : x を参照キャプチャ（呼び出し時点の最新値を見る）
 * - `[=]` / `[&]`       : デフォルト値/参照キャプチャ（使う変数が増えるほど注意が必要）
 * - `[y = expr]`        : 初期化キャプチャ（C++14〜）
 * - `[p = std::move(p)]`: ムーブキャプチャ（C++14〜、所有権の移動を伴う）
 * - `[this]` / `[*this]`: thisキャプチャ / *thisキャプチャ（C++17〜、コピーか参照かが重要）
 *
 * [推奨] 初学者はまず「値キャプチャ」と「参照キャプチャ」の違いを確実に覚えるのが近道です。
 * @return void
 */
void demonstrateLambdaCaptures() {
  printTitle("lambda captures (patterns and result differences)");

  // -------------------------------------------------------------------------
  // (1) 値キャプチャ vs 参照キャプチャ
  // -------------------------------------------------------------------------
  int base = 10;
  const auto addByValue = [base](const int x) { return base + x; };   // baseはコピーされる
  const auto addByRef = [&base](const int x) { return base + x; };    // baseへの参照を保持

  // baseを書き換えると、参照キャプチャは結果が変わるが、値キャプチャは変わらない。
  // 期待される違い（例）:
  // - 作成時 base=10
  // - base を 100 に変更
  // - addByValue(5) は 15 のまま
  // - addByRef(5) は 105 になる
  base = 100;
  std::cout << "base(after change)=" << base << "\n";
  std::cout << "addByValue(5)=" << addByValue(5) << "  (expected: 15)\n";
  std::cout << "addByRef(5)=" << addByRef(5) << "    (expected: 105)\n";

  // -------------------------------------------------------------------------
  // (2) デフォルトキャプチャの注意点
  // -------------------------------------------------------------------------
  int a = 1;
  int b = 2;
  const auto sumDefaultValue = [=]() { return a + b; };  // a,b を値でコピー（作成時の値）
  const auto sumDefaultRef = [&]() { return a + b; };    // a,b を参照（呼び出し時の値）

  a = 10;
  b = 20;
  std::cout << "sumDefaultValue()=" << sumDefaultValue() << " (expected: 3)\n";
  std::cout << "sumDefaultRef()=" << sumDefaultRef() << "   (expected: 30)\n";

  // -------------------------------------------------------------------------
  // (3) 初期化キャプチャ（C++14〜）
  // -------------------------------------------------------------------------
  const int source = 7;
  const auto captureComputed = [computed = source * 10]() { return computed; };
  std::cout << "captureComputed()=" << captureComputed() << " (expected: 70)\n";

  // -------------------------------------------------------------------------
  // (4) ムーブキャプチャ（C++14〜）: 所有権の移動が「結果」に影響する例
  // -------------------------------------------------------------------------
  std::unique_ptr<std::string> messagePtr = std::make_unique<std::string>("moved-message");
  // messagePtr をラムダにムーブする。以後、外側の messagePtr は空になるのが通常。
  const auto useMovedPtr = [moved = std::move(messagePtr)]() -> std::string {
    if (!moved) {
      return "(moved is null)";
    }
    return *moved;
  };
  std::cout << "after move: messagePtr is " << (messagePtr ? "not null" : "null") << " (expected: null)\n";
  std::cout << "useMovedPtr()=" << useMovedPtr() << " (expected: moved-message)\n";

  // -------------------------------------------------------------------------
  // (5) this / *this キャプチャ（C++17〜）
  // -------------------------------------------------------------------------
  struct counter {
    int value = 0;

    // this キャプチャ: メンバを参照する（後でvalueが変わると結果も変わる）
    auto makeLambdaCaptureThis() {
      return [this]() { return value; };
    }

    // *this キャプチャ: オブジェクトをコピーする（作成時のスナップショット）
    auto makeLambdaCaptureStarThis() {
      return [*this]() { return value; };
    }
  };

  counter c;
  c.value = 1;
  const auto readByThis = c.makeLambdaCaptureThis();
  const auto readByStarThis = c.makeLambdaCaptureStarThis();

  c.value = 999;
  std::cout << "readByThis()=" << readByThis() << "      (expected: 999)\n";
  std::cout << "readByStarThis()=" << readByStarThis() << " (expected: 1)\n";

  // [重要] この違いはバグになりやすい:
  // - 参照（this）で持つと、オブジェクト寿命が先に切れた場合に危険（ぶら下がり）
  // - コピー（*this）で持つと、安全だがコピーコストや、更新が反映されない点に注意
}

}  // namespace

/**
 * @brief エントリポイント（modern）。
 * @details
 * 実行の流れ（学習用に「手順」を明示）:
 * - (1) 引数をvectorへコピー（後で検索しやすくする）
 * - (2) `--help` ならヘルプ表示
 * - (3) コンパイラ/標準値を表示（環境確認）
 * - (4) ラムダのキャプチャ例を実行（結果の違いを確認）
 * - (5) `--numbers` をパース（失敗したら例外）
 * - (6) 統計を2通りで計算して一致チェック（デグレ防止）
 * - (7) 結果を表示
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

    demonstrateLambdaCaptures();

    std::vector<int> numbers = parseNumbersOption(args);
    if (numbers.empty()) {
      // [推奨] 入力が無い場合でも「動く」デフォルトを用意し、学習の入口を軽くする。
      numbers = {1, 2, 3, 4, 5};
    }

    printTitle("compute statistics");
    const auto startTime = std::chrono::steady_clock::now();
    const statisticsResult stats = computeStatistics(std::span<const int>(numbers.data(), numbers.size()));
    const statisticsResult refinedStats = computeStatisticsRefined(std::span<const int>(numbers.data(), numbers.size()));
    const auto endTime = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    // [重要] 学習用: 2通りの実装が同じ結果になることを確認する（デグレ防止の考え方）。
    if (stats.sum != refinedStats.sum || stats.minValue != refinedStats.minValue || stats.maxValue != refinedStats.maxValue) {
      std::ostringstream oss;
      oss << "main(modern.cpp): statistics mismatch"
          << " sum(" << stats.sum << " vs " << refinedStats.sum << ")"
          << " min(" << stats.minValue << " vs " << refinedStats.minValue << ")"
          << " max(" << stats.maxValue << " vs " << refinedStats.maxValue << ")";
      throw std::runtime_error(oss.str());
    }

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

#if 0
// ---------------------------------------------------------------------------
// [悪い例/良い例] よくある間違いの対比（ビルドが通らない例は #if 0 に閉じ込める）
// ---------------------------------------------------------------------------
//
// (A) パース失敗を無視する（ビルドは通るがバグの温床）
// [悪い例] 失敗しても 0 扱いなどにしてしまい、入力ミスが見逃される
// int value = std::stoi(token);  // 例外が飛ぶ
//
// [良い例] optional / 例外メッセージで原因を特定できるようにする（このファイルの parseNumbersOption）
//
// (B) 合計を int で持つ（オーバーフロー）
// [悪い例] 大きい入力で sum が壊れる可能性がある
// int sum = 0;
// for (int v : numbers) { sum += v; }
//
// [良い例] long long で合計する（このファイルの statisticsResult::sum）
#endif

#if 0
// ---------------------------------------------------------------------------
// [豊富な対比] メンテ優先 / コード量優先 / 速度優先（同じ題材を別方針で書く例）
// ---------------------------------------------------------------------------
//
// (1) メンテ優先（業務/Web系で多い）
// - エラーは例外/optionalで明確化
// - ログに「関数名・引数・入力トークン」など原因特定情報を含める
// - 変数名は意図が分かる名前にする（短さより意味）
//
// (2) コード量優先（競プロ/短命ツールで多い）
// - アルゴリズム/auto/ラムダで短縮する
// - ただし失敗を握りつぶしがちなので、学習や業務では非推奨
//
// (3) 速度優先（ゲーム/低遅延で多い）
// - 事前確保: numbers.reserve(...)
// - 例外を避ける/ログを抑える（方針次第）
// - iostream を避ける（printf/バッファ/高速I/O）など
//
// [注意] 速度優先は「測ってから」。根拠のない最適化は、可読性と安全性を落とすだけになりやすい。
//
// ---- 速度寄り: 例外を使わずエラーコードで返す（組み込みにも寄せられる） ----
// enum class errorCode { ok, invalidArgument };
// struct parseNumbersResult { errorCode code; std::vector<int> numbers; std::string message; };
// parseNumbersResult parseNumbersNoException(const std::vector<std::string>& args);
#endif

