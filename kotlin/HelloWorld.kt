/**
 * Kotlin Hello World プログラム
 * 概要: Kotlinの基本的な構文とオブジェクト指向プログラミングのサンプル
 * 主な仕様:
 * - println を使用したコンソール出力
 * - データクラス、null安全性の活用
 * - 拡張関数、高階関数の実装
 * - 関数型プログラミング要素の活用
 * 制限事項: Kotlin 1.5以降が推奨
 */

import kotlin.random.Random

/**
 * 練習用のデータクラス
 * 人物の情報を管理するデータクラス
 * @property name 名前
 * @property age 年齢
 */
data class Person(
    val name: String,
    var age: Int
) {
    
    init {
        // 初期化ブロックでのバリデーション
        require(name.isNotBlank()) { "名前が空文字列です: '$name'" }
        require(age >= 0) { "年齢は0以上である必要があります: $age" }
    }

    /**
     * 自己紹介メソッド
     * @return 自己紹介文字列
     */
    fun introduce(): String {
        return try {
            "こんにちは！私の名前は${name}で、${age}歳です。"
        } catch (e: Exception) {
            println("エラーが発生しました - クラス: Person, メソッド: introduce, エラー: ${e.message}")
            throw e
        }
    }

    /**
     * 年齢を1歳増加させるメソッド
     */
    fun incrementAge() {
        try {
            age++
            println("${name}さんの年齢が${age}歳になりました。")
        } catch (e: Exception) {
            println("エラーが発生しました - クラス: Person, メソッド: incrementAge, エラー: ${e.message}")
            throw e
        }
    }

    /**
     * 年齢グループを判定するメソッド
     * @return 年齢グループ
     */
    fun getAgeGroup(): String = when {
        age < 18 -> "未成年"
        age < 65 -> "成人"
        else -> "シニア"
    }
}

/**
 * 学生クラス（データクラスの継承は不可のため、通常のクラスで実装）
 * @property person 人物情報
 * @property studentId 学生ID
 * @property subjects 履修科目のリスト
 */
class Student(
    private val person: Person,
    val studentId: String
) {
    private val _subjects = mutableListOf<String>()
    
    // 委譲を使用してPersonの機能を利用
    val name: String get() = person.name
    var age: Int
        get() = person.age
        set(value) { person.age = value }

    init {
        require(studentId.isNotBlank()) { "学生IDが空文字列です: '$studentId'" }
    }

    /**
     * 履修科目の読み取り専用リスト
     */
    val subjects: List<String> get() = _subjects.toList()

    /**
     * 履修科目を追加
     * @param subject 科目名
     */
    fun addSubject(subject: String) {
        try {
            require(subject.isNotBlank()) { "科目名が空文字列です: '$subject'" }
            
            _subjects.add(subject.trim())
            println("${name}さんが${subject}を履修しました。")
            
        } catch (e: IllegalArgumentException) {
            println("エラーが発生しました - クラス: Student, メソッド: addSubject, 引数: subject=$subject, エラー: ${e.message}")
            throw e
        }
    }

    /**
     * 自己紹介メソッド
     * @return 学生用の自己紹介文字列
     */
    fun introduce(): String {
        val baseIntro = person.introduce()
        val subjectsText = if (subjects.isNotEmpty()) {
            ", 履修科目: ${subjects.joinToString(", ")}"
        } else {
            ""
        }
        return "$baseIntro 学生ID: $studentId$subjectsText"
    }

    /**
     * 年齢を1歳増加させるメソッド（委譲）
     */
    fun incrementAge() = person.incrementAge()

    /**
     * 年齢グループを判定するメソッド（委譲）
     */
    fun getAgeGroup(): String = person.getAgeGroup()
}

/**
 * 拡張関数のサンプル
 * Listに対して平均年齢を計算する拡張関数
 */
fun List<Person>.averageAge(): Double = 
    if (isEmpty()) 0.0 else sumOf { it.age }.toDouble() / size

/**
 * 拡張関数のサンプル
 * Personに対して誕生日の挨拶を追加
 */
fun Person.celebrateBirthday(): String {
    incrementAge()
    return "${name}さん、${age}歳の誕生日おめでとうございます！"
}

/**
 * 高階関数のサンプル
 * 条件に一致するPersonを検索する関数
 */
fun findPeople(people: List<Person>, predicate: (Person) -> Boolean): List<Person> {
    return people.filter(predicate)
}

/**
 * スコープ関数のサンプル
 * Personを作成し、初期設定を行う関数
 */
fun createPersonWithSetup(name: String, age: Int, setup: Person.() -> Unit): Person {
    return Person(name, age).apply(setup)
}

/**
 * テスト用の関数1 - 基本的な計算
 * @param a 第一引数
 * @param b 第二引数
 * @return a + b の結果
 */
fun testFunction1(a: Int, b: Int): Int {
    println("testFunction1が呼び出されました: 引数 a=$a, b=$b")
    return a + b
}

/**
 * テスト用の関数2 - 文字列処理
 * @param text 処理する文字列
 */
fun testFunction2(text: String) {
    println("testFunction2が呼び出されました: 文字列 \"$text\"")
    println("文字列の長さ: ${text.length}文字")
    println("大文字変換: ${text.uppercase()}")
    println("単語数: ${text.split(" ").size}")
}

/**
 * テスト用の関数3 - リスト処理（関数型プログラミング）
 * @param numbers 整数のリスト
 */
