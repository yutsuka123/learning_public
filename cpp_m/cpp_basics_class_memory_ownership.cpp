/**
 * @file cpp_basics_class_memory_ownership.cpp
 * @brief [重要] C++の基礎（クラスと構造体・メモリ確保・所有権・std::vector・C言語との違い）を
 * 横断的に復習する学習用サンプル。
 * @details
 * - 目的: `cpp11.cpp`〜`cpp23.cpp` はC++標準バージョン別の新機能を扱うのに対し、本ファイルは
 *   バージョンを問わず押さえておきたい「C++の基本骨格」を1つにまとめて復習する。
 * - 対象読者: C言語の経験はあり、C++のクラス・スマートポインタ・vectorを基礎から復習したい人向け。
 * - 主な題材:
 *   1. クラスと構造体（class vs struct）の違いと使い分け
 *   2. メモリ確保: 古いやり方(new/delete)とスマートポインタの比較
 *   3. 所有権（unique_ptr / shared_ptr / weak_ptr）
 *   4. std::vector の復習（基本操作・capacity・Cの動的配列との対比）
 *   5. C言語との違い一覧（各項目、実際に動くC風コード/C++コードの対比つき）
 *   6. 便利なC++機能一覧（簡易チートシート。各項目、短い実コード例つき。深掘りは cpp11.cpp〜cpp23.cpp を参照）
 *   7. 標準入出力ストリーム（`<<`/`>>`の使い方、マニピュレータ、`printf`系との比較）
 *   8. ラムダ式を詳しく（キャプチャ方式、ジェネリックラムダ、`<algorithm>`との組み合わせ、
 *      `std::function`、再帰。それぞれ「普通の関数/functorで書いたら」との比較つき）
 * - [注意] 各出力文の実行結果は、その文の右横または直後に `// -> 実際の出力` の形でコメントしている。
 *   値が計算されず文字列リテラルそのままの出力（§5,6等）は、読めば分かるため省略している。
 *
 * 実行手順:
 * cd learning_public   (リポジトリのルート)
 * g++ -std=c++17 -Wall -Wextra -Wpedantic -g \
 *     cpp_m/cpp_basics_class_memory_ownership.cpp \
 *     -o cpp_m/cpp_basics_class_memory_ownership
 * ./cpp_m/cpp_basics_class_memory_ownership
 *
 * @note [推奨] AddressSanitizer/UndefinedBehaviorSanitizer でメモリリーク・不正アクセスがないことを確認する。
 * 例: g++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -g \
 *     cpp_m/cpp_basics_class_memory_ownership.cpp -o /tmp/cbm_asan && /tmp/cbm_asan
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/**
 * @brief セクションの見出しを表示します。
 * @param title const std::string& 見出し文字列
 * @return void
 */
void printTitle(const std::string& title) {
  std::cout << "\n=== " << title << " ===\n";
}

/* ------------------------------------------------------------------------
 * 1. クラスと構造体（class vs struct）
 * ---------------------------------------------------------------------- */

/**
 * @brief structのデフォルトアクセス指定（public）を確認するための型。
 * @details メンバに何も書かなければ public。Cのstructと同じ「データの塊」としても使える。
 */
struct PointStruct {
  double x;
  double y;
};

/**
 * @brief classのデフォルトアクセス指定（private）を確認するための型。
 * @details メンバに何も書かなければ private。外から直接 x_, y_ に触れないので、
 * コンストラクタ/ゲッターを介したアクセスに限定できる（カプセル化）。
 */
class PointClass {
 public:
  PointClass(double x, double y) : x_(x), y_(y) {}

  double x() const { return x_; }
  double y() const { return y_; }

 private:
  double x_;
  double y_;
};

/**
 * @brief struct と class の違い（デフォルトアクセス指定子だけ）を確認します。
 * @details
 * - C++では `struct` と `class` はキーワードが違うだけで、機能的にはほぼ同じ
 *   （メンバ関数もコンストラクタも継承もテンプレートも両方に書ける）。Cのstructとは違い、
 *   C++のstruct/classはどちらもメンバ関数・コンストラクタ・デストラクタ・継承を持てる。
 * - 唯一の言語仕様上の違いは「メンバ/継承のデフォルトアクセス指定子」: structはpublic、classはprivate。
 * - 慣習: データの塊・不変条件のない値には struct、コンストラクタで不変条件を保証したい・
 *   内部状態を隠したい場合は class、という使い分けが一般的（あくまで慣習でコンパイラは強制しない）。
 * @return void
 */
void demonstrateClassVsStruct() {
  printTitle("1. class vs struct (default access)");

  PointStruct ps{1.0, 2.0};  // publicなので直接アクセスできる
  std::cout << "PointStruct: x=" << ps.x << ", y=" << ps.y << " (メンバへ直接アクセス)\n";
  // -> PointStruct: x=1, y=2 (メンバへ直接アクセス)

  PointClass pc(3.0, 4.0);  // x_, y_ はprivateなので、公開されたゲッター経由でアクセス
  std::cout << "PointClass: x=" << pc.x() << ", y=" << pc.y() << " (ゲッター経由でアクセス)\n";
  // -> PointClass: x=3, y=4 (ゲッター経由でアクセス)

  // [悪い例] classのprivateメンバへの直接アクセスはコンパイルエラーになるため、実行コードにはできない
  // （これ自体が「privateが機能している証拠」でもある）。あえて#if 0で無効化して残す。
  // 実際に有効化すると、gccなら概ね次のようなエラーになる:
  //   error: 'double PointClass::x_' is private within this context
#if 0
  std::cout << pc.x_;  // <- コンパイルエラー（x_はprivate）
#endif
}

/* ------------------------------------------------------------------------
 * 2. メモリ確保: 古いやり方(new/delete) と スマートポインタ の比較
 * ---------------------------------------------------------------------- */

/**
 * @brief construct/destructのタイミングをログ出力する、メモリ確保デモ用クラス。
 * @details コンストラクタ/デストラクタが「いつ」呼ばれるかを目で確認できるようにするための道具。
 */
class Sensor {
 public:
  explicit Sensor(std::string name) : name_(std::move(name)) {
    std::cout << "  [Sensor] construct: " << name_ << "\n";
  }
  ~Sensor() { std::cout << "  [Sensor] destruct: " << name_ << "\n"; }

  const std::string& name() const { return name_; }

 private:
  std::string name_;
};

/**
 * @brief 「古いやり方」(new/delete)でのメモリ確保・解放を確認します。
 * @details
 * - [注意] `new` した後は、すべての実行パス（正常終了/早期return/例外）で
 *   確実に `delete` を呼ばなければならない。呼び忘れるとメモリリーク、
 *   2回呼ぶと二重解放(double free)というバグになる。
 * - このセクションでは正しくdeleteしているが、「呼び忘れやすい」という構造的な問題自体は
 *   なくなっていない点に注目する（次の `demonstrateSmartPointerBasics` と比較する）。
 * @return void
 */
void demonstrateRawNewDelete() {
  printTitle("2a. old way: new / delete (raw pointer)");

  Sensor* raw = new Sensor("raw-new");  // -> "  [Sensor] construct: raw-new" (コンストラクタのログ)
  std::cout << "raw pointer -> name=" << raw->name() << "\n";
  // -> raw pointer -> name=raw-new

  // [重要] newした数だけ必ずdeleteする。ここで忘れると即メモリリーク。
  delete raw;  // -> "  [Sensor] destruct: raw-new" (デストラクタのログ)
  raw = nullptr;  // [推奨] delete直後にnullptr代入（Cのfree後NULL代入と同じ考え方）
}

