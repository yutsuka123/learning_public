/**
 * @file legacy_memory.cpp
 * @brief 10〜20年前の C/C++ における古典的メモリ管理サンプル
 *
 * 概要:
 *   - new / delete, new[] / delete[] を用いた手動メモリ解放
 *   - malloc / free を組み合わせた C スタイル管理
 *   - スマートポインタや RAII を使用しないため、解放忘れや例外安全性の問題が発生しやすい
 * 主な仕様:
 *   1. new で確保した整数配列を手動で delete[]
 *   2. malloc で確保した C 配列を free
 *   3. 手動解放を忘れた場合のメモリリークを実演
 * 制限事項:
 *   - 例外安全性は考慮していない（あえて古いコード例として）
 *   - C++17 以降でもコンパイルは可能だが推奨されない
 *
 * コンパイル例:
 *   cl /utf-8 /EHsc /std:c++17 legacy_memory.cpp /Fe:legacy_memory.exe
 */

#include <iostream>
#include <cstdlib>  // malloc, free
#include <cstring>  // memset

int main() {
    std::cout << "=== 旧式メモリ管理サンプル ===\n";

    // ------------------------------
    // 1. new / delete[] による手動管理
    // ------------------------------
    {
        std::cout << "\n-- new / delete[] サンプル --\n";
        const size_t size = 5;
        int* numbers = new int[size];  // 動的配列の確保

        for (size_t i = 0; i < size; ++i) {
            numbers[i] = static_cast<int>(i * i);
        }

        std::cout << "配列内容:";
        for (size_t i = 0; i < size; ++i) {
            std::cout << ' ' << numbers[i];
        }
        std::cout << "\n";

        // 【重要】手動で delete[] しないとメモリリーク
        delete[] numbers;
        std::cout << "delete[] 完了\n";
    }

    // ------------------------------
    // 2. malloc / free による C スタイル管理
    // ------------------------------
    {
        std::cout << "\n-- malloc / free サンプル --\n";
        const size_t size = 5;
        int* numbers = static_cast<int*>(std::malloc(size * sizeof(int)));
        if (!numbers) {
            std::cerr << "malloc 失敗\n";
            return 1;
        }
        std::memset(numbers, 0, size * sizeof(int));

        for (size_t i = 0; i < size; ++i) {
            numbers[i] = static_cast<int>(i + 1);
        }
        std::cout << "配列内容:";
        for (size_t i = 0; i < size; ++i) {
            std::cout << ' ' << numbers[i];
        }
        std::cout << "\n";

        std::free(numbers);  // 手動で free
        std::cout << "free 完了\n";
    }

    // ------------------------------
    // 3. メモリリーク例（解放忘れ）
    // ------------------------------
    {
        std::cout << "\n-- メモリリーク例 (delete 忘れ) --\n";
        int* leakPtr = new int[10];
        for (int i = 0; i < 10; ++i) leakPtr[i] = i;
        std::cout << "delete[] を忘れるとリーク!\n";
        // delete[] leakPtr;  // ★ 故意にコメントアウト
    }

    std::cout << "\nプログラムが終了しました (メモリリークが発生したまま)。\n";
    return 0;
}
