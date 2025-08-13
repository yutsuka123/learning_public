/**
 * TypeScript 基本型システム練習
 *
 * 🎯 学習目的:
 * TypeScriptの基本的な型システムを完全に理解し、型安全なコードを書けるようになる
 *
 * 📚 学習内容:
 * - プリミティブ型（string, number, boolean, null, undefined）
 * - 配列型とタプル型の使い分け
 * - オブジェクト型とオプショナルプロパティ
 * - Union型とIntersection型の実践的な活用
 * - リテラル型とテンプレートリテラル型
 * - 関数型と型注釈の書き方
 *
 * 🚀 実行方法:
 * 1. TypeScriptをコンパイル: npx tsc 01_basic_types_practice.ts
 * 2. JavaScriptを実行: node 01_basic_types_practice.js
 *
 * 💡 学習のポイント:
 * - 型注釈の正しい書き方を覚える
 * - TypeScriptの型推論を理解する
 * - any型を避けて具体的な型を使う
 * - エラーメッセージから型の問題を理解する
 *
 * ⚠️ 注意点:
 * - 型の安全性を最優先に考える
 * - 読み取り専用プロパティの重要性を理解する
 * - オプショナルプロパティ（?）の適切な使用
 *
 * 📋 制限事項: TypeScript 4.0以降が推奨
 */
// === 基本的なプリミティブ型 ===
/**
 * 基本型の変数宣言と初期化
 */
function demonstrateBasicTypes() {
    console.log('=== 基本型の練習 ===');
    // 文字列型
    var userName = '田中太郎';
    var userMessage = "\u3053\u3093\u306B\u3061\u306F\u3001".concat(userName, "\u3055\u3093\uFF01");
    // 数値型
    var userAge = 25;
    var userScore = 98.5;
    var hexValue = 0xFF;
    var binaryValue = 10;
    // 真偽値型
    var isActive = true;
    var isComplete = false;
    // null と undefined
    var nullValue = null;
    var undefinedValue = undefined;
    console.log('文字列:', userName, userMessage);
    console.log('数値:', userAge, userScore, hexValue, binaryValue);
    console.log('真偽値:', isActive, isComplete);
    console.log('null/undefined:', nullValue, undefinedValue);
    console.log();
}
// === 配列型とタプル型 ===
/**
 * 配列型の定義と操作
 */
function demonstrateArrayTypes() {
    console.log('=== 配列型の練習 ===');
    // 配列型の宣言方法1: Type[]
    var numbers = [1, 2, 3, 4, 5];
    var fruits = ['りんご', 'バナナ', 'オレンジ'];
    // 配列型の宣言方法2: Array<Type>
    var scores = [85, 92, 78, 95];
    var names = ['太郎', '花子', '次郎'];
    // 読み取り専用配列
    var readonlyNumbers = [1, 2, 3];
    var readonlyFruits = ['りんご', 'バナナ'];
    // タプル型（固定長で異なる型を持つ配列）
    var person = ['田中太郎', 30];
    var coordinate = [10, 20];
    var userInfo = ['佐藤花子', 25, true];
    // オプショナル要素を持つタプル
    var optionalTuple = ['テスト'];
    var optionalTuple2 = ['テスト', 42];
    // 残余要素を持つタプル
    var restTuple = ['ラベル', 1, 2, 3, 4];
    console.log('数値配列:', numbers);
    console.log('文字列配列:', fruits);
    console.log('スコア配列:', scores);
    console.log('名前配列:', names);
    console.log('読み取り専用配列:', readonlyNumbers, readonlyFruits);
    console.log('タプル:', person, coordinate, userInfo);
    console.log('オプショナルタプル:', optionalTuple, optionalTuple2);
    console.log('残余要素タプル:', restTuple);
    console.log();
}
/**
 * オブジェクト型の練習
 */
function demonstrateObjectTypes() {
    console.log('=== オブジェクト型の練習 ===');
    // インターフェースを使用したオブジェクト
    var user1 = {
        id: 1,
        name: '田中太郎',
        age: 30,
        email: 'tanaka@example.com',
        isActive: true
    };
    var user2 = {
        id: 2,
        name: '佐藤花子',
        age: 25,
        isActive: false
        // email と phone は省略可能
    };
    // 型エイリアスを使用したオブジェクト
    var product1 = {
        productId: 'P001',
        productName: 'ノートパソコン',
        price: 89800,
        category: '電子機器',
        inStock: true,
        description: '高性能なノートパソコンです'
    };
    var product2 = {
        productId: 'P002',
        productName: 'マウス',
        price: 2980,
        category: '周辺機器',
        inStock: false
        // description は省略可能
    };
    // インデックスシグネチャを持つオブジェクト型
    var dynamicObject = {
        name: '動的オブジェクト',
        count: 42,
        isValid: true,
        data: [1, 2, 3]
    };
    console.log('ユーザー情報:', user1, user2);
    console.log('商品情報:', product1, product2);
    console.log('動的オブジェクト:', dynamicObject);
    console.log();
}
/**
 * Union型とIntersection型の練習
 */
function demonstrateUnionAndIntersectionTypes() {
    console.log('=== Union型とIntersection型の練習 ===');
    // Union型の使用例
    var currentStatus = 'processing';
    var mixedValue1 = 'テキスト';
    var mixedValue2 = 42;
    var nullableBoolean = true;
    // Intersection型の使用例
    var employeePerson = {
        name: '山田太郎',
        age: 35,
        employeeId: 'EMP001',
        department: 'エンジニアリング'
    };
    // Union型の型ガード
    function processValue(value) {
        if (typeof value === 'string') {
            return "\u6587\u5B57\u5217: ".concat(value.toUpperCase());
        }
        else {
            return "\u6570\u5024: ".concat(value.toFixed(2));
        }
    }
    console.log('ステータス:', currentStatus);
    console.log('混合値:', processValue(mixedValue1), processValue(mixedValue2));
    console.log('null許容真偽値:', nullableBoolean);
    console.log('従業員情報:', employeePerson);
    console.log();
}
/**
 * リテラル型の練習
 */