/**
 * @brief `unique_ptr` を使ったスマートポインタでのメモリ確保・解放を確認します。
 * @details
 * - `std::make_unique<Sensor>(...)` で確保すると、スコープを抜けるタイミングで
 *   デストラクタが自動的に呼ばれる（RAII: Resource Acquisition Is Initialization）。
 * - `delete` を書く必要がない → 「呼び忘れ」というバグのクラスがそもそも起こらない。
 * - 例外が飛んだ場合でも、スタック巻き戻し中にデストラクタは呼ばれるため、
 *   raw new/delete より例外安全性が高い。
 * @return void
 */
void demonstrateSmartPointerBasics() {
  printTitle("2b. modern way: unique_ptr (RAII)");

  {
    std::unique_ptr<Sensor> smart = std::make_unique<Sensor>("smart-unique");
    // -> "  [Sensor] construct: smart-unique" (コンストラクタのログ)
    std::cout << "unique_ptr -> name=" << smart->name() << "\n";
    // -> unique_ptr -> name=smart-unique
    // deleteを書く必要はない。ここでスコープを抜けると自動的にデストラクタが呼ばれる。
  }  // -> "  [Sensor] destruct: smart-unique" (スコープを抜けて自動的にデストラクタのログ)
  std::cout << "(スコープを抜けた直後: 上のdestructログが既に出ている)\n";
  // -> (スコープを抜けた直後: 上のdestructログが既に出ている)
}

/* ------------------------------------------------------------------------
 * 3. 所有権（unique_ptr / shared_ptr / weak_ptr）
 * ---------------------------------------------------------------------- */

/**
 * @brief unique_ptr（排他所有）の所有権はコピーできず、moveでしか移動できないことを確認します。
 * @details
 * - `unique_ptr` は「このリソースの持ち主は常に1つだけ」を型で保証する。
 * - コピーコンストラクタ/代入は削除されている（`= delete` 相当）ため、コピーしようとすると
 *   コンパイルエラーになる。
 * - 所有権を渡したい場合は `std::move` で明示的に移動する（移動元はnullptrになる）。
 * @return void
 */
void demonstrateUniquePtrOwnership() {
  printTitle("3a. ownership: unique_ptr (exclusive, move-only)");

  std::unique_ptr<Sensor> ownerA = std::make_unique<Sensor>("unique-owner");
  // -> "  [Sensor] construct: unique-owner" (コンストラクタのログ)
  std::unique_ptr<Sensor> ownerB;  // まだ何も持たない(nullptr)

  std::cout << "before move: owner_a=" << (ownerA ? "has value" : "null")
            << ", owner_b=" << (ownerB ? "has value" : "null") << "\n";
  // -> before move: owner_a=has value, owner_b=null

  ownerB = std::move(ownerA);  // 所有権をAからBへ移動。Aはnullptrになる。

  std::cout << "after move:  owner_a=" << (ownerA ? "has value" : "null") << ", owner_b="
            << (ownerB ? "has value (" + ownerB->name() + ")" : "null") << "\n";
  // -> after move:  owner_a=null, owner_b=has value (unique-owner)

  // [悪い例] unique_ptrのコピーはコンパイルエラーになるため、実行コードにはできない
  // （コピーコンストラクタが `= delete` されているため）。あえて#if 0で無効化して残す。
  // 実際に有効化すると、gccなら概ね次のようなエラーになる:
  //   error: use of deleted function 'std::unique_ptr<Sensor>::unique_ptr(const std::unique_ptr<Sensor>&)'
#if 0
  std::unique_ptr<Sensor> ownerC = ownerB;  // <- コンパイルエラー（コピー不可）
#endif
}  // -> ここでownerBが破棄され "  [Sensor] destruct: unique-owner" (デストラクタのログ)

/**
 * @brief shared_ptr（共有所有）は参照カウントで複数の所有者を許すことを確認します。
 * @details
 * - `shared_ptr` は内部に参照カウントを持ち、コピーされるたびにカウントが増える。
 * - 参照カウントが0になった時点（最後のshared_ptrが破棄された時点）で実体が解放される。
 * - `use_count()` で「今何個のshared_ptrが同じ実体を指しているか」を確認できる。
 * @return void
 */
void demonstrateSharedPtrOwnership() {
  printTitle("3b. ownership: shared_ptr (shared, ref-counted)");

  std::shared_ptr<Sensor> ownerA = std::make_shared<Sensor>("shared-owner");
  // -> "  [Sensor] construct: shared-owner" (コンストラクタのログ)
  {
    std::shared_ptr<Sensor> ownerB = ownerA;  // コピー可能。参照カウントが増える。
    std::cout << "after copy: use_count=" << ownerA.use_count() << "\n";
    // -> after copy: use_count=2
  }  // ownerBがスコープを抜けて破棄される → 参照カウントが減る
  std::cout << "after owner_b out of scope: use_count=" << ownerA.use_count() << "\n";
  // -> after owner_b out of scope: use_count=1
}  // -> ここでownerAが破棄され "  [Sensor] destruct: shared-owner" (デストラクタのログ)

/**
 * @brief weak_ptr（非所有の観測者）で、shared_ptrの実体がまだ生きているかを安全に確認します。
 * @details
 * - `weak_ptr` はshared_ptrが管理する実体を「所有せずに観測だけ」できるポインタ。
 *   参照カウントには影響しない（weak_ptrが何個あっても、実体はshared_ptrの数だけで決まる）。
 * - 実体が既に解放されているかもしれないので、使う前に `lock()` で「まだ生きているか」を確認し、
 *   生きていれば一時的な `shared_ptr` を得てから使う。
 * - 典型的な用途: 親子関係で親が子をshared_ptrで持ち、子は親をweak_ptrで持つ設計。
 *   両方をshared_ptrにすると、お互いを参照しあって参照カウントが0にならず、
 *   実体が永遠に解放されない「循環参照」というメモリリークになる。weak_ptrはこれを避けるために使う。
 * @return void
 */
void demonstrateWeakPtrOwnership() {
  printTitle("3c. ownership: weak_ptr (non-owning observer)");

  std::weak_ptr<Sensor> observer;
  {
    std::shared_ptr<Sensor> owner = std::make_shared<Sensor>("weak-target");
    // -> "  [Sensor] construct: weak-target" (コンストラクタのログ)
    observer = owner;  // 所有権は増えない（use_countは1のまま）

    if (std::shared_ptr<Sensor> locked = observer.lock()) {
      std::cout << "while alive: locked=has value (" << locked->name() << ")\n";
      // -> while alive: locked=has value (weak-target)
    }
  }  // owner がスコープを抜けて破棄される → 参照カウントが0になり実体も解放される
     // -> "  [Sensor] destruct: weak-target" (デストラクタのログ)

  if (std::shared_ptr<Sensor> locked = observer.lock()) {
    std::cout << "after original shared_ptr reset: locked=has value (" << locked->name() << ")\n";
  } else {
    std::cout << "after original shared_ptr reset: locked=null (already destroyed)\n";
    // -> after original shared_ptr reset: locked=null (already destroyed)
  }
}

/* ------------------------------------------------------------------------
 * 4. std::vector の復習（基本操作・capacityとCの動的配列との対比）
 * ---------------------------------------------------------------------- */

