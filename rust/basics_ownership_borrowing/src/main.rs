//! Rust 所有権・借用 深掘りサンプル。
//!
//! [重要] C++の unique_ptr/shared_ptr/weak_ptr
//! (`cpp_m/cpp_basics_class_memory_ownership.cpp`) と直接対比しながら、
//! Rustの所有権(ownership)・借用(borrowing)システムを学ぶことを目的とする。
//! [推奨] 1機能ごとに関数を分け、各出力文の直後に `// -> 実際の出力` を添えて
//! コードと結果を同じ場所で確認できるようにしている。
//!
//! ## 主な題材
//! 1. 所有権の基本（move by default）: C++は既定でコピー、Rustは既定でムーブ
//! 2. 借用(borrowing)のルール: `&T` / `&mut T`、可変参照は同時に1つだけ
//! 3. スライス: `&str`, `&[T]`
//! 4. 構造体とメソッド(impl)
//! 5. enumとパターンマッチ(match): `Option<T>` / `Result<T, E>`
//! 6. `?`演算子によるエラー伝播
//! 7. トレイト(trait)とポリモーフィズム
//! 8. `Vec<T>`とコレクション操作
//! 9. `Box`/`Rc`/`Weak`: C++の`unique_ptr`/`shared_ptr`/`weak_ptr`との対比
//!
//! ## 実行手順
//! ```sh
//! cd rust/basics_ownership_borrowing   (リポジトリのルートから)
//! cargo run
//! ```
#![allow(non_snake_case)] // このリポジトリの命名規則(lowerCamelCase)を優先するため（rust/hello_world踏襲）

use std::rc::{Rc, Weak};

fn printTitle(title: &str) {
    println!("\n=== {} ===", title);
}

/* ------------------------------------------------------------------------
 * 1. 所有権の基本（move by default）
 * ---------------------------------------------------------------------- */

/// 所有権の基本(move by default)を確認する。
///
/// C++との違い:
/// - C++: `std::string b = a;` は既定でコピー。move したい場合は明示的に `std::move(a)` が必要。
/// - Rust: `String` のような「ヒープを持つ型」は既定でムーブされる。`let b = a;` の後に `a` を
///   使うと**コンパイルエラー**になる（C++のuse-after-moveが実行時に「有効だが不定の状態」を
///   許してしまうのと対照的）。
fn demonstrateOwnershipMoveByDefault() {
    printTitle("1. ownership: move by default (vs C++ copy by default)");

    let a = String::from("hello");
    let b = a; // aの所有権がbへムーブされる。以後aは使えない。
    println!("b={}", b);
    // -> b=hello

    // [悪い例] ムーブ後の変数を使おうとするとコンパイルエラーになる。
    // Rustではこれが「実行時のバグ」ではなく「コンパイルエラー」として検出される点がC++との
    // 大きな違い。実際に有効化すると、次のようなエラーになる:
    //   error[E0382]: borrow of moved value: `a`
    // println!("a={}", a); // <- コンパイルエラー

    // Copy型（i32等の単純な値型）はムーブではなくコピーされるため、両方使い続けられる。
    let x = 5;
    let y = x; // xはCopyトレイトを実装しているため、コピーされる（ムーブされない）
    println!("x={}, y={} (i32はCopy型なので両方使える)", x, y);
    // -> x=5, y=5 (i32はCopy型なので両方使える)

    // 明示的にクローンすれば、Stringでも複製して両方使い続けられる（C++のコピーに相当）
    let c = String::from("world");
    let d = c.clone(); // 明示的な深いコピー。C++のコピーコンストラクタに相当
    println!("c={}, d={} (cloneで両方使える)", c, d);
    // -> c=world, d=world (cloneで両方使える)
}

/* ------------------------------------------------------------------------
 * 2. 借用(borrowing)のルール
 * ---------------------------------------------------------------------- */

