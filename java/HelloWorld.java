/**
 * Java Hello World プログラム
 * 概要: Javaの基本的な構文とオブジェクト指向プログラミングのサンプル
 * 主な仕様:
 * - System.out.println を使用したコンソール出力
 * - クラスとオブジェクトの基本的な使用方法
 * - カプセル化、継承、ポリモーフィズムの実装
 * - 例外処理の実装
 * 制限事項: Java 8以降が推奨
 */

import java.util.*;
import java.util.stream.Collectors;

/**
 * 練習用のサンプルクラス
 * 人物の情報を管理するクラス
 */
class Person {
    private String name;  // 名前（プライベートフィールド）
    private int age;      // 年齢（プライベートフィールド）

    /**
     * コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @throws IllegalArgumentException 無効な引数が渡された場合
     */
    public Person(String name, int age) {
        try {
            if (name == null || name.trim().isEmpty()) {
                throw new IllegalArgumentException("名前がnullまたは空文字列です");
            }
            if (age < 0) {
                throw new IllegalArgumentException("年齢は0以上である必要があります: " + age);
            }
            
            this.name = name.trim();
            this.age = age;
            
        } catch (IllegalArgumentException e) {
            System.err.println("エラーが発生しました - クラス: Person, メソッド: コンストラクタ, " +
                             "引数: name=" + name + ", age=" + age + ", エラー: " + e.getMessage());
            throw e;
        }
    }

    /**
     * 名前のゲッター
     * @return 名前
     */
    public String getName() {
        return name;
    }

    /**
     * 年齢のゲッター
     * @return 年齢
     */
    public int getAge() {
        return age;
    }

    /**
     * 年齢のセッター
     * @param age 新しい年齢
     * @throws IllegalArgumentException 無効な年齢が指定された場合
     */
    public void setAge(int age) {
        try {
            if (age < 0) {
                throw new IllegalArgumentException("年齢は0以上である必要があります: " + age);
            }
            this.age = age;
        } catch (IllegalArgumentException e) {
            System.err.println("エラーが発生しました - クラス: Person, メソッド: setAge, " +
                             "引数: age=" + age + ", エラー: " + e.getMessage());
            throw e;
        }
    }

    /**
     * 自己紹介メソッド
     * @return 自己紹介文字列
     */
    public String introduce() {
        try {
            return "こんにちは！私の名前は" + name + "で、" + age + "歳です。";
        } catch (Exception e) {
            System.err.println("エラーが発生しました - クラス: Person, メソッド: introduce, " +
                             "エラー: " + e.getMessage());
            throw e;
        }
    }

    /**
     * 年齢を1歳増加させるメソッド
     */
    public void incrementAge() {
        try {
            age++;
            System.out.println(name + "さんの年齢が" + age + "歳になりました。");
        } catch (Exception e) {
            System.err.println("エラーが発生しました - クラス: Person, メソッド: incrementAge, " +
                             "エラー: " + e.getMessage());
            throw e;
        }
    }

    /**
     * オブジェクトの文字列表現
     * @return 文字列表現
     */
    @Override
    public String toString() {
        return "Person{name='" + name + "', age=" + age + "}";
    }

    /**
     * オブジェクトの等価性判定
     * @param obj 比較対象オブジェクト
     * @return 等価な場合true
     */
    @Override
    public boolean equals(Object obj) {
        if (this == obj) return true;
        if (obj == null || getClass() != obj.getClass()) return false;
        Person person = (Person) obj;
        return age == person.age && Objects.equals(name, person.name);
    }

    /**
     * ハッシュコード
     * @return ハッシュコード値
     */
    @Override
    public int hashCode() {
        return Objects.hash(name, age);
    }
}

/**
 * 学生クラス（継承の例）
 * Personクラスを継承し、学生固有の機能を追加
 */
class Student extends Person {
    private String studentId;  // 学生ID
    private List<String> subjects;  // 履修科目

    /**
     * コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @param studentId 学生ID
     */
    public Student(String name, int age, String studentId) {
        super(name, age);  // 親クラスのコンストラクタ呼び出し
        
        try {
            if (studentId == null || studentId.trim().isEmpty()) {
                throw new IllegalArgumentException("学生IDがnullまたは空文字列です");
            }
            
            this.studentId = studentId.trim();
            this.subjects = new ArrayList<>();
            
        } catch (IllegalArgumentException e) {
            System.err.println("エラーが発生しました - クラス: Student, メソッド: コンストラクタ, " +
                             "引数: studentId=" + studentId + ", エラー: " + e.getMessage());
            throw e;
        }
    }