/**
 * @brief std::vector の基本操作（push_back/at/範囲for/resize）を確認します。
 * @details
 * - Cで`malloc`+手動での要素数管理をしていた「可変長配列」を、vectorは1つの型で提供する。
 * - `operator[]`は範囲チェックをしない（Cの配列と同じ危険さ）が、`at()`は範囲外で例外を投げる。
 * - `resize()`で要素数そのものを変更できる（拡大時は新要素がデフォルト構築される。intなら0）。
 * - `at()`の範囲外アクセスは実際に`try`/`catch`で例外を捕まえて確認する。一方`operator[]`の範囲外
 *   アクセスは例外を投げず未定義動作(UB)になるため、実行結果が環境依存・クラッシュの恐れがあり、
 *   意図的に実行コードとしては書かない（コメントで留める）。
 * - [注意] `e.what()`の文言は標準規格では規定されておらず、標準ライブラリ実装ごとに異なる
 *   （例: libc++は`"vector"`という短い文言、libstdc++はもっと詳細な文言を返す）。
 * @return void
 */
void demonstrateVectorBasics() {
  printTitle("4a. std::vector basics (push_back / at / range-for / resize)");

  std::vector<int> vec;
  vec.push_back(10);
  vec.push_back(20);
  vec.push_back(30);

  std::cout << "after push_back x3: size=" << vec.size() << ", values=[";
  for (size_t i = 0; i < vec.size(); i++) {
    std::cout << vec[i] << (i + 1 < vec.size() ? ", " : "");
  }
  std::cout << "]\n";
  // -> after push_back x3: size=3, values=[10, 20, 30]

  std::cout << "vec.at(1)=" << vec.at(1) << "\n";
  // -> vec.at(1)=20

  vec.resize(5);  // 拡大した分は int のデフォルト値(0)で埋まる
  std::cout << "after resize(5): size=" << vec.size() << ", values=[";
  for (size_t i = 0; i < vec.size(); i++) {
    std::cout << vec[i] << (i + 1 < vec.size() ? ", " : "");
  }
  std::cout << "]\n";
  // -> after resize(5): size=5, values=[10, 20, 30, 0, 0]

  // at()は範囲外アクセスで例外を投げる。実際にtry/catchで実行し、捕まえた例外を出力する。
  try {
    const int outOfRange = vec.at(100);  // 例外はここで投げられる（代入は起こらない）
    std::cout << "vec.at(100)=" << outOfRange << "\n";  // ここには到達しない
  } catch (const std::out_of_range& e) {
    std::cout << "vec.at(100) threw std::out_of_range: " << e.what() << "\n";
    // -> vec.at(100) threw std::out_of_range: vector (libc++の場合。libstdc++等では文言が変わる)
  }

  // [悪い例] operator[]の範囲外アクセスは例外を投げず未定義動作(UB)になる。
  // 結果が環境依存になったり、クラッシュ/メモリ破壊につながったりする可能性があるため、
  // atとは違い意図的に実行コードとしては書かない（安全に実演できる方法がない）。
  // int x = vec[100]; // <- コンパイルは通るが、実行すると未定義動作
}

/**
 * @brief `size()` と `capacity()` の違い、および再確保がCの`realloc`と同じ発想であることを確認します。
 * @details
 * - `size()`: 実際に入っている要素数。`capacity()`: 再確保なしで入る最大要素数。
 * - capacityを超えてpush_backすると、vectorは内部で「新しい大きい領域を確保→全要素をコピー(or move)→
 *   古い領域を解放」を行う。これはCで `realloc` が(必要なら)新領域確保+コピーを行うのと同じ発想。
 *   具体的な増分（何倍に増やすか）は標準規格では未規定で、実装（libstdc++/libc++等）ごとに異なる。
 * - `reserve(n)` で事前にcapacityを確保しておくと、その後のpush_backでの再確保回数を減らせる
 *   （Cで「最初から十分な要素数でmallocしておく」のと同じ最適化）。
 * @return void
 */
void demonstrateVectorCapacityVsCArray() {
  printTitle("4b. vector capacity growth vs C's malloc/realloc array");

  std::vector<int> growing;
  std::cout << "capacity growth while push_back:";
  for (int i = 0; i < 6; i++) {
    growing.push_back(i);
    std::cout << " size=" << growing.size() << " capacity=" << growing.capacity();
    if (i + 1 < 6) {
      std::cout << " ->";
    }
  }
  std::cout << "\n";
  // -> capacity growth while push_back: size=1 capacity=1 -> size=2 capacity=2 -> size=3 capacity=4
  //    -> size=4 capacity=4 -> size=5 capacity=8 -> size=6 capacity=8
  //    （libc++/clangでの実測例。capacityの増分は実装依存のため環境によって数値は変わり得る）

  std::vector<int> reserved;
  reserved.reserve(100);  // Cで言う「最初に十分なサイズでmallocしておく」に相当
  reserved.push_back(1);
  reserved.push_back(2);
  reserved.push_back(3);
  std::cout << "after reserve(100) + push_back x3: size=" << reserved.size()
            << " capacity=" << reserved.capacity() << " (再確保なしで収まる)\n";
  // -> after reserve(100) + push_back x3: size=3 capacity=100 (再確保なしで収まる)
}

/* ------------------------------------------------------------------------
 * 5. C言語との違い一覧（実コードでの対比）
 * ---------------------------------------------------------------------- */
/* [注意] 「メモリ解放(new/delete vs スマートポインタ)」は§2、「構造体(struct/classの機能差)」は
 * §1で既に実コードとして対比済みのため、ここでは重複を避けて再掲しない。 */

/**
 * @brief 動的配列: Cの`malloc`/`realloc`/`free`(手動管理) と C++の`std::vector`(RAII自動管理) を比較します。
 * @details `realloc`は失敗すると元のポインタを無効化しないため、戻り値を別変数で受けてから
 * 元のポインタへ代入する（そのまま代入すると失敗時に元の領域を指すアドレスを失いリークする）。
 * @return void
 */
void demonstrateDynamicArrayCStyleVsCpp() {
  printTitle("5a. dynamic array: C (malloc/realloc/free) vs C++ (std::vector)");

  int* cArray = static_cast<int*>(std::malloc(3 * sizeof(int)));
  if (cArray == nullptr) {
    std::cerr << "malloc failed\n";
    return;
  }
  for (int i = 0; i < 3; i++) {
    cArray[i] = i * 10;
  }
  int* grown = static_cast<int*>(std::realloc(cArray, 5 * sizeof(int)));  // 3->5要素へ再確保
  if (grown == nullptr) {
    std::free(cArray);  // reallocが失敗した場合、元のcArrayは自分でfreeする責任が残る
    std::cerr << "realloc failed\n";
    return;
  }
  cArray = grown;
  cArray[3] = 30;
  cArray[4] = 40;
  std::cout << "[C]   cArray=[";
  for (int i = 0; i < 5; i++) {
    std::cout << cArray[i] << (i + 1 < 5 ? ", " : "");
  }
  std::cout << "]\n";
  // -> [C]   cArray=[0, 10, 20, 30, 40]
  std::free(cArray);  // 使い終わったら必ず自分でfreeする
  cArray = nullptr;

  std::vector<int> cppArray{0, 10, 20};
  cppArray.push_back(30);  // 内部でrealloc相当の再確保が自動的に行われる
  cppArray.push_back(40);
  std::cout << "[C++] cppArray=[";
  for (size_t i = 0; i < cppArray.size(); i++) {
    std::cout << cppArray[i] << (i + 1 < cppArray.size() ? ", " : "");
  }
  std::cout << "]\n";
  // -> [C++] cppArray=[0, 10, 20, 30, 40]  (freeを書く必要はない。スコープを抜けると自動解放)
}

/**
 * @brief 文字列: Cの`char*`+手動NUL終端管理 と C++の`std::string` を比較します。
 * @details `strncpy`は書き込み先が短いとNUL終端を保証しない仕様のため、手動で最終バイトへ
 * `'\0'`を代入している。`strncat`も連結先の残り容量を自分で計算して渡す必要がある。
 * @return void
 */