fun testFunction3(numbers: List<Int>) {
    require(numbers.isNotEmpty()) { "空のリストが渡されました" }
    
    println("testFunction3が呼び出されました: リストサイズ=${numbers.size}")
    println("リストの内容: $numbers")
    
    // 関数型プログラミングスタイルでの操作
    val stats = numbers.let { nums ->
        mapOf(
            "合計" to nums.sum(),
            "平均" to nums.average(),
            "最大値" to nums.maxOrNull(),
            "最小値" to nums.minOrNull(),
            "偶数の個数" to nums.count { it % 2 == 0 },
            "奇数の個数" to nums.count { it % 2 == 1 }
        )
    }
    
    stats.forEach { (key, value) -> 
        println("$key: $value")
    }
}

/**
 * null安全性のサンプル関数
 * @param text null許容文字列
 * @return 処理結果（null安全）
 */
fun processNullableString(text: String?): String {
    return text?.let { nonNullText ->
        "処理結果: ${nonNullText.trim().takeIf { it.isNotEmpty() } ?: "空文字列"}"
    } ?: "nullが渡されました"
}

/**
 * メイン関数
 * プログラムのエントリーポイント
 */
fun main() {
    try {
        // Hello World の出力
        println("Hello World!")
        println("Kotlinプログラミング学習を開始します。")
        println()

        // === 基本的な関数呼び出しのテスト ===
        println("=== 関数呼び出しのテスト ===")
        
        // テスト関数1の呼び出し
        val result1 = testFunction1(10, 20)
        println("testFunction1の結果: $result1")
        println()
        
        // テスト関数2の呼び出し
        testFunction2("Kotlinの学習")
        println()
        
        // テスト関数3の呼び出し
        val numbers = listOf(1, 2, 3, 4, 5, 10, 15, 20)
        testFunction3(numbers)
        println()

        // === オブジェクト指向プログラミングのサンプル ===
        println("=== オブジェクト指向プログラミングのサンプル ===")

        // データクラスのインスタンス作成
        val person1 = Person("田中太郎", 25)
        val person2 = Person("佐藤花子", 30)

        // メソッドの呼び出し
        println(person1.introduce())
        println(person2.introduce())
        println()

        // プロパティの変更とメソッド呼び出し
        println("年齢を増加させます...")
        person1.incrementAge()
        person2.incrementAge()
        println()

        // 変更後の状態確認
        println("変更後の情報:")
        println(person1.introduce())
        println(person2.introduce())
        println()

        // === 学生クラスのサンプル ===
        println("=== 学生クラスのサンプル ===")
        
        val student1 = Student(Person("山田次郎", 20), "S001")
        val student2 = Student(Person("鈴木三郎", 22), "S002")
        
        // 履修科目の追加
        student1.addSubject("数学")
        student1.addSubject("物理学")
        student1.addSubject("プログラミング")
        
        student2.addSubject("英語")
        student2.addSubject("歴史")
        
        println()
        println("学生の自己紹介:")
        println(student1.introduce())
        println(student2.introduce())
        println()

        // === 拡張関数のサンプル ===
        println("=== 拡張関数のサンプル ===")
        
        val people = listOf(person1, person2, Person("高橋四郎", 35))
        println("平均年齢: ${people.averageAge()}歳")
        
        // 誕生日の拡張関数
        val birthdayPerson = Person("誕生日太郎", 29)
        println(birthdayPerson.celebrateBirthday())
        println()

        // === 高階関数とラムダ式のサンプル ===
        println("=== 高階関数とラムダ式のサンプル ===")
        
        // 30歳以上の人を検索
        val adults = findPeople(people) { it.age >= 30 }
        println("30歳以上の人:")
        adults.forEach { println("  ${it.name} (${it.age}歳, ${it.getAgeGroup()})") }
        println()

        // === スコープ関数のサンプル ===
        println("=== スコープ関数のサンプル ===")
        
        val configuredPerson = createPersonWithSetup("設定太郎", 28) {
            println("${name}さんを設定中...")
            incrementAge()
            println("年齢グループ: ${getAgeGroup()}")
        }
        println("設定完了: ${configuredPerson.introduce()}")
        println()

        // === null安全性のサンプル ===
        println("=== null安全性のサンプル ===")
        
        println(processNullableString("有効な文字列"))
        println(processNullableString(""))
        println(processNullableString(null))
        println()

        // === when式のサンプル ===
        println("=== when式のサンプル ===")
        
        val randomAge = Random.nextInt(0, 80)
        val category = when {
            randomAge < 18 -> "未成年"
            randomAge < 65 -> "成人"
            else -> "シニア"
        }
        println("ランダム年齢 $randomAge 歳 -> カテゴリ: $category")
        println()

        // === エラーハンドリングのテスト ===
        println("=== エラーハンドリングのテスト ===")
        
        try {
            // 無効な引数でPersonを作成
            Person("", -5)
        } catch (e: IllegalArgumentException) {
            println("期待通りのエラーをキャッチしました: ${e.message}")
        }
        
        try {
            // 空のリストで関数を呼び出し
            testFunction3(emptyList())
        } catch (e: IllegalArgumentException) {
            println("期待通りのエラーをキャッチしました: ${e.message}")
        }

        println()
        println("プログラムが正常に終了しました。")

    } catch (e: Exception) {
        println("予期しないエラーが発生しました - 関数: main, エラー: ${e.message}")
        e.printStackTrace()
    }
}