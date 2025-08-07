/**
 * C++ 高度な機能サンプル
 * 概要: メモリ管理手法とラムダ式のデモンストレーション
 * 主な仕様:
 * - 様々なメモリ管理手法の実演
 * - ラムダ式と通常関数の比較
 * - スマートポインタの使用例
 * - RAIIパターンの実践
 * 制限事項: C++11以降が必要
 */

#include <iostream>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include "opencv_sample.hpp"

// Windows環境での日本語表示対応
#ifdef _WIN32
#include <windows.h>
#endif

// C++バージョンを判定する関数
std::string getCppVersion() {
    // C++標準バージョンの判定
    if (__cplusplus > 201703L) return "C++20 以降";
    else if (__cplusplus > 201402L) return "C++17";
    else if (__cplusplus > 201103L) return "C++14";
    else if (__cplusplus > 199711L) return "C++11";
    else return "C++98/03";
}

// ==========================================================================
// メモリ管理例：古典的な手動メモリ管理
// ==========================================================================

/**
 * @brief 古典的なC++のメモリ管理（手動の割り当てと解放）
 * 
 * この方法は以下の問題があります：
 * - メモリリークのリスク：解放を忘れる可能性がある
 * - ダングリングポインタ：解放後にアクセスする可能性がある
 * - 例外安全でない：例外が発生した場合にメモリリークする可能性がある
 */
void demonstrateClassicMemoryManagement() {
    std::cout << "=================================================================" << std::endl;
    std::cout << " 古典的なメモリ管理 (C++98以前、C言語スタイル)" << std::endl;
    std::cout << "=================================================================" << std::endl;
    std::cout << "手動でメモリを確保し、使用後に解放する必要があるスタイルです。" << std::endl;
    std::cout << "このスタイルは多くの問題を引き起こす可能性があります。" << std::endl << std::endl;
    
    // 整数の動的配列を作成
    try {
        std::cout << "▼ 動的配列の管理（C++98スタイル）" << std::endl;
        std::cout << "処理内容: new[] 演算子でメモリを確保し、delete[] で解放" << std::endl;
        std::cout << "【開始】整数配列のメモリ確保" << std::endl;
        
        int* numbers = new int[5];  // 5つの整数の配列を割り当て
        std::cout << "【実行】new int[5] でメモリを確保（アドレス: " << numbers << "）" << std::endl;
        
        // 配列に値を設定
        std::cout << "【処理】配列に値を設定中..." << std::endl;
        for (int i = 0; i < 5; i++) {
            numbers[i] = i * 10;
            std::cout << "  numbers[" << i << "] = " << numbers[i] << std::endl;
        }
        
        // 配列の内容を表示
        std::cout << "【結果】配列の内容: ";
        for (int i = 0; i < 5; i++) {
            std::cout << numbers[i] << " ";
        }
        std::cout << std::endl;
        
        // メモリを解放（これを忘れるとメモリリークになる）
        std::cout << "【終了】delete[] でメモリを解放" << std::endl;
        delete[] numbers;
        std::cout << "【注意】この時点でnumbersポインタは無効（ダングリングポインタ）" << std::endl;
        
        // このポイント以降は numbers ポインタを使用すべきではない（ダングリングポインタ）
        std::cout << "【危険】解放済みのメモリにアクセスするとクラッシュする可能性あり" << std::endl;
        // numbers[0] = 100;  // 危険！解放済みメモリにアクセス
    }
    catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << std::endl;
        // ここで例外が発生すると、メモリリークが発生する可能性がある
    }
    
    // オブジェクトの動的生成
    try {
        std::cout << std::endl;
        std::cout << "▼ 動的オブジェクトの管理（C++98スタイル）" << std::endl;
        std::cout << "処理内容: new演算子でオブジェクトを生成し、deleteで解放" << std::endl;
        std::cout << "【開始】std::stringオブジェクトのメモリ確保" << std::endl;
        
        std::string* name = new std::string("山田太郎");
        std::cout << "【実行】new std::string でオブジェクトを生成（アドレス: " << name << "）" << std::endl;
        std::cout << "【確認】オブジェクトの値: " << *name << std::endl;
        
        // メモリを解放
        std::cout << "【終了】delete でオブジェクトを解放" << std::endl;
        delete name;
        std::cout << "【注意】この時点でnameポインタは無効（ダングリングポインタ）" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << std::endl;
    }
    
    // 例外安全でない古典的なコード例
    std::cout << std::endl;
    std::cout << "▼ 例外安全でないコード例（C++98スタイル）" << std::endl;
    std::cout << "問題点: 例外が発生するとメモリリークする可能性がある" << std::endl;
    std::cout << "以下のコードはリスクを示すためのもので、実行はしません:" << std::endl;
    std::cout << "  int* data1 = new int[100];" << std::endl;
    std::cout << "  int* data2 = new int[100];  // 例外が発生する可能性がある操作" << std::endl;
    std::cout << "  // ここで例外が発生すると、data1のメモリがリークする" << std::endl;
    std::cout << "  delete[] data2;" << std::endl;
    std::cout << "  delete[] data1;" << std::endl;
    
    std::cout << std::endl;
    std::cout << "【まとめ】古典的なメモリ管理の問題点:" << std::endl;
    std::cout << "1. メモリリークのリスク: 解放を忘れると、メモリが漏れる" << std::endl;
    std::cout << "2. ダングリングポインタ: 解放後のメモリにアクセスすると未定義動作" << std::endl;
    std::cout << "3. 例外安全でない: 例外発生時にメモリリークする可能性が高い" << std::endl;
    std::cout << std::endl;
}