void demonstrateStringCStyleVsCpp() {
  printTitle("5b. string: C (char* + manual NUL handling) vs C++ (std::string)");

  char cGreeting[16];  // 事前にバッファサイズを決め打ちする必要がある
  std::strncpy(cGreeting, "Hello", sizeof(cGreeting) - 1);
  cGreeting[sizeof(cGreeting) - 1] = '\0';  // strncpyは終端を保証しないため手動で終端させる
  std::strncat(cGreeting, ", C!", sizeof(cGreeting) - std::strlen(cGreeting) - 1);  // 連結も残り容量計算が必要
  std::cout << "[C]   cGreeting=" << cGreeting << " (len=" << std::strlen(cGreeting) << ")\n";
  // -> [C]   cGreeting=Hello, C! (len=9)

  std::string cppGreeting = "Hello";
  cppGreeting += ", C++!";  // バッファサイズを意識する必要はない（自動的に伸びる）
  std::cout << "[C++] cppGreeting=" << cppGreeting << " (len=" << cppGreeting.size() << ")\n";
  // -> [C++] cppGreeting=Hello, C++! (len=11)
}

int addInt(int a, int b) {  // Cならこう「名前を変えて」区別するしかない
  return a + b;
}
double addDouble(double a, double b) {
  return a + b;
}

int add(int a, int b) {  // C++なら同じ名前 `add` のまま、引数の型で区別できる
  return a + b;
}
double add(double a, double b) {
  return a + b;
}

/**
 * @brief 関数: Cは同名関数を宣言できない と C++は引数の型/個数でオーバーロードできる、を比較します。
 * @details 本当のCコードはこのファイル（.cpp）では実行できないため、「Cなら名前を変えるしかない」
 * ことを示す代わりとして `addInt`/`addDouble` という名前分けした関数で再現している。
 * @return void
 */
void demonstrateFunctionOverloadCStyleVsCpp() {
  printTitle("5c. function overload: C (name must differ) vs C++ (overload by parameter type)");

  std::cout << "[C]   addInt(1, 2)=" << addInt(1, 2) << ", addDouble(1.5, 2.5)=" << addDouble(1.5, 2.5)
            << " (名前を変えないと共存できない)\n";
  // -> [C]   addInt(1, 2)=3, addDouble(1.5, 2.5)=4 (名前を変えないと共存できない)
  std::cout << "[C++] add(1, 2)=" << add(1, 2) << ", add(1.5, 2.5)=" << add(1.5, 2.5)
            << " (同じ名前addのまま共存できる)\n";
  // -> [C++] add(1, 2)=3, add(1.5, 2.5)=4 (同じ名前addのまま共存できる)
}

void report(int value) {  // int版
  std::cout << "  report(int) called with value=" << value << "\n";
}
void report(const char* text) {  // ポインタ版
  std::cout << "  report(const char*) called with text=" << (text ? text : "(null)") << "\n";
}

/**
 * @brief 空ポインタ: `0`(NULL相当)がオーバーロード解決を誤らせる例 と `nullptr`が正しく解決される例を確認します。
 * @details 整数リテラル`0`は`report(int)`に対して完全一致（変換なし）、`report(const char*)`に対しては
 * ポインタ変換（一段階の変換）が必要なため、`report(int)`が優先して選ばれる。「ポインタのつもりで書いた0」が
 * 意図せず整数版を呼んでしまう典型例。
 * [注意] `NULL`マクロ自体は本来ここでも同じ問題を再現するはずだが、環境（コンパイラ/標準ライブラリ）によっては
 * `NULL`が`__null`という特殊なビルトイン型で定義されており、`report(int)`と`report(const char*)`の
 * どちらとも同じ順位で変換可能なため「曖昧呼び出し」の**コンパイルエラー**になることがある
 * （実際、本ファイルをApple clang/libc++でビルドすると`report(NULL)`はコンパイルエラーになった）。
 * そのため実行できる形として、リテラル`0`で同じ問題を再現している。
 * `nullptr`は「ポインタ専用の型」を持つため、このような誤解決も曖昧エラーも起こらない。
 * @return void
 */
void demonstrateNullVsNullptrOverloadResolution() {
  printTitle("5d. null pointer: 0(NULL相当) can pick the wrong overload vs nullptr picks correctly");

  std::cout << "[C-ish] report(0):\n";
  report(0);
  // -> "  report(int) called with value=0" （ポインタのつもりでも整数版が呼ばれてしまう）

  std::cout << "[C++]   report(nullptr):\n";
  report(nullptr);
  // -> "  report(const char*) called with text=(null)" （意図通りポインタ版が呼ばれる）
}

int iot_sensor_read() {  // Cならではの「関数名にprefixを付けて衝突を避ける」慣習
  return 42;
}

namespace iot {
int sensorRead() {  // C++なら名前空間で衝突を避けられる（呼ぶ側は iot::sensorRead()）
  return 42;
}
}  // namespace iot

/**
 * @brief 名前の衝突回避: Cの`prefix`手動回避 と C++の`namespace` を比較します。
 * @return void
 */
void demonstrateNamespaceVsPrefix() {
  printTitle("5e. avoiding name collisions: C (prefix convention) vs C++ (namespace)");

  std::cout << "[C]   iot_sensor_read()=" << iot_sensor_read() << "\n";
  // -> [C]   iot_sensor_read()=42
  std::cout << "[C++] iot::sensorRead()=" << iot::sensorRead() << "\n";
  // -> [C++] iot::sensorRead()=42
}

/**
 * @brief 型変換: Cでもよく見る暗黙変換まかせ と C++推奨の`static_cast`明示変換を比較します。
 * @details 単純代入`int x = doubleValue;`の暗黙の縮小変換は、`-Wall -Wextra`でも
 * 警告されないことが多い（`-Wconversion`等の追加フラグが必要）。これが「気付きにくい」所以。
 * @return void
 */
void demonstrateTypeConversionCStyleVsCpp() {
  printTitle("5f. type conversion: C-style implicit vs C++ explicit static_cast");

  const double preciseValue = 3.99;

  const int implicitTruncated = preciseValue;  // 暗黙の縮小変換。-Wall/-Wextraでも無警告なことが多い
  std::cout << "[C-ish] int implicitTruncated = preciseValue; -> " << implicitTruncated
            << " (意図せず切り捨てられたのか読み手には分かりにくい)\n";
  // -> [C-ish] int implicitTruncated = preciseValue; -> 3 (...)

  const int explicitTruncated = static_cast<int>(preciseValue);  // 変換の意図をコードで明示する
  std::cout << "[C++]   static_cast<int>(preciseValue) -> " << explicitTruncated << " (意図が明確)\n";
  // -> [C++]   static_cast<int>(preciseValue) -> 3 (意図が明確)
}

enum class DivideErrorCode { ok = 0, divideByZero = 1 };  // Cスタイル: エラーをint相当の戻り値で表す

DivideErrorCode divideCStyle(int a, int b, int* outResult) {
  if (b == 0) {
    return DivideErrorCode::divideByZero;  // 呼び出し側が戻り値を毎回チェックする必要がある
  }
  *outResult = a / b;
  return DivideErrorCode::ok;
}

int divideCpp(int a, int b) {  // C++スタイル: 異常系はthrowで知らせる
  if (b == 0) {
    throw std::invalid_argument("divideCpp: b must not be zero");
  }
  return a / b;
}