/// 借用(borrowing)のルール(`&T` / `&mut T`)を確認する。
///
/// C++との違い:
/// - C++の参照/ポインタは、複数の書き込み可能な参照が同時に存在してもコンパイラは止めない
///   （実行時にデータ競合やUBを引き起こしうる）。
/// - Rustは「不変参照(`&T`)は複数OK、可変参照(`&mut T`)は同時に1つだけ、かつ不変参照と
///   同時には持てない」というルールをコンパイル時に強制する（借用チェッカー）。
fn demonstrateBorrowingRules() {
    printTitle("2. borrowing rules: &T (shared) / &mut T (exclusive)");

    let mut value = String::from("data");

    // 不変参照(&T)は同時に何個でも作れる
    let ref1 = &value;
    let ref2 = &value;
    println!("ref1={}, ref2={} (不変参照は複数OK)", ref1, ref2);
    // -> ref1=data, ref2=data (不変参照は複数OK)
    // [注意] ref1/ref2はここが最後の使用箇所。Rust 2018以降のNLL(非字句スコープ生存期間)により、
    // 借用の「生存期間」はここで終わったとみなされるため、次の可変参照が問題なく作れる。

    // 可変参照(&mut T)はスコープ内で同時に1つだけ
    let mutableRef = &mut value;
    mutableRef.push('!');
    println!("after mutableRef.push: {}", mutableRef);
    // -> after mutableRef.push: data!

    // [悪い例] 可変参照と不変参照を同時に使おうとするとコンパイルエラーになる。
    // 実際に有効化すると、次のようなエラーになる:
    //   error[E0502]: cannot borrow `value` as immutable because it is also borrowed as mutable
    // let anotherRef = &value;
    // println!("{} {}", mutableRef, anotherRef); // <- コンパイルエラー
}

/* ------------------------------------------------------------------------
 * 3. スライス
 * ---------------------------------------------------------------------- */

/// スライス(`&str`, `&[T]`)で「所有せず一部を参照する」パターンを確認する。
///
/// C++の`std::string_view`/`std::span`に近い概念（`cpp_basics_class_memory_ownership.cpp` §6と対比可能）。
fn demonstrateSlices() {
    printTitle("3. slices: &str and &[T] (borrow a view, don't own)");

    let sentence = String::from("Hello Rust World");
    let firstWord: &str = &sentence[0..5]; // "Hello" をコピーせず参照するだけ
    println!("firstWord={}", firstWord);
    // -> firstWord=Hello

    let numbers = [10, 20, 30, 40, 50];
    let middleSlice: &[i32] = &numbers[1..4]; // [20, 30, 40] を参照
    println!("middleSlice={:?}", middleSlice);
    // -> middleSlice=[20, 30, 40]
}

/* ------------------------------------------------------------------------
 * 4. 構造体とメソッド(impl)
 * ---------------------------------------------------------------------- */

/// センサー情報を表す構造体（C++の`Sensor`クラスに相当）。
struct Sensor {
    name: String,
    value: i32,
}

impl Sensor {
    /// コンストラクタ相当の関連関数。Rustにはコンストラクタ専用構文はなく、慣習として`new`を使う。
    fn new(name: &str, value: i32) -> Self {
        Sensor { name: name.to_string(), value }
    }

    /// 不変メソッド（C++の`const`メンバ関数に相当。`&self`は不変借用）。
    fn describe(&self) -> String {
        format!("{}={}", self.name, self.value)
    }

    /// 可変メソッド（`&mut self`は可変借用。呼ぶ側は可変な変数からアクセスする必要がある）。
    fn increment(&mut self) {
        self.value += 1;
    }
}

/// 構造体とメソッド(impl)を確認する。C++のclass/構造体のメンバ関数と対比する。
fn demonstrateStructsAndMethods() {
    printTitle("4. structs and methods (impl) vs C++ class member functions");

    let mut sensor = Sensor::new("temperature", 25);
    println!("{}", sensor.describe());
    // -> temperature=25

    sensor.increment();
    println!("after increment: {}", sensor.describe());
    // -> after increment: temperature=26
}

/* ------------------------------------------------------------------------
 * 5. enumとパターンマッチ(match): Option<T> / Result<T, E>
 * ---------------------------------------------------------------------- */

