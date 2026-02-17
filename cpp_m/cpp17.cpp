/**
 * @file cpp17.cpp
 * @brief [重要] C++17 学習用サンプル（単体コンパイル前提）。
 * @details
 * - 目的: C++17 の代表的機能を、実務でよく出る形（分岐・パース・戻り値）で確認する。
 * - 主な題材: 構造化束縛 / `if constexpr` / `std::optional` / `std::string_view`
 * - C言語経験者向け補足:
 *   - `std::optional<T>` は「値がある/ない」を型で表現します。Cでありがちな「-1 を失敗」等のセンチネルより安全。
 *   - `std::string_view` は「文字列を所有しない参照」です。Cの `const char*` に近いですが、
 *     **長さ情報を持つ**一方で、元の文字列の寿命が切れると危険（ぶら下がり）です。
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
 * @details
 * 入力と結果の例:
 * - "123" -> 123（成功: optionalに値あり）
 * - "007" -> 7（成功: 先頭ゼロは許容）
 * - "45x" -> 失敗（optionalが空: std::nullopt）
 *
 * 処理手順:
 * - `std::stoi` は `std::string` を要求するため、`string_view` を一度 `string` にコピーする
 * - `std::stoi` で数値化し、`parsedLength` が文字列全体と一致するか確認
 *   - 例: "45x" は parsedLength=2, size=3 になり、途中に非数値があると判断して失敗扱い
 * - 例外が出た場合も失敗扱い（nullopt）
 *
 * C言語経験者向け:
 * - `strtol(text, &endptr, 10)` の endptr を見るのと同じ発想です。
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

#if 0
// ---------------------------------------------------------------------------
// [業界別の好み] 例外 vs 戻り値（組み込み等で「例外禁止」の場合）
// ---------------------------------------------------------------------------
//
// [メンテ優先] optional で「失敗しうる」を型で表す（このファイルの parseInt）
//
// [組み込み/速度優先の一部] 例外禁止: 戻り値でエラーを返す例
// enum class errorCode { ok, invalidArgument };
// struct parseResult { errorCode code; int value; };
// parseResult parseIntNoException(std::string_view text);
#endif

/**
 * @brief `if constexpr` の例（型に応じた分岐をコンパイル時に行う）。
 * @details
 * - `if constexpr` は「型によって不要な分岐」をコンパイル時に消せます。
 * - そのため、テンプレートでありがちな「使わない分岐が原因でコンパイルエラー」になりにくくします。
 *
 * 出力例（このファイルの demonstrateIfConstexpr より）:
 * - int    -> "integral"
 * - double -> "floating"
 * - string -> "other"
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
 * @details
 * - `std::pair` や `std::tuple` の要素を、分解して個別の変数として受け取れます。
 *
 * 重要ポイント:
 * - `const auto [id, name] = user;` は **コピー**します（idとnameは独立した値になる）
 * - 「参照で受けたい」場合は `const auto& [id, name] = user;` のように書きます（ただし user の寿命が必要）
 *
 * 出力例:
 * - user={42,"alice"} の場合、"id=42 name=alice"
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
 * @details
 * - `parseInt` は optional を返すので、呼び出し側は「成功/失敗」を必ず分岐で扱えます。
 *
 * 重要ポイント:
 * - `valueOpt.has_value()` で「値があるか」を判定する
 * - 値があるときだけ `*valueOpt` で取り出す（空のoptionalを `*` すると未定義動作）
 *
 * 出力例:
 * - "123" -> value=123
 * - "45x" -> parse failed
 * - "007" -> value=7
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
 * @details
 * - `getTypeCategory` はテンプレートなので、渡した型ごとにコンパイル時に分岐が確定します。
 * - 初学者向けに「型が違うと結果が変わる」ことを、出力で確認します。
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
 * @details
 * 実行順:
 * - (1) 構造化束縛（分解代入）
 * - (2) if constexpr（型で分岐）
 * - (3) optional/string_view（失敗を型で表す）
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

#if 0
// ---------------------------------------------------------------------------
// [悪い例/良い例] よくある間違いの対比（ビルドが通らない例は #if 0 に閉じ込める）
// ---------------------------------------------------------------------------
//
// (A) C++17 機能を C++14 でビルドしてしまう
// [悪い例] 構造化束縛は C++17 から。C++14 でコンパイルするとエラーになる。
// std::pair<int, std::string> user{1, "bob"};
// auto [id, name] = user;  // <- C++17
//
// [良い例] `cpp17.cpp` は C++17 でビルドする（`INSTALL.md` の /std:c++17）
//
// (B) string_view の寿命（dangling）: ビルドは通るが危険
// [悪い例] ローカル変数の文字列を指す string_view を返す（返った時点でぶら下がり参照）
// std::string_view badGetView() {
//   std::string local = "abc";
//   return std::string_view(local);  // <- local は関数終了で破棄される
// }
//
// [良い例] 返すなら std::string（所有）にする、または呼び出し側で寿命が保証された文字列を参照する
// std::string goodGetOwned() {
//   return "abc";
// }
//
// (C) コード量優先の例（短いが注意）
// [短い例] optional の扱いを1行に詰めると読みづらくなることがある
// auto v = parseInt(input).value_or(0);  // 失敗を0扱いにしてしまい、入力ミスを見逃しやすい
//
// [メンテ優先] 失敗時はログ・メッセージで原因を残す（このファイルの demonstrateOptionalAndStringView）
#endif

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