/**
 * @brief エラー処理: Cの戻り値+errno相当(ここではenum戻り値で再現) と C++の例外(throw/try/catch)を比較します。
 * @details Cスタイルは「戻り値を毎回ifでチェックし忘れる」リスクがある。C++の例外は
 * 呼び出し元まで自動的に伝播するため、チェック漏れによる異常値の見逃しが起きにくい
 * （ただし例外を使うかどうかは設計判断であり、常に例外が優れているわけではない）。
 * @return void
 */
void demonstrateErrorHandlingCStyleVsCpp() {
  printTitle("5g. error handling: C-style (return code) vs C++ (exceptions)");

  int cResult = 0;
  const DivideErrorCode cError = divideCStyle(10, 0, &cResult);
  std::cout << "[C-ish] divideCStyle(10, 0): error="
            << (cError == DivideErrorCode::divideByZero ? "divideByZero" : "ok")
            << " (戻り値を毎回ifでチェックする必要がある)\n";
  // -> [C-ish] divideCStyle(10, 0): error=divideByZero (...)

  try {
    const int cppResult = divideCpp(10, 0);
    std::cout << "[C++]   divideCpp(10, 0)=" << cppResult << "\n";  // ここには到達しない
  } catch (const std::invalid_argument& e) {
    std::cout << "[C++]   divideCpp(10, 0) threw: " << e.what() << "\n";
    // -> [C++]   divideCpp(10, 0) threw: divideCpp: b must not be zero
  }
}

#define SQUARE_MACRO(x) ((x) * (x))  // [悪い例の温床] 引数をそのまま式へ埋め込むマクロ

constexpr int squareConstexpr(int x) {  // C++: コンパイル時にも実行時にも使える普通の関数
  return x * x;
}

/**
 * @brief コンパイル時計算: Cの`#define`マクロの引数二重評価という落とし穴 と C++の`constexpr`を比較します。
 * @details `SQUARE_MACRO(x)`は`((x) * (x))`に展開されるため、副作用のある式（呼ぶたびに値が変わる関数呼び出し等）
 * を渡すと、その式が2回評価されてしまう。`constexpr`関数は普通の関数と同じ引数評価規則
 * （1回だけ評価）に従うため、この問題が起きない。
 * @return void
 */
void demonstrateMacroVsConstexpr() {
  printTitle("5h. compile-time: C #define macro pitfall vs C++ constexpr");

  int counter = 0;
  auto next = [&counter]() { return ++counter; };  // 呼ぶたびに1, 2, 3... と増える値を返す

  const int macroResult = SQUARE_MACRO(next());  // 展開後は ((next()) * (next())) となり、next()が2回呼ばれる
  std::cout << "[C]   SQUARE_MACRO(next()) = " << macroResult << ", counter is now " << counter
            << " (引数が意図せず2回評価された)\n";
  // -> [C]   SQUARE_MACRO(next()) = 2, counter is now 2 (1*2=2、counterが2回分進んだ)

  counter = 0;
  const int constexprResult = squareConstexpr(next());  // 普通の関数と同じく、next()は1回だけ呼ばれる
  std::cout << "[C++] squareConstexpr(next()) = " << constexprResult << ", counter is now " << counter
            << " (1回だけ評価された)\n";
  // -> [C++] squareConstexpr(next()) = 1, counter is now 1 (1*1=1、counterは1回分しか進まない)

  static_assert(squareConstexpr(4) == 16, "constexprはコンパイル時にも評価できる");
}

/* ------------------------------------------------------------------------
 * 6. 便利なC++機能一覧（簡易チートシート）
 * ---------------------------------------------------------------------- */

/**
 * @brief よく使う便利なC++機能を、それぞれ短い実コードで確認します。
 * @details 深掘りは主に `cpp11.cpp`〜`cpp23.cpp`、および本ファイル§1〜§5,§8 を参照。
 * ここでは「名前と最小限の使い方」を1箇所で思い出せるようにするための索引とする。
 * @return void
 */
void demonstrateUsefulCppFeatureCheatsheet() {
  printTitle("6. 便利なC++機能一覧（簡易チートシート、コード例つき）");

  // auto: 型推論。右辺から型を決めてもらう (詳細: cpp11.cpp)
  auto autoNumber = 42;  // int と推論される
  std::cout << "auto: autoNumber=" << autoNumber << " (型はintと推論)\n";
  // -> auto: autoNumber=42 (型はintと推論)

  // 範囲for: イテレータを直接いじらずコンテナの各要素を回せる
  const std::vector<int> rangeForData{1, 2, 3};
  int rangeForSum = 0;
  for (const auto& v : rangeForData) {
    rangeForSum += v;
  }
  std::cout << "range-for: sum=" << rangeForSum << "\n";
  // -> range-for: sum=6

  // nullptr: 型安全なヌルポインタ (詳細: 本ファイル§5d、cpp11.cpp)
  int* nullptrSample = nullptr;
  std::cout << "nullptr: nullptrSample is " << (nullptrSample == nullptr ? "null" : "not null") << "\n";
  // -> nullptr: nullptrSample is null

  // enum class: スコープ付き列挙型。名前の衝突を防ぐ (詳細: cpp11.cpp)
  enum class CheatsheetColor { red, green, blue };
  const CheatsheetColor favoriteColor = CheatsheetColor::green;  // ColorKind:: を必ず付けるので衝突しない
  std::cout << "enum class: favoriteColor is "
            << (favoriteColor == CheatsheetColor::green ? "green" : "other") << "\n";
  // -> enum class: favoriteColor is green

  // ラムダ式: その場で作れる無名関数 (詳細: 本ファイル§8、cpp11.cpp)
  const auto square = [](int x) { return x * x; };
  std::cout << "lambda: square(6)=" << square(6) << "\n";
  // -> lambda: square(6)=36

  // スマートポインタ: RAIIでメモリ管理を自動化 (詳細: 本ファイル§2,3)
  const std::unique_ptr<int> cheatsheetPtr = std::make_unique<int>(7);
  std::cout << "unique_ptr: *cheatsheetPtr=" << *cheatsheetPtr << " (スコープを抜けると自動解放)\n";
  // -> unique_ptr: *cheatsheetPtr=7 (スコープを抜けると自動解放)

  // STLコンテナ: 用途別の可変長データ構造 (vectorの詳細は本ファイル§4)
  std::map<std::string, int> sensorIdByName{{"temp", 1}, {"humidity", 2}};
  std::cout << "map: sensorIdByName[\"humidity\"]=" << sensorIdByName["humidity"] << "\n";
  // -> map: sensorIdByName["humidity"]=2

  // <algorithm>: 手書きループの代わりに使える標準アルゴリズム群 (sortの例は本ファイル§8c)
  const std::vector<int> algoData{5, 3, 8, 1, 9};
  const auto foundIt = std::find(algoData.begin(), algoData.end(), 8);
  std::cout << "<algorithm>: std::find(8) " << (foundIt != algoData.end() ? "found" : "not found") << "\n";
  // -> <algorithm>: std::find(8) found

  // 例外処理: try/catch/throw (別の例は本ファイル§4a, §5g)
  try {
    throw std::runtime_error("cheatsheet demo error");
  } catch (const std::exception& e) {
    std::cout << "try/catch: caught \"" << e.what() << "\"\n";
    // -> try/catch: caught "cheatsheet demo error"
  }

  // 構造化束縛 (C++17): 複数値を一度に受け取る (詳細: cpp17.cpp)
  const std::pair<int, std::string> structuredBindingSource{1, "one"};
  const auto& [bindingId, bindingName] = structuredBindingSource;
  std::cout << "structured bindings: id=" << bindingId << " name=" << bindingName << "\n";
  // -> structured bindings: id=1 name=one

  // std::optional (C++17): 「値があるかもしれない」を型で表現する (詳細: cpp17.cpp)
  const std::optional<int> maybeValue = 5;
  std::cout << "std::optional: has_value=" << maybeValue.has_value() << " value=" << maybeValue.value_or(-1)
            << "\n";
  // -> std::optional: has_value=1 value=5

  // std::string_view (C++17): 文字列をコピーせず参照するための軽量な型 (詳細: cpp17.cpp)
  const std::string_view viewSample = "string_view sample";
  std::cout << "std::string_view: size=" << viewSample.size() << " (コピーなしで参照)\n";
  // -> std::string_view: size=18 (コピーなしで参照)

  // std::span / concepts (C++20): 本ファイルは-std=c++17を前提にしているため実行はしない。
  // 実行できる例は cpp20.cpp を参照。
  std::cout << "std::span / concepts: C++20専用のため、実行例は cpp20.cpp を参照\n";

  // テンプレート: 型ごとに関数/クラスを書き直さずに済む (実例は本ファイル§8b genericAddAsTemplate)
  std::cout << "template: 実例は§8b genericAddAsTemplate を参照\n";

  // const / constexpr: 「変更されない」「コンパイル時に計算できる」ことを明示する (別の例は本ファイル§5h)
  constexpr int constexprCheatsheetValue = 6 * 7;  // コンパイル時に計算される
  std::cout << "constexpr: constexprCheatsheetValue=" << constexprCheatsheetValue << " (コンパイル時計算)\n";
  // -> constexpr: constexprCheatsheetValue=42 (コンパイル時計算)
}