/// 条件を満たす最初の偶数を探す。見つからなければ`None`を返す。
/// [注意] clippyは`.iter().find(...)`への書き換えを提案するが、本セクションの主題は
/// 「for + if + return Some/None」という制御フローそのものの確認であるため、意図的に手動ループのままにする。
#[allow(clippy::manual_find)]
fn findEvenNumber(numbers: &[i32]) -> Option<i32> {
    for &n in numbers {
        if n % 2 == 0 {
            return Some(n);
        }
    }
    None
}

/// 割り算を行う。ゼロ除算なら`Err`を返す。
fn divide(a: i32, b: i32) -> Result<i32, String> {
    if b == 0 {
        return Err("divide by zero".to_string());
    }
    Ok(a / b)
}

/// enumとパターンマッチ(match)、`Option<T>`と`Result<T, E>`を確認する。
///
/// C++との違い:
/// - C++の「値がないかもしれない」は`nullptr`/`std::optional`で表現するが、うっかり参照すると
///   UB(nullptr逆参照)か例外(`optional::value()`)になりうる（本ファイルcpp_basics §5dのNULL問題）。
/// - Rustの`Option<T>`は「値があるSome(T)か、値がないNoneか」を型として持ち、`match`で
///   両方のケースを網羅しないと**コンパイルエラー**になる（うっかり片方の分岐を忘れられない）。
fn demonstrateEnumAndMatch() {
    printTitle("5. enum & match: Option<T> vs nullptr, Result<T, E> vs exceptions");

    let numbers = [1, 3, 5, 8, 9];
    match findEvenNumber(&numbers) {
        Some(value) => println!("found even number: {}", value),
        None => println!("no even number found"),
    }
    // -> found even number: 8

    match findEvenNumber(&[1, 3, 5]) {
        Some(value) => println!("found even number: {}", value),
        None => println!("no even number found"),
    }
    // -> no even number found

    // Result<T, E>: 成功(Ok)か失敗(Err)かを型で表現する
    match divide(10, 0) {
        Ok(value) => println!("divide result: {}", value),
        Err(message) => println!("divide error: {}", message),
    }
    // -> divide error: divide by zero
}

/* ------------------------------------------------------------------------
 * 6. ?演算子によるエラー伝播
 * ---------------------------------------------------------------------- */

/// 文字列をパースして2倍にする。パース失敗ならそのエラーを呼び出し元へ伝播する。
///
/// `?`演算子は「エラーだったら即座にこの関数から`Err`としてreturnする」ことを1文字で表現する
/// 糖衣構文。C++の例外(throw/catch)がスタックを自動的に巻き戻すのに対し、Rustの`?`は
/// 戻り値の型(`Result`)として明示されるため、呼び出し元は**必ず**エラー処理を型で強制される
/// （C++は例外を握りつぶして無視することもできてしまう）。
fn parseAndDouble(text: &str) -> Result<i32, std::num::ParseIntError> {
    let parsed: i32 = text.parse()?; // パース失敗なら即座にErrとしてこの関数から抜ける
    Ok(parsed * 2)
}

fn demonstrateQuestionMarkOperator() {
    printTitle("6. ? operator: Result propagation vs C++ exceptions");

    match parseAndDouble("21") {
        Ok(value) => println!("parseAndDouble(\"21\")={}", value),
        Err(e) => println!("parseAndDouble(\"21\") error: {}", e),
    }
    // -> parseAndDouble("21")=42

    match parseAndDouble("abc") {
        Ok(value) => println!("parseAndDouble(\"abc\")={}", value),
        Err(e) => println!("parseAndDouble(\"abc\") error: {}", e),
    }
    // -> parseAndDouble("abc") error: invalid digit found in string
}

/* ------------------------------------------------------------------------
 * 7. トレイト(trait)とポリモーフィズム
 * ---------------------------------------------------------------------- */

/// センサードライバのインターフェースを表すトレイト（C++の抽象基底クラス/関数ポインタに相当）。
trait SensorDriver {
    fn readValue(&self) -> i32;
    fn name(&self) -> &str;
}

struct FixedTemperatureDriver;
impl SensorDriver for FixedTemperatureDriver {
    fn readValue(&self) -> i32 {
        25
    }
    fn name(&self) -> &str {
        "temperature"
    }
}