    /**
     * 履修科目を追加
     * @param subject 科目名
     */
    public void addSubject(String subject) {
        try {
            if (subject == null || subject.trim().isEmpty()) {
                throw new IllegalArgumentException("科目名がnullまたは空文字列です");
            }
            
            subjects.add(subject.trim());
            System.out.println(getName() + "さんが" + subject + "を履修しました。");
            
        } catch (IllegalArgumentException e) {
            System.err.println("エラーが発生しました - クラス: Student, メソッド: addSubject, " +
                             "引数: subject=" + subject + ", エラー: " + e.getMessage());
            throw e;
        }
    }

    /**
     * 履修科目一覧を取得
     * @return 履修科目のリスト（コピー）
     */
    public List<String> getSubjects() {
        return new ArrayList<>(subjects);
    }

    /**
     * 自己紹介メソッド（オーバーライド）
     * @return 学生用の自己紹介文字列
     */
    @Override
    public String introduce() {
        StringBuilder sb = new StringBuilder();
        sb.append(super.introduce());
        sb.append(" 学生ID: ").append(studentId);
        
        if (!subjects.isEmpty()) {
            sb.append(", 履修科目: ");
            sb.append(subjects.stream().collect(Collectors.joining(", ")));
        }
        
        return sb.toString();
    }

    /**
     * 学生IDのゲッター
     * @return 学生ID
     */
    public String getStudentId() {
        return studentId;
    }
}

/**
 * メインプログラムクラス
 */
public class HelloWorld {
    
    /**
     * プログラムのエントリーポイント
     * @param args コマンドライン引数
     */
    public static void main(String[] args) {
        try {
            // Hello World の出力
            System.out.println("Hello World!");
            System.out.println("Javaプログラミング学習を開始します。");
            System.out.println();

            // === オブジェクト指向プログラミングのサンプル ===
            System.out.println("=== オブジェクト指向プログラミングのサンプル ===");

            // インスタンスの作成
            Person person1 = new Person("田中太郎", 25);
            Person person2 = new Person("佐藤花子", 30);

            // メソッドの呼び出し
            System.out.println(person1.introduce());
            System.out.println(person2.introduce());
            System.out.println();

            // プロパティの変更とメソッド呼び出し
            System.out.println("年齢を増加させます...");
            person1.incrementAge();
            person2.incrementAge();
            System.out.println();

            // 変更後の状態確認
            System.out.println("変更後の情報:");
            System.out.println(person1.introduce());
            System.out.println(person2.introduce());
            System.out.println();

            // === 継承のサンプル ===
            System.out.println("=== 継承のサンプル ===");
            
            Student student1 = new Student("山田次郎", 20, "S001");
            Student student2 = new Student("鈴木三郎", 22, "S002");
            
            // 履修科目の追加
            student1.addSubject("数学");
            student1.addSubject("物理学");
            student1.addSubject("プログラミング");
            
            student2.addSubject("英語");
            student2.addSubject("歴史");
            
            System.out.println();
            System.out.println("学生の自己紹介:");
            System.out.println(student1.introduce());
            System.out.println(student2.introduce());
            System.out.println();

            // === コレクションとラムダ式のサンプル（Java 8以降） ===
            System.out.println("=== コレクションとラムダ式のサンプル ===");
            
            List<Person> people = Arrays.asList(person1, person2, student1, student2);
            
            // Stream APIを使用した操作
            System.out.println("25歳以上の人:");
            people.stream()
                  .filter(p -> p.getAge() >= 25)
                  .forEach(p -> System.out.println("  " + p.getName() + " (" + p.getAge() + "歳)"));
            
            System.out.println();
            
            // 平均年齢の計算
            OptionalDouble averageAge = people.stream()
                                             .mapToInt(Person::getAge)
                                             .average();
            
            if (averageAge.isPresent()) {
                System.out.printf("平均年齢: %.1f歳%n", averageAge.getAsDouble());
            }
            
            System.out.println();

            // === エラーハンドリングのテスト ===
            System.out.println("=== エラーハンドリングのテスト ===");
            
            try {
                // 無効な引数でPersonを作成
                Person invalidPerson = new Person("", -5);
            } catch (IllegalArgumentException e) {
                System.out.println("期待通りのエラーをキャッチしました: " + e.getMessage());
            }
            
            try {
                // 無効な学生IDでStudentを作成
                Student invalidStudent = new Student("テスト", 20, "");
            } catch (IllegalArgumentException e) {
                System.out.println("期待通りのエラーをキャッチしました: " + e.getMessage());
            }

            System.out.println();
            System.out.println("プログラムが正常に終了しました。");

        } catch (Exception e) {
            System.err.println("予期しないエラーが発生しました - メソッド: main, エラー: " + e.getMessage());
            e.printStackTrace();
        }
    }
}