/* ------------------------------------------------------------------------
 * 7. 標準入出力ストリーム（<</>>の使い方、マニピュレータ、printf系との比較）
 * ---------------------------------------------------------------------- */

/**
 * @brief `std::cout`のチェーン出力と`printf`の書き方を比較します。
 * @details
 * - printf: `printf("chained: a=%d, b=%d, c=%d\n", a, b, c);`
 *   → 型を`%d`/`%s`/`%f`などで自己申告する。型と書式指定子が食い違ってもコンパイラの検出は
 *     保証されず（一部コンパイラは警告してくれるが言語仕様上の保証ではない）、実行時のバグや
 *     クラッシュにつながりうる。
 * - cout: `std::cout << "chained: a=" << a << ", b=" << b << ", c=" << c << "\n";`
 *   → `<<` は渡した値の実際の型を見て自動的に正しい出力方法を選ぶ（型安全）。書式指定子を
 *     書き間違えるというバグのクラスがそもそも存在しない。
 * - 一方でprintfは「書式文字列1つで全体のレイアウトを見渡せる」利点があり、複雑な整形では
 *   printf系の方が見やすいと感じる人もいる（好みや用途で使い分けられる）。
 * @return void
 */
void demonstrateCoutVsPrintfBasics() {
  printTitle("7a. std::cout chaining vs printf");

  const int a = 1;
  const int b = 2;
  const int c = 3;

  std::printf("[printf] chained: a=%d, b=%d, c=%d\n", a, b, c);
  // -> [printf] chained: a=1, b=2, c=3
  std::cout << "[cout]   chained: a=" << a << ", b=" << b << ", c=" << c << "\n";
  // -> [cout]   chained: a=1, b=2, c=3
}

/**
 * @brief 幅/桁埋め/小数点桁数/真偽値/基数指定を、`printf`の書式指定子と`cout`のマニピュレータで比較します。
 * @details
 * - 0埋め幅4:  printf `"%04d"`   / cout `std::setw(4) << std::setfill('0')`
 * - 小数点2桁: printf `"%.2f"`   / cout `std::fixed << std::setprecision(2)`
 * - bool表示:  printfはbool専用の指定子がなく`%d`で0/1になる / coutは`std::boolalpha`でtrue/falseにできる
 * - 16進数:    printf `"%x"`     / cout `std::hex`
 * - [重要] coutのマニピュレータ（`fixed`/`hex`/`boolalpha`等、`setw`を除く）は「次に戻すまでずっと効く」。
 *   使い終わったら明示的に既定へ戻す（`std::defaultfloat`・`std::dec`・`std::noboolalpha`等）のを忘れずに。
 *   `setw`だけは次の1回の出力のみ有効という違いにも注意する。
 * @return void
 */
void demonstrateCoutVsPrintfFormatting() {
  printTitle("7b. stream manipulators vs printf format specifiers");

  std::printf("[printf] zero-padded: [%04d]\n", 42);
  // -> [printf] zero-padded: [0042]
  std::cout << "[cout]   zero-padded: [" << std::setw(4) << std::setfill('0') << 42 << "]\n";
  // -> [cout]   zero-padded: [0042]

  const double pi = 3.14159265;
  std::printf("[printf] fixed 2 digits: %.2f\n", pi);
  // -> [printf] fixed 2 digits: 3.14
  std::cout << "[cout]   fixed 2 digits: " << std::fixed << std::setprecision(2) << pi << "\n";
  // -> [cout]   fixed 2 digits: 3.14
  std::cout << std::defaultfloat;  // 後続の出力に影響しないよう既定表記へ戻す

  std::printf("[printf] bool as %%d: %d\n", static_cast<int>(true));
  // -> [printf] bool as %d: 1
  std::cout << "[cout]   bool as boolalpha: " << std::boolalpha << true << "\n";
  // -> [cout]   bool as boolalpha: true
  std::cout << std::noboolalpha;  // 既定へ戻す

  std::printf("[printf] hex: %x\n", 42);
  // -> [printf] hex: 2a
  std::cout << "[cout]   hex: " << std::hex << 42 << std::dec << "\n";  // 出力後すぐdecへ戻す
  // -> [cout]   hex: 2a
}

/**
 * @brief 文字列の組み立て・分解を、`ostringstream`/`istringstream`と`snprintf`/`sscanf`で比較します。
 * @details
 * - 組み立て: Cは `snprintf(buf, sizeof(buf), "sensor-id=%d, temp=%.1f", 7, 25.5);` のように
 *   「事前にバッファサイズを自分で見積もる」必要がある（小さすぎると出力が切り詰められる）。
 *   C++の`ostringstream`は内部の`std::string`が自動で伸びるため、サイズを気にしなくてよい。
 * - 分解: Cは `sscanf(str, "%63s %d %d", name, &value, &ok);` のように、`%s`にバッファ長の上限
 *   （ここでは`%63s`）を必ず書かないとバッファオーバーフローの危険がある。
 *   C++の`istringstream` + `std::string`は、読み取った分だけ自動的にサイズが確保されるため、
 *   長さ上限を気にする必要がない。
 * - `istringstream`は`std::cin >> x;`と全く同じ書き方で読み取れる。このファイルでは自動実行のため
 *   対話的な`std::cin`の代わりに使っている。
 * @return void
 */
void demonstrateStringStreamsVsCStdio() {
  printTitle("7c. ostringstream/istringstream vs snprintf/sscanf");

  char buf[64];
  std::snprintf(buf, sizeof(buf), "sensor-id=%d, temp=%.1f", 7, 25.5);
  std::cout << "[snprintf] built string: " << buf << "\n";
  // -> [snprintf] built string: sensor-id=7, temp=25.5

  std::ostringstream builder;
  builder << "sensor-id=" << 7 << ", temp=" << 25.5;
  std::cout << "[ostringstream] built string: " << builder.str() << "\n";
  // -> [ostringstream] built string: sensor-id=7, temp=25.5

  char nameBuf[64] = {0};
  int scannedValue = 0;
  int scannedOk = 0;
  std::sscanf("sensor-7 42 1", "%63s %d %d", nameBuf, &scannedValue, &scannedOk);
  std::cout << "[sscanf] parsed: name=" << nameBuf << " value=" << scannedValue
            << " ok=" << scannedOk << "\n";
  // -> [sscanf] parsed: name=sensor-7 value=42 ok=1

  std::istringstream parser("sensor-7 42 1");
  std::string parsedName;
  int parsedValue = 0;
  bool parsedOk = false;
  parser >> parsedName >> parsedValue >> parsedOk;  // 空白区切りで順番に読み取る（std::cin >> と同じ書き方）
  std::cout << "[istringstream] parsed: name=" << parsedName << " value=" << parsedValue
            << " ok=" << parsedOk << "\n";
  // -> [istringstream] parsed: name=sensor-7 value=42 ok=1
}