struct FixedHumidityDriver;
impl SensorDriver for FixedHumidityDriver {
    fn readValue(&self) -> i32 {
        60
    }
    fn name(&self) -> &str {
        "humidity"
    }
}

/// トレイト(trait)によるポリモーフィズムを確認する。
///
/// C++の`cpp_basics_class_memory_ownership.cpp` §6「構造体に持たせた関数ポインタ(簡易vtable)」と
/// 直接対比できる。Rustのtraitは「このメソッド群を実装している型なら何でも」を表現し、
/// `dyn Trait`で実行時多態（C++の仮想関数テーブルに近い仕組み）を実現する。
fn demonstrateTraits() {
    printTitle("7. traits (polymorphism) vs C++ function-pointer vtable pattern");

    let drivers: Vec<Box<dyn SensorDriver>> =
        vec![Box::new(FixedTemperatureDriver), Box::new(FixedHumidityDriver)];

    for driver in &drivers {
        println!("driver name={} value={}", driver.name(), driver.readValue());
    }
    // -> driver name=temperature value=25
    // -> driver name=humidity value=60
}

/* ------------------------------------------------------------------------
 * 8. Vec<T>とコレクション操作
 * ---------------------------------------------------------------------- */

/// `Vec<T>`の基本操作を確認する。C++の`std::vector`と対比する
/// （`cpp_basics_class_memory_ownership.cpp` §4）。
/// [注意] clippyは`vec![10, 20, 30]`への書き換えを提案するが、本セクションの主題は
/// C++の`push_back`連続呼び出しと対比する「`Vec::new()` + `push()`を繰り返す」操作そのものの
/// 確認であるため、意図的にそのままにする。
#[allow(clippy::vec_init_then_push)]
fn demonstrateVecBasics() {
    printTitle("8. Vec<T> basics vs C++ std::vector");

    let mut numbers: Vec<i32> = Vec::new();
    numbers.push(10);
    numbers.push(20);
    numbers.push(30);
    println!("after push x3: len={} values={:?}", numbers.len(), numbers);
    // -> after push x3: len=3 values=[10, 20, 30]

    // C++のvec.at()相当: get()はOption<&T>を返し、範囲外でもパニックしない
    match numbers.get(100) {
        Some(value) => println!("numbers.get(100)={}", value),
        None => println!("numbers.get(100)=None (範囲外でもパニックしない)"),
    }
    // -> numbers.get(100)=None (範囲外でもパニックしない)

    // [注意] C++の operator[] は範囲外アクセスがUB(未定義動作)になるのに対し、Rustの `[]` は
    // 範囲外だと「必ずpanicして安全に停止する」。UBにはならない点が大きな違い（詳細は下記コメント）。
    // numbers[100]; // <- 実行時にpanicする。UBにはならないが、意図的に実行コードとしては書かない

    let sum: i32 = numbers.iter().sum(); // イテレータ + sum()で合計を取る（<algorithm>に近い発想）
    println!("sum={}", sum);
    // -> sum=60

    let doubled: Vec<i32> = numbers.iter().map(|n| n * 2).collect(); // map+collectで新しいVecを作る
    println!("doubled={:?}", doubled);
    // -> doubled=[20, 40, 60]
}

/* ------------------------------------------------------------------------
 * 9. Box/Rc/Weak: C++のunique_ptr/shared_ptr/weak_ptrとの対比
 * ---------------------------------------------------------------------- */

/// construct/dropのタイミングをログ出力する構造体（C++版`Sensor`クラスに相当）。
struct LoggedSensor {
    name: String,
}

impl LoggedSensor {
    fn new(name: &str) -> Self {
        println!("  [LoggedSensor] construct: {}", name);
        LoggedSensor { name: name.to_string() }
    }
}

impl Drop for LoggedSensor {
    /// スコープを抜ける際にRustコンパイラが自動的に呼ぶ後始末処理（C++のデストラクタに相当）。
    fn drop(&mut self) {
        println!("  [LoggedSensor] drop: {}", self.name);
    }
}

