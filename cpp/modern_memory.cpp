/**
 * @file modern_memory.cpp
 * @brief 現代的なC++（C++11以降）のメモリ管理サンプル
 *
 * 概要:
 *   - RAII（Resource Acquisition Is Initialization）に基づいた安全なメモリ管理
 *   - std::unique_ptr / std::shared_ptr / std::weak_ptr の基本的な使い方
 *   - 例外安全性を意識したコード
 * 主な仕様:
 *   1. 動的に確保した整数配列を std::unique_ptr で自動管理
 *   2. クラス Sample オブジェクトを std::shared_ptr で共有管理
 *   3. 循環参照を避けるため std::weak_ptr を利用
 * 制限事項:
 *   - C++17 以降でのコンパイルを想定
 *   - スマートポインタを使用しない old-style new/delete は含まない
 *
 * コンパイル例:
 *   cl /utf-8 /EHsc /std:c++17 modern_memory.cpp /Fe:modern_memory.exe
 */

#include <iostream>
#include <memory>
#include <vector>
#include <string>

/**
 * @class Sample
 * @brief 共有管理されるテスト用クラス
 */
class Sample {
private:
    std::string name_;              ///< オブジェクト名
    std::weak_ptr<Sample> partner_; ///< 循環参照防止用 weak_ptr

public:
    /// コンストラクタ
    explicit Sample(const std::string& name)
        : name_(name) {
        std::cout << "[Sample] " << name_ << " が生成されました\n";
    }

    /// デストラクタ（破棄確認用メッセージ）
    ~Sample() {
        std::cout << "[Sample] " << name_ << " が破棄されました\n";
    }

    /// パートナー設定（循環参照を避けるため weak_ptr を使用）
    void setPartner(const std::shared_ptr<Sample>& partner) {
        partner_ = partner;
    }

    /// 自己紹介メソッド
    void introduce() const {
        std::cout << "こんにちは！私は " << name_ << " です。";
        if (auto p = partner_.lock()) {
            std::cout << " パートナーは " << p->name_ << " です。";
        }
        std::cout << "\n";
    }
};

int main() {
    std::cout << "=== 現代的C++メモリ管理サンプル ===\n";

    // ------------------------------
    // 1. unique_ptr による所有権の単独管理
    // ------------------------------
    {
        std::cout << "\n-- unique_ptr サンプル --\n";
        const size_t size = 5;
        // 動的配列を unique_ptr で管理（配列版）
        std::unique_ptr<int[]> numbers = std::make_unique<int[]>(size);

        for (size_t i = 0; i < size; ++i) {
            numbers[i] = static_cast<int>(i * i);
        }

        std::cout << "配列内容:";
        for (size_t i = 0; i < size; ++i) {
            std::cout << ' ' << numbers[i];
        }
        std::cout << "\n";
        // スコープを抜けると自動で delete[] される
    }

    // ------------------------------
    // 2. shared_ptr と weak_ptr による共有管理
    // ------------------------------
    {
        std::cout << "\n-- shared_ptr / weak_ptr サンプル --\n";

        auto alice = std::make_shared<Sample>("Alice");
        auto bob   = std::make_shared<Sample>("Bob");

        // パートナー設定（循環参照抑止）
        alice->setPartner(bob);
        bob->setPartner(alice);

        alice->introduce();
        bob->introduce();

        std::cout << "\n現在の参照カウント:\n";
        std::cout << " Alice: " << alice.use_count() << "\n";
        std::cout << " Bob  : " << bob.use_count()   << "\n";

        // スコープを抜けると参照カウントが 0 になり自動で delete される
    }

    std::cout << "\nプログラムが正常に終了しました。\n";
    return 0;
}