/**
 * @brief 独自の構造体を出力する場合の「毎回書き方」(printf)と「1回定義すれば使い回せる」(operator<<)を比較します。
 * @details
 * - printfでは構造体をそのまま渡す書式指定子がないため、出力するたびに
 *   `printf("(%.1f, %.1f)\n", p.x, p.y);` のようにメンバを展開して書く必要がある
 *   （出力箇所が増えるほど同じ展開コードが増える）。
 * - C++では `std::ostream& operator<<(std::ostream&, const Point2D&)` を1回定義しておけば、
 *   以後はどこでも `std::cout << p;` と書くだけでよい（`std::cout << a << p << b;` のように
 *   他の出力ともチェーンできる）。戻り値が受け取った`os`（参照）なのがチェーンできる理由。
 * @return void
 */
struct Point2D {
  double x;
  double y;
};

std::ostream& operator<<(std::ostream& os, const Point2D& p) {
  os << "(" << p.x << ", " << p.y << ")";
  return os;
}

void demonstrateCustomOperatorOverload() {
  printTitle("7d. printing a custom type: printf (repeat every time) vs operator<< (define once)");

  const Point2D p{1.5, 2.5};
  std::printf("[printf] point: (%.1f, %.1f)\n", p.x, p.y);
  // -> [printf] point: (1.5, 2.5)
  std::cout << "[cout]   point: " << p << "\n";
  // -> [cout]   point: (1.5, 2.5)
}

/* ------------------------------------------------------------------------
 * 8. ラムダ式を詳しく（関数/functorとの比較つき）
 * ---------------------------------------------------------------------- */

/**
 * @brief ラムダのキャプチャを、「普通の関数で書いた場合」「昔ながらの関数オブジェクト(functor)で書いた場合」と比較します。
 * @details
 * ラムダ式の基本形: `[キャプチャ](引数) -> 戻り値型 { 処理 }`（戻り値型は推論できれば省略可）。
 * - `[x]` 値キャプチャ / `[&x]` 参照キャプチャ / `[=]` 全部値 / `[&]` 全部参照 / `[x, &y]` 混在、
 *   のように書き分けられる。
 * - 普通の関数は外側の変数を「自動でキャプチャ」できない。同じことをしたければ、
 *   キャプチャしたい値を毎回引数として渡す必要がある（下の `addByValueAsFunction` を参照）。
 * - ラムダ `[base](int x){ return base + x; }` がコンパイルされると、実際には
 *   「`base`をメンバ変数として持ち、`operator()`で呼び出せるクラス」に近いものが生成される。
 *   これを手で書いたのが下の `AddByValueFunctor`（C++11でラムダが追加される前の定番の書き方）。
 *   つまりラムダは「その場でfunctorを書ける簡潔な構文」と言い換えられる。
 * - `mutable`: 値キャプチャした変数は本来ラムダ内で変更不可（読み取り専用のコピー）だが、
 *   `mutable`を付けるとラムダ内だけで変更できるようになる（外側の変数には影響しない）。
 * @return void
 */
int addByValueAsFunction(int base, int x) {  // 普通の関数: baseを毎回引数で渡す必要がある
  return base + x;
}

struct AddByValueFunctor {  // 昔ながらの「関数オブジェクト」: メンバ変数でbaseを保持する
  int base;
  explicit AddByValueFunctor(int b) : base(b) {}
  int operator()(int x) const { return base + x; }
};

void demonstrateLambdaVsFunctionVsFunctor() {
  printTitle("8a. lambda capture vs plain function vs old-style functor");

  // [注意] baseをconstにすると、コンパイラが「定数式なのでキャプチャ不要」と判断でき、
  // -Wunused-lambda-capture 警告が出ることがある。キャプチャの意味を素直に確認するため、
  // ここではあえて非const変数にしている。
  int base = 10;

  const auto lambdaVersion = [base](int x) { return base + x; };
  const AddByValueFunctor functorVersion(base);

  std::cout << "lambda (auto-captures base):          " << lambdaVersion(5) << "\n";
  // -> lambda (auto-captures base):          15
  std::cout << "plain function (base passed by hand): " << addByValueAsFunction(base, 5) << "\n";
  // -> plain function (base passed by hand): 15
  std::cout << "functor (base stored as member):      " << functorVersion(5) << "\n";
  // -> functor (base stored as member):      15

  int counter = 0;
  auto mutableCounter = [counter]() mutable {
    counter++;  // mutableがないとコンパイルエラー（値キャプチャは既定で読み取り専用）
    return counter;
  };
  std::cout << "mutable lambda called twice: " << mutableCounter() << ", " << mutableCounter() << "\n";
  // -> mutable lambda called twice: 1, 2 （呼び出しごとに内部コピーが増える）
  std::cout << "counter after calls (unaffected): " << counter << "\n";
  // -> counter after calls (unaffected): 0 （外側のcounterは値キャプチャなので変化しない）

  // [悪い例] mutableなしで値キャプチャした変数を変更しようとするとコンパイルエラーになるため、
  // 実行コードにはできない。あえて#if 0で無効化して残す。
  // 実際に有効化すると、gccなら概ね次のようなエラーになる:
  //   error: increment of read-only variable 'counter'
#if 0
  auto bad = [counter]() { counter++; return counter; };  // <- コンパイルエラー
#endif
}

/**
 * @brief ジェネリックラムダ(`auto`引数, C++14)と、同等のテンプレート関数を比較します。
 * @details
 * - ジェネリックラムダ `[](auto a, auto b){ return a + b; }` は、呼ばれた実引数の型ごとに
 *   コンパイラが内部的に`operator()`のテンプレートを生成する。
 * - これは下の `genericAddAsTemplate` のような「テンプレート関数」を書くのとほぼ同じ効果。
 *   ラムダ版は「その場で書けて、名前を考えなくてよい」という手軽さが利点。
 * @return void
 */
template <typename T>
T genericAddAsTemplate(T a, T b) {
  return a + b;
}

void demonstrateGenericLambdaVsTemplateFunction() {
  printTitle("8b. generic lambda (auto parameter) vs template function");

  const auto genericAddLambda = [](auto a, auto b) { return a + b; };

  std::cout << "lambda genericAdd(1, 2)=" << genericAddLambda(1, 2) << "\n";
  // -> lambda genericAdd(1, 2)=3
  std::cout << "template function genericAdd(1, 2)=" << genericAddAsTemplate(1, 2) << "\n";
  // -> template function genericAdd(1, 2)=3
  std::cout << "lambda genericAdd(1.5, 2.5)=" << genericAddLambda(1.5, 2.5) << "\n";
  // -> lambda genericAdd(1.5, 2.5)=4
  std::cout << "template function genericAdd(1.5, 2.5)=" << genericAddAsTemplate(1.5, 2.5) << "\n";
  // -> template function genericAdd(1.5, 2.5)=4
}