// ==========================================================================
// メモリ管理例：スマートポインタを使用した自動メモリ管理
// ==========================================================================

/**
 * @brief リソースを管理するクラス
 */
class Resource {
private:
    std::string name_;
    size_t memory_size_;
    
public:
    Resource(const std::string& name, size_t memory_size = 1024) 
        : name_(name), memory_size_(memory_size) {
        std::cout << "【生成】リソース「" << name_ << "」を確保しました（サイズ: " 
                  << memory_size_ << " バイト）" << std::endl;
    }
    
    ~Resource() {
        std::cout << "【破棄】リソース「" << name_ << "」を解放しました（サイズ: " 
                  << memory_size_ << " バイト）" << std::endl;
    }
    
    void use() const {
        std::cout << "【使用】リソース「" << name_ << "」を使用中...（サイズ: " 
                  << memory_size_ << " バイト）" << std::endl;
    }
    
    size_t getMemorySize() const {
        return memory_size_;
    }
};

/**
 * @brief 現代的なC++のメモリ管理（スマートポインタ）
 * 
 * C++11以降で導入されたスマートポインタを使用すると：
 * - メモリリークを防止：スコープを抜けると自動的に解放される
 * - 所有権の明示：unique_ptrは排他的所有権、shared_ptrは共有所有権
 * - 例外安全：例外が発生しても確実にリソースが解放される
 */
