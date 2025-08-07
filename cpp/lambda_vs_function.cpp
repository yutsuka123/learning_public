/**
 * @file lambda_vs_function.cpp
 * @brief C++ におけるラムダ式と従来の関数オブジェクト／関数ポインタの比較サンプル
 *
 * 概要:
 *   1. 関数ポインタ (Cスタイル)
 *   2. 関数オブジェクト (ファンクタ) - C++03までの典型
 *   3. std::function + ラムダ式 (C++11〜) : 柔軟で可読性が高い
 *   4. ジェネリックラムダ (C++14〜) と `auto` パラメータ
 *   5. キャプチャリストによる状態保持
 * 主な仕様:
 *   - 10 個の整数に対して、さまざまなフィルター関数を適用
 *   - 可読性・パフォーマンス・柔軟性の観点を比較
 *   - コンパイル時に `-std:c++17` 以上が必要
 *
 * コンパイル例 (MSVC x64):
 *   cl /utf-8 /EHsc /std:c++17 lambda_vs_function.cpp /Fe:lambda_vs_function.exe
 */

#include <iostream>
#include <vector>
#include <functional>
#include <algorithm>

// =============================================
// 1. 関数ポインタ (Cスタイル)
// =============================================
bool isEven(int n) { return n % 2 == 0; }

// =============================================
// 2. 関数オブジェクト (ファンクタ)
// =============================================
struct IsMultipleOf {
    int divisor;
    explicit IsMultipleOf(int d) : divisor(d) {}
    bool operator()(int n) const { return n % divisor == 0; }
};

int main() {
    std::cout << "=== C++ ラムダ式 vs 従来手法 ===\n";

    std::vector<int> numbers{1,2,3,4,5,6,7,8,9,10};

    // ---------------------------------------------
    // 1. 関数ポインタ
    // ---------------------------------------------
    {
        std::cout << "\n-- 関数ポインタ (isEven) --\n";
        std::vector<int> evens;
        std::copy_if(numbers.begin(), numbers.end(), std::back_inserter(evens), isEven);
        for(int n: evens) std::cout << n << ' ';
        std::cout << "\n";
    }

    // ---------------------------------------------
    // 2. 関数オブジェクト (ファンクタ)
    // ---------------------------------------------
    {
        std::cout << "\n-- 関数オブジェクト (IsMultipleOf(3)) --\n";
        std::vector<int> mult3;
        std::copy_if(numbers.begin(), numbers.end(), std::back_inserter(mult3), IsMultipleOf{3});
        for(int n: mult3) std::cout << n << ' ';
        std::cout << "\n";
    }

    // ---------------------------------------------
    // 3. std::function + ラムダ式
    // ---------------------------------------------
    {
        std::cout << "\n-- std::function + ラムダ (>=5) --\n";
        std::function<bool(int)> greaterEqual5 = [](int n){ return n >= 5; };
        std::vector<int> ge5;
        std::copy_if(numbers.begin(), numbers.end(), std::back_inserter(ge5), greaterEqual5);
        for(int n: ge5) std::cout << n << ' ';
        std::cout << "\n";
    }

    // ---------------------------------------------
    // 4. ジェネリックラムダ (C++14〜)
    // ---------------------------------------------
    {
        std::cout << "\n-- ジェネリックラムダ (偶数かつ >=4) --\n";
        auto evenAndGE4 = [](auto n){ return n % 2 == 0 && n >= 4; };
        std::vector<int> evens4;
        std::copy_if(numbers.begin(), numbers.end(), std::back_inserter(evens4), evenAndGE4);
        for(int n: evens4) std::cout << n << ' ';
        std::cout << "\n";
    }

    // ---------------------------------------------
    // 5. キャプチャリスト (外部変数を取り込む)
    // ---------------------------------------------
    {
        std::cout << "\n-- キャプチャリスト (任意の閾値) --\n";
        int threshold = 7;
        // [&] で外部変数を参照キャプチャ
        auto greaterThan = [&threshold](int n){ return n > threshold; };
        std::vector<int> gt;
        std::copy_if(numbers.begin(), numbers.end(), std::back_inserter(gt), greaterThan);
        std::cout << "threshold=" << threshold << " 以上: ";
        for(int n: gt) std::cout << n << ' ';
        std::cout << "\n";

        // threshold を変更し再利用
        threshold = 3;
        gt.clear();
        std::copy_if(numbers.begin(), numbers.end(), std::back_inserter(gt), greaterThan);
        std::cout << "threshold=" << threshold << " 以上: ";
        for(int n: gt) std::cout << n << ' ';
        std::cout << "\n";
    }

    std::cout << "\nプログラムが正常に終了しました。\n";
    return 0;
}
