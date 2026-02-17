/*
 * Program.cs
 *
 * [重要] モダンC#学習用のエントリポイントです。
 * [目的]
 * - C++初心者ではなく「C言語経験者（+C#初心者）」にも伝わる形で、C#の現代的な書き方を体験する。
 * - 特にラムダ式（lambda）の「キャプチャの仕方」で結果が変わる点を、出力で確認できるようにする。
 *
 * [主な仕様]
 * - サンプルは「小さな関数」に分け、関数ごとに目的/引数/戻り値/手順/結果例をコメントします。
 * - 重要なサンプルは (expected: ...) を付けて、結果の違いを目視確認できるようにします。
 *
 * [制限事項]
 * - 外部NuGet依存は追加しません（標準ライブラリのみ）。
 * - 実行順や出力は学習用のため、厳密なテストフレームワークは使いません。
 */

namespace ModernCSharpApp;

internal static class Program
{
    /// <summary>
    /// エントリポイント。
    /// </summary>
    /// <param name="args">コマンドライン引数。</param>
    /// <returns>終了コード（0: 正常、0以外: 異常）。</returns>
    private static int Main(string[] args)
    {
        try
        {
            Console.WriteLine("[ModernCSharpApp] start");
            Console.WriteLine($"args: {string.Join(' ', args)}");

            // まずは「ラムダ式のキャプチャ差」を体験してから、他のモダン機能へ進みます。
            printTitle("lambda examples (capture differences)");
            runLambdaExamples();

            printTitle("modern features (small examples)");
            runModernFeatureExamples();

            Console.WriteLine("[ModernCSharpApp] done");
            return 0;
        }
        catch (Exception ex)
        {
            // [推奨] エラーメッセージに「どの関数で」「どういう条件で」失敗したかを含める。
            // ここでは最低限として例外メッセージとスタックトレースを表示します。
            Console.Error.WriteLine($"[error] function=Main message=\"{ex.Message}\"");
            Console.Error.WriteLine(ex);
            return 1;
        }
    }

    /// <summary>
    /// 見出し（セクション）を表示します。
    /// </summary>
    /// <param name="title">見出し。</param>
    private static void printTitle(string title)
    {
        Console.WriteLine();
        Console.WriteLine($"=== {title} ===");
    }

    /// <summary>
    /// ラムダ式の例をまとめて実行します。
    /// </summary>
    /// <remarks>
    /// [目的]
    /// - C#のラムダ式で「キャプチャの違い」がどう結果に影響するかを出力で確認する。
    ///
    /// [重要: 用語]
    /// - キャプチャ: ラムダが「外側の変数」を参照（またはコピー）して使えるようにする仕組み。
    /// - クロージャ: キャプチャした変数とラムダを束ねた実体（ランタイムが内部クラスを作ることがある）。
    ///
    /// [C言語経験者向け補足]
    /// - Cの「関数ポインタ」は外側の状態を持てない（持つならグローバルや構造体+関数など）。
    /// - C#のラムダは外側の状態を自然に持てるが、持ち方（キャプチャ）でバグや性能差が出る。
    /// </remarks>
    private static void runLambdaExamples()
    {
        // (1) 基本: 引数を受け取って計算する（キャプチャなし）
        // - これは「純粋関数」に近く、外側の状態に依存しないので読みやすく安全。
        Func<int, int> square = x => x * x;
        Console.WriteLine($"square(5)={square(5)} (expected: 25)");

        // (2) キャプチャあり: 外側の変数 factor を使う
        // - factor を変更すると、ラムダの結果も変わる（= 参照しているため）。
        var factor = 10;
        Func<int, int> multiplyByFactor = x => x * factor;
        Console.WriteLine($"factor={factor}");
        Console.WriteLine($"multiplyByFactor(3)={multiplyByFactor(3)} (expected: 30)");
        factor = 100;
        Console.WriteLine($"factor(after change)={factor}");
        Console.WriteLine($"multiplyByFactor(3)={multiplyByFactor(3)} (expected: 300)");

        // (3) 「値を固定したい」場合: ローカル変数にコピーしてからキャプチャする
        // - こうすると後で factor が変わっても結果は変わらない（固定化）。
        var fixedFactor = factor; // ここで値をコピー
        Func<int, int> multiplyByFixedFactor = x => x * fixedFactor;
        factor = 7; // factorは変える
        Console.WriteLine($"fixedFactor={fixedFactor} factor(now)={factor}");
        Console.WriteLine($"multiplyByFixedFactor(3)={multiplyByFixedFactor(3)} (expected: 300)");

        // (4) ループ + キャプチャの典型ミス（forの変数をキャプチャ）
        // - 過去のC#では「ループ変数のキャプチャ」が罠になりやすかった（foreachは改善済み）。
        // - ここでは「わざと」同じ変数 i をキャプチャしてしまう例を作り、結果差を見せます。
        var actionsBad = new List<Action>();
        for (var i = 0; i < 3; i++)
        {
            actionsBad.Add(() => Console.WriteLine($"[bad] i={i} (expected: 0/1/2 but often becomes 3)"));
        }

        // [良い例] ループ内で別変数にコピーしてキャプチャする
        var actionsGood = new List<Action>();
        for (var i = 0; i < 3; i++)
        {
            var captured = i; // ここでコピー（固定化）
            actionsGood.Add(() => Console.WriteLine($"[good] captured={captured} (expected: 0/1/2)"));
        }

        Console.WriteLine("run actionsBad:");
        foreach (var action in actionsBad)
        {
            action();
        }

        Console.WriteLine("run actionsGood:");
        foreach (var action in actionsGood)
        {
            action();
        }

        // (5) staticラムダ（C# 9+）: キャプチャを禁止して意図と性能を明確にする
        // - キャプチャが不要な処理にstaticを付けると、誤って外側変数を使うのを防げます。
        // - 速度面でも「クロージャ生成が不要」になりやすい（状況による）。
        Func<int, int> staticDouble = static x => x * 2;
        Console.WriteLine($"staticDouble(4)={staticDouble(4)} (expected: 8)");

        // (6) メソッドグループ（ラムダの代替）: 既存メソッドをそのまま渡す
        // - ラムダが不要ならこちらの方が読みやすいことがある。
        Func<string, int> length = getStringLength;
        Console.WriteLine($"length('abc')={length("abc")} (expected: 3)");
    }