void demonstrateSmartPointers() {
    std::cout << "=================================================================" << std::endl;
    std::cout << " スマートポインタによるメモリ管理 (C++11以降)" << std::endl;
    std::cout << "=================================================================" << std::endl;
    std::cout << "スマートポインタは、リソースの自動的な解放を保証するC++11の機能です。" << std::endl;
    std::cout << "これにより、メモリリークやダングリングポインタの問題が大幅に軽減されます。" << std::endl << std::endl;
    
    // std::unique_ptr - 排他的所有権
    std::cout << "▼ std::unique_ptr（排他的所有権）" << std::endl;
    std::cout << "処理内容: 唯一の所有者がリソースを管理（コピー不可、ムーブ可能）" << std::endl;
    std::cout << "C++11の機能: リソースの排他的所有権を表現" << std::endl;
    {
        std::cout << "【開始】unique_ptrのスコープ開始" << std::endl;
        
        // C++14からはmake_uniqueが推奨
        #if __cplusplus >= 201402L
        std::cout << "【実行】std::make_unique<Resource>を使用（C++14機能）" << std::endl;
        std::unique_ptr<Resource> resource1 = std::make_unique<Resource>("データベース接続", 4096);
        #else
        std::cout << "【実行】std::unique_ptr<Resource>(new Resource())を使用（C++11）" << std::endl;
        std::unique_ptr<Resource> resource1(new Resource("データベース接続", 4096));
        #endif
        
        resource1->use();
        
        std::cout << std::endl;
        std::cout << "【実行】所有権の移動デモンストレーション" << std::endl;
        std::cout << "【注意】unique_ptrはコピーできません（コンパイルエラー）:" << std::endl;
        std::cout << "  // std::unique_ptr<Resource> resource2 = resource1;  // エラー!" << std::endl;
        
        std::cout << "【実行】std::moveで所有権を移動" << std::endl;
        std::unique_ptr<Resource> resource2 = std::move(resource1);  // 所有権を移動
        
        // この時点で resource1 は nullptr
        if (!resource1) {
            std::cout << "【確認】resource1はnullptrになりました（所有権がなくなった）" << std::endl;
        }
        
        resource2->use();
        std::cout << "【終了】unique_ptrのスコープ終了（自動的に解放される）" << std::endl;
    }  // ここでresource2のデストラクタが自動的に呼ばれる
    std::cout << "【確認】スコープを抜けると自動的にデストラクタが呼ばれ、リソースが解放されました" << std::endl;
    
    // std::shared_ptr - 共有所有権
    std::cout << std::endl;
    std::cout << "▼ std::shared_ptr（共有所有権）" << std::endl;
    std::cout << "処理内容: 複数の所有者でリソースを共有管理（参照カウント方式）" << std::endl;
    std::cout << "C++11の機能: リソースの共有所有権を表現" << std::endl;
    {
        std::cout << "【開始】shared_ptrのスコープ開始" << std::endl;
        std::cout << "【実行】std::make_shared<Resource>を使用（効率的なメモリ割り当て）" << std::endl;
        std::shared_ptr<Resource> resource1 = std::make_shared<Resource>("設定ファイル", 2048);
        std::cout << "【確認】初期参照カウント: " << resource1.use_count() << std::endl;
        
        {
            // 所有権の共有
            std::cout << std::endl;
            std::cout << "【実行】内部スコープで所有権を共有" << std::endl;
            std::shared_ptr<Resource> resource2 = resource1;  // 参照カウント増加
            std::cout << "【確認】resource2 = resource1 後の参照カウント: " << resource1.use_count() << std::endl;
            
            std::shared_ptr<Resource> resource3 = resource1;  // 参照カウント増加
            std::cout << "【確認】resource3 = resource1 後の参照カウント: " << resource1.use_count() << std::endl;
            
            resource2->use();
            resource3->use();
            
            std::cout << "【終了】内部スコープ終了（resource2とresource3が破棄される）" << std::endl;
        }  // ここでresource2とresource3のスコープが終了（参照カウント減少）
        
        std::cout << "【確認】内部スコープ終了後の参照カウント: " << resource1.use_count() << std::endl;
        resource1->use();
        std::cout << "【終了】shared_ptrのスコープ終了" << std::endl;
    }  // ここでresource1のデストラクタが自動的に呼ばれる（参照カウントが0になる）
    std::cout << "【確認】スコープを抜けると自動的にデストラクタが呼ばれ、リソースが解放されました" << std::endl;
    
    // std::weak_ptr - 循環参照の回避
    std::cout << std::endl;
    std::cout << "▼ std::weak_ptr（循環参照の回避）" << std::endl;
    std::cout << "処理内容: shared_ptrの循環参照問題を解決するための弱参照" << std::endl;
    std::cout << "C++11の機能: リソースの弱参照（参照カウントを増やさない）" << std::endl;
    {
        std::cout << "【開始】weak_ptr の循環参照回避デモ" << std::endl;

        // ノード構造体：次ノードを shared_ptr、前ノードを weak_ptr で保持
        struct Node {
            std::string name;
            std::shared_ptr<Node> next;
            std::weak_ptr<Node> prev;

            explicit Node(const std::string& n) : name(n) {
                std::cout << "Node " << name << " を生成" << std::endl;
            }
            ~Node() {
                std::cout << "Node " << name << " を破棄" << std::endl;
            }
        };

        // 2 つのノードを生成し相互参照（ただし片方は weak_ptr）
        auto node1 = std::make_shared<Node>("node1");
        auto node2 = std::make_shared<Node>("node2");

        node1->next = node2;  // shared_ptr
        node2->prev = node1;  // weak_ptr なので参照カウントは増えない

        std::cout << "node1.use_count() = " << node1.use_count() << std::endl;
        std::cout << "node2.use_count() = " << node2.use_count() << std::endl;

        std::cout << "【終了】weak_ptr の例スコープ終了" << std::endl;
    }

    std::cout << "=================================================================" << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// エントリーポイント
////////////////////////////////////////////////////////////////////////////////
int main() {
#ifdef _WIN32
    // コンソールのコードページを UTF-8 に設定（日本語文字化け防止）
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "===============================================================" << std::endl;
    std::cout << " C++ バージョン: " << getCppVersion() << std::endl;
    std::cout << "===============================================================" << std::endl << std::endl;


    //helloworldの表示
    std::cout << "Hello, World!" << std::endl;

    //ラムダ式と関数    
    auto add = [](int a, int b) -> int {
        return a + b;
    };
    std::cout << add(1, 2) << std::endl;


    // 古典的メモリ管理のデモ
    demonstrateClassicMemoryManagement();

    std::cout << std::endl;

    // スマートポインタのデモ
    demonstrateSmartPointers();


    //opencvモジュールの呼び出し
    //opencv_sample.cppを呼び出して処理する
    runOpenCvSample();


    return 0;
}