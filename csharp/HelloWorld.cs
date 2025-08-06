/**
 * C# Hello World プログラム
 * 概要: C#の基本的な構文とオブジェクト指向プログラミングのサンプル
 * 主な仕様: 
 * - コンソール出力によるHello World表示
 * - クラスとインスタンスの基本的な使用方法
 * - メソッド呼び出しとプロパティアクセス
 * 制限事項: .NET 6.0以降が必要
 */

using System;

namespace HelloWorldApp
{
    /// <summary>
    /// 練習用のサンプルクラス
    /// 人物の情報を管理するクラス
    /// </summary>
    public class Person
    {
        /// <summary>
        /// 名前プロパティ（自動実装プロパティ）
        /// </summary>
        public string Name { get; set; } = string.Empty;

        /// <summary>
        /// 年齢プロパティ（自動実装プロパティ）
        /// </summary>
        public int Age { get; set; }

        /// <summary>
        /// コンストラクタ
        /// </summary>
        /// <param name="name">名前</param>
        /// <param name="age">年齢</param>
        public Person(string name, int age)
        {
            try
            {
                Name = name ?? throw new ArgumentNullException(nameof(name), "名前がnullです");
                Age = age >= 0 ? age : throw new ArgumentOutOfRangeException(nameof(age), "年齢は0以上である必要があります");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"エラーが発生しました - クラス: Person, メソッド: コンストラクタ, 引数: name={name}, age={age}, エラー: {ex.Message}");
                throw;
            }
        }

        /// <summary>
        /// 自己紹介メソッド
        /// </summary>
        /// <returns>自己紹介文字列</returns>
        public string Introduce()
        {
            try
            {
                return $"こんにちは！私の名前は{Name}で、{Age}歳です。";
            }
            catch (Exception ex)
            {
                Console.WriteLine($"エラーが発生しました - クラス: Person, メソッド: Introduce, エラー: {ex.Message}");
                throw;
            }
        }

        /// <summary>
        /// 年齢を1歳増加させるメソッド
        /// </summary>
        public void IncrementAge()
        {
            try
            {
                Age++;
                Console.WriteLine($"{Name}さんの年齢が{Age}歳になりました。");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"エラーが発生しました - クラス: Person, メソッド: IncrementAge, エラー: {ex.Message}");
                throw;
            }
        }
    }

    /// <summary>
    /// メインプログラムクラス
    /// </summary>
    public class Program
    {
        /// <summary>
        /// プログラムのエントリーポイント
        /// </summary>
        /// <param name="args">コマンドライン引数</param>
        public static void Main(string[] args)
        {
            try
            {
                // Hello World の出力
                Console.WriteLine("Hello World!");
                Console.WriteLine("C#プログラミング学習を開始します。");
                Console.WriteLine();

                // オブジェクト指向プログラミングのサンプル
                Console.WriteLine("=== オブジェクト指向プログラミングのサンプル ===");

                // インスタンスの作成
                Person person1 = new Person("田中太郎", 25);
                Person person2 = new Person("佐藤花子", 30);

                // メソッドの呼び出し
                Console.WriteLine(person1.Introduce());
                Console.WriteLine(person2.Introduce());
                Console.WriteLine();

                // プロパティの変更とメソッド呼び出し
                Console.WriteLine("年齢を増加させます...");
                person1.IncrementAge();
                person2.IncrementAge();
                Console.WriteLine();

                // 変更後の状態確認
                Console.WriteLine("変更後の情報:");
                Console.WriteLine(person1.Introduce());
                Console.WriteLine(person2.Introduce());

                Console.WriteLine();
                Console.WriteLine("プログラムが正常に終了しました。");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"予期しないエラーが発生しました - メソッド: Main, エラー: {ex.Message}");
                Console.WriteLine($"スタックトレース: {ex.StackTrace}");
            }
        }
    }
}