/**
 * @brief `std::sort`の比較関数を、その場で書くラムダと、名前付きの普通の関数で比較します。
 * @details
 * - キャプチャを使わないラムダ（`[]`で始まる）は、実質「その場限りの名前のない関数」であり、
 *   通常の関数と同じように関数ポインタとしても渡せる。
 * - 使い回す予定がある/複雑な比較ロジックには名前付き関数の方が読みやすいこともあるが、
 *   「その場だけで使う短い処理」はラムダの方が呼び出し箇所の近くに書けて見通しがよい。
 * @return void
 */
bool descendingCompare(int a, int b) {  // 名前付きの普通の関数（キャプチャなしラムダと同じ形）
  return a > b;
}

void demonstrateLambdaVsNamedFunctionForSort() {
  printTitle("8c. lambda vs named function as std::sort comparator");

  std::vector<int> byFunction{3, 1, 4, 5, 2};
  std::sort(byFunction.begin(), byFunction.end(), descendingCompare);
  std::cout << "sorted via named function descendingCompare: [";
  for (size_t i = 0; i < byFunction.size(); i++) {
    std::cout << byFunction[i] << (i + 1 < byFunction.size() ? ", " : "");
  }
  std::cout << "]\n";
  // -> sorted via named function descendingCompare: [5, 4, 3, 2, 1]

  std::vector<int> byLambda{3, 1, 4, 5, 2};
  std::sort(byLambda.begin(), byLambda.end(), [](int a, int b) { return a > b; });
  std::cout << "sorted via inline lambda (same result):      [";
  for (size_t i = 0; i < byLambda.size(); i++) {
    std::cout << byLambda[i] << (i + 1 < byLambda.size() ? ", " : "");
  }
  std::cout << "]\n";
  // -> sorted via inline lambda (same result):      [5, 4, 3, 2, 1]
}

/**
 * @brief `std::function`型のコールバック引数へ、名前付き関数とラムダの両方を渡せることを確認します。
 * @details
 * - `std::function<int(int,int)>` は「このシグネチャ(int,int)->intで呼べるものなら何でも」を表す型。
 *   名前付き関数・キャプチャなしラムダ・キャプチャありラムダ・functorのどれでも代入できる。
 * - 後半では「moveキャプチャ」（C++14の初期化キャプチャ）で `unique_ptr` の所有権をラムダの中へ
 *   持ち込む例も確認する。`unique_ptr`はコピーできないため、これは通常の関数では書けない
 *   （関数は「値」を保持できず、呼ばれるたびに引数を受け取るだけのため）。ラムダならではの使い方。
 * @return void
 */
int multiplyFunction(int a, int b) {  // 名前付き関数。std::functionへそのまま代入できる
  return a * b;
}

int callWithCallback(int a, int b, const std::function<int(int, int)>& op) {
  return op(a, b);
}

void demonstrateLambdaAsStdFunctionVsNamedFunction() {
  printTitle("8d. std::function callback: named function vs lambda (+ move-capture)");

  std::cout << "callWithCallback(named function multiplyFunction): "
            << callWithCallback(5, 6, multiplyFunction) << "\n";
  // -> callWithCallback(named function multiplyFunction): 30
  std::cout << "callWithCallback(lambda):                           "
            << callWithCallback(5, 6, [](int a, int b) { return a * b; }) << "\n";
  // -> callWithCallback(lambda):                           30

  std::unique_ptr<Sensor> uniqueSensor = std::make_unique<Sensor>("move-captured");
  // -> "  [Sensor] construct: move-captured" (コンストラクタのログ)
  // [重要] unique_ptrはコピーできないので、通常の値キャプチャ[uniqueSensor]はコンパイルエラーになる。
  // 初期化キャプチャ(C++14)で「ラムダ内の新しい変数へmoveする」ことで所有権ごと持ち込める。
  // これは「関数」では表現できない、ラムダ(や手書きfunctor)だけができること。
  auto moveCapturingLambda = [ptr = std::move(uniqueSensor)]() { return ptr->name(); };
  std::cout << "moveCapturingLambda() -> name=" << moveCapturingLambda() << "\n";
  // -> moveCapturingLambda() -> name=move-captured
}  // -> ここでmoveCapturingLambdaが破棄され "  [Sensor] destruct: move-captured" (デストラクタのログ)

/**
 * @brief 再帰処理を、「普通の再帰関数」と「std::functionを使った再帰ラムダ」で比較します。
 * @details
 * - ラムダは自分自身の名前を持たないため、素直には自分を呼び出せない。
 *   `std::function`型の変数へ代入し、それを（参照キャプチャで）自分の中から呼ぶことで再帰にできる。
 * - ただしこの書き方は`std::function`の間接呼び出し相当のオーバーヘッドがあり、
 *   単純な再帰なら下の `factorialAsFunction` のような普通の再帰関数の方がシンプルで高速。
 *   「外側の状態をキャプチャしつつ再帰したい」といった特別な事情がない限り、
 *   再帰は普通の名前付き関数で書く方がよい。
 * @return void
 */
int factorialAsFunction(int n) {  // 普通の再帰関数。こちらの方がシンプル
  return (n <= 1) ? 1 : n * factorialAsFunction(n - 1);
}

void demonstrateRecursiveLambdaVsFunction() {
  printTitle("8e. recursive lambda (std::function trick) vs plain recursive function");

  std::cout << "plain recursive function: factorial(5) = " << factorialAsFunction(5) << "\n";
  // -> plain recursive function: factorial(5) = 120

  std::function<int(int)> factorialLambda = [&factorialLambda](int n) -> int {
    return (n <= 1) ? 1 : n * factorialLambda(n - 1);
  };
  std::cout << "recursive lambda (std::function trick): factorial(5) = " << factorialLambda(5) << "\n";
  // -> recursive lambda (std::function trick): factorial(5) = 120
}

}  // namespace

/**
 * @brief エントリポイント。ファイル冒頭 @details の1〜8を順番に実行します。
 * @return int 終了コード
 */
int main() {
  try {
    demonstrateClassVsStruct();
    demonstrateRawNewDelete();
    demonstrateSmartPointerBasics();
    demonstrateUniquePtrOwnership();
    demonstrateSharedPtrOwnership();
    demonstrateWeakPtrOwnership();
    demonstrateVectorBasics();
    demonstrateVectorCapacityVsCArray();
    demonstrateDynamicArrayCStyleVsCpp();
    demonstrateStringCStyleVsCpp();
    demonstrateFunctionOverloadCStyleVsCpp();
    demonstrateNullVsNullptrOverloadResolution();
    demonstrateNamespaceVsPrefix();
    demonstrateTypeConversionCStyleVsCpp();
    demonstrateErrorHandlingCStyleVsCpp();
    demonstrateMacroVsConstexpr();
    demonstrateUsefulCppFeatureCheatsheet();
    demonstrateCoutVsPrintfBasics();
    demonstrateCoutVsPrintfFormatting();
    demonstrateStringStreamsVsCStdio();
    demonstrateCustomOperatorOverload();
    demonstrateLambdaVsFunctionVsFunctor();
    demonstrateGenericLambdaVsTemplateFunction();
    demonstrateLambdaVsNamedFunctionForSort();
    demonstrateLambdaAsStdFunctionVsNamedFunction();
    demonstrateRecursiveLambdaVsFunction();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[error] function=main(file=cpp_basics_class_memory_ownership.cpp)"
              << " message=\"" << ex.what() << "\"\n";
    return 1;
  } catch (...) {
    std::cerr << "[error] function=main(file=cpp_basics_class_memory_ownership.cpp)"
              << " message=\"unknown exception\"\n";
    return 2;
  }
}
