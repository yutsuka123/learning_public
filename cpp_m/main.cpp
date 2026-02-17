/**
 * @file main.cpp
 * @brief [重要] cpp_m/（モダンC++学習用）の入口。
 * @details
 * - 本ファイルは「このフォルダの使い方」を案内し、コンパイル時の標準バージョン情報を表示します。
 * - `cpp_m/` 配下の各 `.cpp` は **単体コンパイル前提**（基本的に各ファイルに `main()` が存在）です。
 *
 * @note [厳守] フォルダ追加/構成変更時は `ソース概要.md` と `.cursorrules` と `cpp_m/README.md` を更新すること。理由: 入口が分からなくなるのを防ぐため。
 * @note [推奨] まず `cpp11.cpp` から順に進める。理由: 機能が段階的に増えるため。
 * @note [禁止] 「とりあえず動いたからOK」でエラーを握りつぶすこと。理由: 学習では原因と対策の言語化が重要なため。
 */

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

/**
 * @brief コマンドライン引数を読みやすい形に連結します。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return std::string 連結した引数の文字列（例: "arg0 arg1 arg2"）
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
 * @brief 代表的な `__cplusplus` 値を人間向けに整形します。
 * @param value long long `__cplusplus` または `_MSVC_LANG` の値
 * @return std::string "201103L (C++11)" のような表示文字列
 */
std::string toCppStandardLabel(const long long value) {
  switch (value) {
    case 199711L:
      return "199711L (C++98/03 相当)";
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
      oss << value << " (未判定: コンパイラ実装に依存)";
      return oss.str();
    }
  }
}

/**
 * @brief コンパイラが報告する「現在のC++標準値」を取得します。
 * @details MSVC では `_MSVC_LANG` がより信頼できる場合があるため、両方を扱います。
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
 * @brief 使い方を表示します。
 * @param programName std::string 実行ファイル名
 * @return void
 */
void printHelp(const std::string& programName) {
  std::cout << "cpp_m/main.cpp (入口)\n";
  std::cout << "\n";
  std::cout << "使い方:\n";
  std::cout << "  " << programName << " --help\n";
  std::cout << "\n";
  std::cout << "学習の進め方（推奨）:\n";
  std::cout << "  cpp11.cpp -> cpp14.cpp -> cpp17.cpp -> cpp20.cpp -> cpp23.cpp -> modern.cpp\n";
  std::cout << "\n";
  std::cout << "ビルド方法は `cpp_m/INSTALL.md` を参照してください。\n";
}

}  // namespace

/**
 * @brief エントリポイント。
 * @param argc int 引数の個数
 * @param argv char** 引数配列
 * @return int 終了コード（0: 正常、0以外: 異常）
 */
int main(const int argc, char** argv) {
  try {
    const std::vector<std::string> args(argv, argv + argc);
    if (argc >= 2 && args[1] == "--help") {
      printHelp(args[0]);
      return 0;
    }

    std::cout << "[cpp_m] 入口プログラム\n";
    std::cout << "- args: " << joinArgs(argc, argv) << "\n";
    std::cout << "\n";

    const long long reported = getReportedCppValue();
    std::cout << "コンパイラ報告の標準値:\n";
    std::cout << "- reported: " << toCppStandardLabel(reported) << "\n";
    std::cout << "- __cplusplus: " << toCppStandardLabel(static_cast<long long>(__cplusplus)) << "\n";
#if defined(_MSVC_LANG)
    std::cout << "- _MSVC_LANG: " << toCppStandardLabel(static_cast<long long>(_MSVC_LANG)) << "\n";
#endif
    std::cout << "\n";

    std::cout << "次にやること:\n";
    std::cout << "- `cpp_m/README.md` を読む\n";
    std::cout << "- `cpp_m/INSTALL.md` の手順で `cpp11.cpp` を単体ビルドして実行する\n";
    std::cout << "\n";
    std::cout << "[重要] 各 `.cpp` は単体でコンパイルしてください（複数同時にビルドすると main() が衝突します）。\n";

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[error] function=main"
              << " message=\"" << ex.what() << "\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 1;
  } catch (...) {
    std::cerr << "[error] function=main"
              << " message=\"unknown exception\""
              << " argc=" << argc
              << " argv=\"" << joinArgs(argc, argv) << "\""
              << "\n";
    return 2;
  }
}