    /// <summary>
    /// 文字列長を返します（メソッドグループ例）。
    /// </summary>
    /// <param name="text">入力文字列。</param>
    /// <returns>文字列長。</returns>
    /// <exception cref="ArgumentNullException">text が null の場合。</exception>
    private static int getStringLength(string text)
    {
        if (text is null)
        {
            throw new ArgumentNullException(nameof(text), "getStringLength: text is null");
        }

        return text.Length;
    }

    /// <summary>
    /// モダンC#の機能を「小さく」確認します。
    /// </summary>
    /// <remarks>
    /// - ここではラムダ以外の代表例として、nullable参照型・switch式・レコード等を触ります。
    /// - 目的は「雰囲気を掴む」こと。深掘りは別ファイルに分けていく想定です。
    /// </remarks>
    private static void runModernFeatureExamples()
    {
        // (1) Nullable参照型: string? は null になり得る
        string? maybeName = null;
        var nameOrDefault = maybeName ?? "guest";
        Console.WriteLine($"nameOrDefault={nameOrDefault} (expected: guest)");

        // (2) switch式 + パターンマッチング（簡単例）
        object value = 123;
        var category = value switch
        {
            int i when i >= 0 => "non-negative int",
            int => "negative int",
            string s => $"string(length={s.Length})",
            null => "null",
            _ => "other",
        };
        Console.WriteLine($"category={category} (expected: non-negative int)");

        // (3) record（不変データの表現に便利）
        // - C言語で言うと「構造体 + 比較/表示を自動生成してくれる」イメージ。
        var user = new userInfo(userId: 1, userName: "alice");
        var user2 = user with { userName = "bob" };
        Console.WriteLine($"user={user} (expected: userInfo {{ userId = 1, userName = alice }} など)");
        Console.WriteLine($"user2={user2} (expected: userInfo {{ userId = 1, userName = bob }} など)");
    }

    /// <summary>
    /// ユーザー情報（record）。
    /// </summary>
    /// <remarks>
    /// [命名規則メモ]
    /// - 本リポジトリでは「ローワーキャメルケース」指定があるため、引数名/プロパティ名もそれに合わせています。
    /// - 一般的なC#の慣習（PublicはPascalCase）とは異なる点に注意してください。
    /// </remarks>
    internal sealed record userInfo(int userId, string userName);
}

