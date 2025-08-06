/**
 * C++ Hello World プログラム
 * 概要: 現代的なC++とオブジェクト指向プログラミングのサンプル
 * 主な仕様:
 * - std::cout を使用したコンソール出力
 * - クラスとオブジェクトの基本的な使用方法
 * - RAII（Resource Acquisition Is Initialization）の実践
 * - スマートポインタの使用
 * 制限事項: C++11以降が必要
 */

#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <vector>

/**
 * @brief 練習用のサンプルクラス
 * 人物の情報を管理するクラス
 */
class Person {
private:
    std::string name_;  ///< 名前（プライベートメンバ）
    int age_;          ///< 年齢（プライベートメンバ）

public:
    /**
     * @brief コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @throws std::invalid_argument 無効な引数が渡された場合
     */
    Person(const std::string& name, int age) : name_(name), age_(age) {
        try {
            if (name.empty()) {
                throw std::invalid_argument("名前が空文字列です");
            }
            if (age < 0) {
                throw std::invalid_argument("年齢は0以上である必要があります");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "エラーが発生しました - クラス: Person, メソッド: コンストラクタ"
                      << ", 引数: name=" << name << ", age=" << age
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief デストラクタ
     */
    ~Person() {
        std::cout << name_ << "さんのオブジェクトが破棄されました。" << std::endl;
    }

    /**
     * @brief 名前のゲッター
     * @return 名前
     */
    const std::string& getName() const noexcept {
        return name_;
    }

    /**
     * @brief 年齢のゲッター
     * @return 年齢
     */
    int getAge() const noexcept {
        return age_;
    }

    /**
     * @brief 年齢のセッター
     * @param age 新しい年齢
     * @throws std::invalid_argument 無効な年齢が指定された場合
     */
    void setAge(int age) {
        try {
            if (age < 0) {
                throw std::invalid_argument("年齢は0以上である必要があります");
            }
            age_ = age;
        }
        catch (const std::exception& e) {
            std::cerr << "エラーが発生しました - クラス: Person, メソッド: setAge"
                      << ", 引数: age=" << age << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief 自己紹介メソッド
     * @return 自己紹介文字列
     */
    std::string introduce() const {
        try {
            return "こんにちは！私の名前は" + name_ + "で、" + std::to_string(age_) + "歳です。";
        }
        catch (const std::exception& e) {
            std::cerr << "エラーが発生しました - クラス: Person, メソッド: introduce"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief 年齢を1歳増加させるメソッド
     */
    void incrementAge() {
        try {
            age_++;
            std::cout << name_ << "さんの年齢が" << age_ << "歳になりました。" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "エラーが発生しました - クラス: Person, メソッド: incrementAge"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }
};

/**
 * @brief メイン関数
 * @return プログラムの終了ステータス
 */
int main() {
    try {
        // Hello World の出力
        std::cout << "Hello World!" << std::endl;
        std::cout << "C++プログラミング学習を開始します。" << std::endl;
        std::cout << std::endl;

        // オブジェクト指向プログラミングのサンプル
        std::cout << "=== オブジェクト指向プログラミングのサンプル ===" << std::endl;

        // スマートポインタを使用したインスタンスの作成
        auto person1 = std::make_unique<Person>("田中太郎", 25);
        auto person2 = std::make_unique<Person>("佐藤花子", 30);

        // メソッドの呼び出し
        std::cout << person1->introduce() << std::endl;
        std::cout << person2->introduce() << std::endl;
        std::cout << std::endl;

        // プロパティの変更とメソッド呼び出し
        std::cout << "年齢を増加させます..." << std::endl;
        person1->incrementAge();
        person2->incrementAge();
        std::cout << std::endl;

        // 変更後の状態確認
        std::cout << "変更後の情報:" << std::endl;
        std::cout << person1->introduce() << std::endl;
        std::cout << person2->introduce() << std::endl;
        std::cout << std::endl;

        // vectorを使用したコレクションの例
        std::vector<std::unique_ptr<Person>> people;
        people.push_back(std::make_unique<Person>("山田次郎", 20));
        people.push_back(std::make_unique<Person>("鈴木三郎", 35));

        std::cout << "=== コレクションのサンプル ===" << std::endl;
        for (const auto& person : people) {
            std::cout << person->introduce() << std::endl;
        }

        std::cout << std::endl;
        std::cout << "プログラムが正常に終了しました。" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "予期しないエラーが発生しました - メソッド: main"
                  << ", エラー: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}