/// `Box<T>`（排他所有・ヒープ確保）を確認する。C++の`unique_ptr`に相当。
///
/// `Box<T>`もStringと同様「既定でムーブ」される。C++の`unique_ptr`は「コピー禁止・move専用」を
/// ランタイムの仕組み(コピーコンストラクタの`= delete`)で実現するが、Rustは所有権システムそのものが
/// 型に依らず一律に同じ制約を強制する。
fn demonstrateBoxOwnership() {
    printTitle("9a. Box<T> (exclusive ownership) vs C++ unique_ptr");

    let boxed = Box::new(LoggedSensor::new("boxed-sensor"));
    // -> "  [LoggedSensor] construct: boxed-sensor"
    println!("boxed.name={}", boxed.name);
    // -> boxed.name=boxed-sensor

    let movedBox = boxed; // 所有権がムーブされる
    println!("movedBox.name={}", movedBox.name);
    // -> movedBox.name=boxed-sensor

    // [悪い例] ムーブ後のboxedを使おうとするとコンパイルエラーになる。
    //   error[E0382]: borrow of moved value: `boxed`
    // println!("{}", boxed.name); // <- コンパイルエラー
}
// -> ここでmovedBoxがスコープを抜け "  [LoggedSensor] drop: boxed-sensor"

/// `Rc<T>`（共有所有・参照カウント、シングルスレッド専用）を確認する。C++の`shared_ptr`に相当。
/// [注意] マルチスレッドで共有したい場合は、スレッド安全な`Arc<T>`を使う（本ファイルでは扱わない）。
fn demonstrateRcOwnership() {
    printTitle("9b. Rc<T> (shared, ref-counted, single-thread) vs C++ shared_ptr");

    let ownerA = Rc::new(LoggedSensor::new("rc-sensor"));
    // -> "  [LoggedSensor] construct: rc-sensor"
    {
        let ownerB = Rc::clone(&ownerA); // C++のshared_ptrコピーに相当。参照カウントが増える
        println!("after clone: strong_count={}", Rc::strong_count(&ownerA));
        // -> after clone: strong_count=2
        println!("ownerB.name={}", ownerB.name);
        // -> ownerB.name=rc-sensor
    } // ownerBがスコープを抜けて参照カウントが減る
    println!("after ownerB out of scope: strong_count={}", Rc::strong_count(&ownerA));
    // -> after ownerB out of scope: strong_count=1
}
// -> ここでownerAが破棄され "  [LoggedSensor] drop: rc-sensor"

/// `Weak<T>`（非所有の観測者）を確認する。C++の`weak_ptr`に相当。
fn demonstrateWeakOwnership() {
    printTitle("9c. Weak<T> (non-owning observer) vs C++ weak_ptr");

    let observer: Weak<LoggedSensor>; // 未初期化のまま宣言。ブロック内で必ず1回だけ代入される（definite assignment）
    {
        let owner = Rc::new(LoggedSensor::new("weak-target"));
        // -> "  [LoggedSensor] construct: weak-target"
        observer = Rc::downgrade(&owner); // 所有権は増えない(strong_countは1のまま)

        match observer.upgrade() {
            Some(locked) => println!("while alive: locked.name={}", locked.name),
            None => println!("while alive: already gone"),
        }
        // -> while alive: locked.name=weak-target
    } // ownerがスコープを抜けてstrong_countが0になり、実体も破棄される
      // -> "  [LoggedSensor] drop: weak-target"

    match observer.upgrade() {
        Some(locked) => println!("after owner dropped: locked.name={}", locked.name),
        None => println!("after owner dropped: already gone (upgrade returns None)"),
    }
    // -> after owner dropped: already gone (upgrade returns None)
}

/* ------------------------------------------------------------------------
 * エントリポイント
 * ---------------------------------------------------------------------- */

fn main() {
    demonstrateOwnershipMoveByDefault();
    demonstrateBorrowingRules();
    demonstrateSlices();
    demonstrateStructsAndMethods();
    demonstrateEnumAndMatch();
    demonstrateQuestionMarkOperator();
    demonstrateTraits();
    demonstrateVecBasics();
    demonstrateBoxOwnership();
    demonstrateRcOwnership();
    demonstrateWeakOwnership();
}