function demonstrateLiteralTypes() {
    console.log('=== リテラル型の練習 ===');
    // 文字列リテラル型
    var currentTheme = 'dark';
    var buttonSize = 'medium';
    // 数値リテラル型
    var diceRoll = 6;
    var statusCode = 200;
    // 真偽値リテラル型
    var isAlwaysTrue = true;
    var isAlwaysFalse = false;
    var clickEventName = 'onClick';
    var hoverEventName = 'onHover';
    console.log('テーマ:', currentTheme);
    console.log('ボタンサイズ:', buttonSize);
    console.log('サイコロ:', diceRoll);
    console.log('ステータスコード:', statusCode);
    console.log('リテラル真偽値:', isAlwaysTrue, isAlwaysFalse);
    console.log('テンプレートリテラル型:', clickEventName, hoverEventName);
    console.log();
}
/**
 * 関数型の練習
 */
function demonstrateFunctionTypes() {
    console.log('=== 関数型の練習 ===');
    // 関数型の実装
    var add = function (a, b) { return a + b; };
    var multiply = function (a, b) { return a * b; };
    var isEmail = function (value) {
        return value.includes('@') && value.includes('.');
    };
    var logger = function (message) {
        console.log("\u30ED\u30B0: ".concat(message));
    };
    // 関数の使用
    var sum = add(10, 20);
    var product = multiply(5, 8);
    var emailValid = isEmail('test@example.com');
    var emailInvalid = isEmail('invalid-email');
    logger("\u8A08\u7B97\u7D50\u679C: ".concat(sum, ", ").concat(product));
    logger("\u30E1\u30FC\u30EB\u691C\u8A3C: ".concat(emailValid, ", ").concat(emailInvalid));
    // オプショナル引数とデフォルト引数
    function greet(name, greeting, punctuation) {
        if (greeting === void 0) { greeting = 'こんにちは'; }
        var punct = punctuation !== null && punctuation !== void 0 ? punctuation : '！';
        return "".concat(greeting, "\u3001").concat(name, "\u3055\u3093").concat(punct);
    }
    var greeting1 = greet('田中');
    var greeting2 = greet('佐藤', 'おはよう');
    var greeting3 = greet('鈴木', 'こんばんは', '。');
    console.log('挨拶1:', greeting1);
    console.log('挨拶2:', greeting2);
    console.log('挨拶3:', greeting3);
    console.log();
}
/**
 * 実践的な練習問題の実装
 */
function practiceExercises() {
    console.log('=== 実践的な練習問題 ===');
    // 学生データの作成
    var students = [
        {
            studentId: 'S001',
            name: '田中一郎',
            age: 16,
            grade: 2,
            subjects: ['数学', '英語', '国語'],
            gpa: 3.8
        },
        {
            studentId: 'S002',
            name: '佐藤花子',
            age: 15,
            grade: 1,
            subjects: ['数学', '理科', '社会']
            // gpa は省略
        }
    ];
    // 成績データの作成
    var subjectScores = [
        { subject: '数学', score: 95, grade: 'A' },
        { subject: '英語', score: 87, grade: 'B' },
        { subject: '国語', score: 92, grade: 'A' }
    ];
    // 学生情報の表示関数
    function displayStudentInfo(student) {
        var gpaText = student.gpa ? "GPA: ".concat(student.gpa) : 'GPA: 未設定';
        console.log("\u5B66\u751FID: ".concat(student.studentId, ", \u540D\u524D: ").concat(student.name, ", \u5E74\u9F62: ").concat(student.age, ", \u5B66\u5E74: ").concat(student.grade, ", ").concat(gpaText));
        console.log("\u5C65\u4FEE\u79D1\u76EE: ".concat(student.subjects.join(', ')));
    }
    // 成績の平均を計算する関数
    function calculateAverageScore(scores) {
        if (scores.length === 0)
            return 0;
        var total = scores.reduce(function (sum, score) { return sum + score.score; }, 0);
        return Math.round((total / scores.length) * 100) / 100;
    }
    console.log('学生情報:');
    students.forEach(displayStudentInfo);
    console.log('\n成績情報:');
    subjectScores.forEach(function (score) {
        console.log("".concat(score.subject, ": ").concat(score.score, "\u70B9 (").concat(score.grade, ")"));
    });
    var averageScore = calculateAverageScore(subjectScores);
    console.log("\u5E73\u5747\u70B9: ".concat(averageScore, "\u70B9"));
    console.log();
}
// === メイン関数 ===
/**
 * 基本型システム練習のメイン関数
 */
function main() {
    try {
        console.log('TypeScript 基本型システム練習プログラム');
        console.log('==========================================');
        console.log();
        // 各セクションの実行
        demonstrateBasicTypes();
        demonstrateArrayTypes();
        demonstrateObjectTypes();
        demonstrateUnionAndIntersectionTypes();
        demonstrateLiteralTypes();
        demonstrateFunctionTypes();
        practiceExercises();
        console.log('基本型システムの練習が完了しました！');
    }
    catch (error) {
        var errorMessage = error instanceof Error ? error.message : String(error);
        console.error("\u30A8\u30E9\u30FC\u304C\u767A\u751F\u3057\u307E\u3057\u305F - \u95A2\u6570: main, \u30A8\u30E9\u30FC: ".concat(errorMessage));
        if (error instanceof Error && error.stack) {
            console.error(error.stack);
        }
    }
}
// プログラムの実行
main();
