/**
 * TypeScript Hello World プログラム
 * 概要: TypeScriptの基本的な構文とオブジェクト指向プログラミングのサンプル
 * 主な仕様:
 * - console.log を使用したコンソール出力
 * - インターフェース、クラス、ジェネリクスの活用
 * - 型安全性、最新のECMAScript機能の実装
 * - 関数型プログラミング要素の活用
 * 制限事項: TypeScript 4.0以降が推奨
 */
var __extends = (this && this.__extends) || (function () {
    var extendStatics = function (d, b) {
        extendStatics = Object.setPrototypeOf ||
            ({ __proto__: [] } instanceof Array && function (d, b) { d.__proto__ = b; }) ||
            function (d, b) { for (var p in b) if (Object.prototype.hasOwnProperty.call(b, p)) d[p] = b[p]; };
        return extendStatics(d, b);
    };
    return function (d, b) {
        if (typeof b !== "function" && b !== null)
            throw new TypeError("Class extends value " + String(b) + " is not a constructor or null");
        extendStatics(d, b);
        function __() { this.constructor = d; }
        d.prototype = b === null ? Object.create(b) : (__.prototype = b.prototype, new __());
    };
})();
var __spreadArray = (this && this.__spreadArray) || function (to, from, pack) {
    if (pack || arguments.length === 2) for (var i = 0, l = from.length, ar; i < l; i++) {
        if (ar || !(i in from)) {
            if (!ar) ar = Array.prototype.slice.call(from, 0, i);
            ar[i] = from[i];
        }
    }
    return to.concat(ar || Array.prototype.slice.call(from));
};
// === クラス定義 ===
/**
 * 練習用のサンプルクラス
 * 人物の情報を管理するクラス
 */
var Person = /** @class */ (function () {
    /**
     * コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @throws Error 無効な引数が渡された場合
     */
    function Person(name, age) {
        try {
            if (!name || name.trim().length === 0) {
                throw new Error("\u540D\u524D\u304C\u7A7A\u6587\u5B57\u5217\u3067\u3059: '".concat(name, "'"));
            }
            if (age < 0) {
                throw new Error("\u5E74\u9F62\u306F0\u4EE5\u4E0A\u3067\u3042\u308B\u5FC5\u8981\u304C\u3042\u308A\u307E\u3059: ".concat(age));
            }
            this._name = name.trim();
            this._age = age;
        }
        catch (error) {
            var errorMessage = error instanceof Error ? error.message : String(error);
            console.error("\u30A8\u30E9\u30FC\u304C\u767A\u751F\u3057\u307E\u3057\u305F - \u30AF\u30E9\u30B9: Person, \u30E1\u30BD\u30C3\u30C9: constructor, \u5F15\u6570: name=".concat(name, ", age=").concat(age, ", \u30A8\u30E9\u30FC: ").concat(errorMessage));
            throw error;
        }
    }
    Object.defineProperty(Person.prototype, "name", {
        /**
         * 名前のゲッター
         */
        get: function () {
            return this._name;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(Person.prototype, "age", {
        /**
         * 年齢のゲッター
         */
        get: function () {
            return this._age;
        },
        /**
         * 年齢のセッター
         */
        set: function (value) {
            try {
                if (value < 0) {
                    throw new Error("\u5E74\u9F62\u306F0\u4EE5\u4E0A\u3067\u3042\u308B\u5FC5\u8981\u304C\u3042\u308A\u307E\u3059: ".concat(value));
                }
                this._age = value;
            }
            catch (error) {
                var errorMessage = error instanceof Error ? error.message : String(error);
                console.error("\u30A8\u30E9\u30FC\u304C\u767A\u751F\u3057\u307E\u3057\u305F - \u30AF\u30E9\u30B9: Person, \u30E1\u30BD\u30C3\u30C9: age setter, \u5F15\u6570: value=".concat(value, ", \u30A8\u30E9\u30FC: ").concat(errorMessage));
                throw error;
            }
        },
        enumerable: false,
        configurable: true
    });
    /**
     * 自己紹介メソッド
     * @returns 自己紹介文字列
     */
    Person.prototype.introduce = function () {
        try {
            return "\u3053\u3093\u306B\u3061\u306F\uFF01\u79C1\u306E\u540D\u524D\u306F".concat(this._name, "\u3067\u3001").concat(this._age, "\u6B73\u3067\u3059\u3002");
        }
        catch (error) {
            var errorMessage = error instanceof Error ? error.message : String(error);
            console.error("\u30A8\u30E9\u30FC\u304C\u767A\u751F\u3057\u307E\u3057\u305F - \u30AF\u30E9\u30B9: Person, \u30E1\u30BD\u30C3\u30C9: introduce, \u30A8\u30E9\u30FC: ".concat(errorMessage));
            throw error;
        }
    };
    /**
     * 年齢を1歳増加させるメソッド
     */
    Person.prototype.incrementAge = function () {
        try {
            this._age++;
            console.log("".concat(this._name, "\u3055\u3093\u306E\u5E74\u9F62\u304C").concat(this._age, "\u6B73\u306B\u306A\u308A\u307E\u3057\u305F\u3002"));
        }
        catch (error) {
            var errorMessage = error instanceof Error ? error.message : String(error);
            console.error("\u30A8\u30E9\u30FC\u304C\u767A\u751F\u3057\u307E\u3057\u305F - \u30AF\u30E9\u30B9: Person, \u30E1\u30BD\u30C3\u30C9: incrementAge, \u30A8\u30E9\u30FC: ".concat(errorMessage));
            throw error;
        }
    };
    /**
     * 年齢グループを判定するメソッド
     * @returns 年齢グループ
     */
    Person.prototype.getAgeGroup = function () {
        if (this._age < 18)
            return '未成年';
        if (this._age < 65)
            return '成人';
        return 'シニア';
    };
    /**
     * オブジェクトの文字列表現
     * @returns 文字列表現
     */
    Person.prototype.toString = function () {
        return "Person { name: '".concat(this._name, "', age: ").concat(this._age, " }");
    };
    return Person;
}());
/**
 * 学生クラス（継承の例）
 * Personクラスを継承し、学生固有の機能を追加
 */
var Student = /** @class */ (function (_super) {
    __extends(Student, _super);
    /**
     * コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @param studentId 学生ID
     */
    function Student(name, age, studentId) {
        var _this = _super.call(this, name, age) || this;
        _this._subjects = [];
        try {
            if (!studentId || studentId.trim().length === 0) {
                throw new Error("\u5B66\u751FID\u304C\u7A7A\u6587\u5B57\u5217\u3067\u3059: '".concat(studentId, "'"));
            }
            _this._studentId = studentId.trim();
        }
        catch (error) {
            var errorMessage = error instanceof Error ? error.message : String(error);
            console.error("\u30A8\u30E9\u30FC\u304C\u767A\u751F\u3057\u307E\u3057\u305F - \u30AF\u30E9\u30B9: Student, \u30E1\u30BD\u30C3\u30C9: constructor, \u5F15\u6570: studentId=".concat(studentId, ", \u30A8\u30E9\u30FC: ").concat(errorMessage));
            throw error;
        }
        return _this;
    }
    Object.defineProperty(Student.prototype, "studentId", {
        /**
         * 学生IDのゲッター
         */
        get: function () {
            return this._studentId;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(Student.prototype, "subjects", {
        /**
         * 履修科目の読み取り専用配列
         */
        get: function () {
            return __spreadArray([], this._subjects, true);
        },
        enumerable: false,
        configurable: true
    });
    /**
     * 履修科目を追加
     * @param subject 科目名
     */
    Student.prototype.addSubject = function (subject) {
        try {
            if (!subject || subject.trim().length === 0) {
                throw new Error("\u79D1\u76EE\u540D\u304C\u7A7A\u6587\u5B57\u5217\u3067\u3059: '".concat(subject, "'"));
            }
            var trimmedSubject = subject.trim();
            if (!this._subjects.includes(trimmedSubject)) {
                this._subjects.push(trimmedSubject);
                console.log("".concat(this.name, "\u3055\u3093\u304C").concat(trimmedSubject, "\u3092\u5C65\u4FEE\u3057\u307E\u3057\u305F\u3002"));
            }
            else {
                console.log("".concat(this.name, "\u3055\u3093\u306F\u65E2\u306B").concat(trimmedSubject, "\u3092\u5C65\u4FEE\u3057\u3066\u3044\u307E\u3059\u3002"));
            }
        }
        catch (error) {
            var errorMessage = error instanceof Error ? error.message : String(error);
            console.error("\u30A8\u30E9\u30FC\u304C\u767A\u751F\u3057\u307E\u3057\u305F - \u30AF\u30E9\u30B9: Student, \u30E1\u30BD\u30C3\u30C9: addSubject, \u5F15\u6570: subject=".concat(subject, ", \u30A8\u30E9\u30FC: ").concat(errorMessage));
            throw error;
        }
    };
    /**
     * 自己紹介メソッド（オーバーライド）
     * @returns 学生用の自己紹介文字列
     */
    Student.prototype.introduce = function () {
        var baseIntro = _super.prototype.introduce.call(this);
        var subjectsText = this._subjects.length > 0
            ? ", \u5C65\u4FEE\u79D1\u76EE: ".concat(this._subjects.join(', '))
            : '';
        return "".concat(baseIntro, " \u5B66\u751FID: ").concat(this._studentId).concat(subjectsText);
    };
    return Student;
}(Person));
// === ジェネリクス関数 ===
/**
 * 配列の統計情報を計算するジェネリック関数
 * @param items 数値の配列
 * @returns 統計情報オブジェクト
 */
function calculateStats(items) {
    if (items.length === 0) {
        throw new Error('空の配列が渡されました');
    }
    var sum = items.reduce(function (acc, item) { return acc + item; }, 0);
    var average = sum / items.length;
    var min = Math.min.apply(Math, items);
    var max = Math.max.apply(Math, items);
    return {
        sum: sum,
        average: Math.round(average * 100) / 100, // 小数点以下2桁に丸める
        min: min,
        max: max,
        count: items.length
    };
}
/**
 * 条件に一致する要素をフィルタリングするジェネリック関数
 * @param items 配列
 * @param predicate 条件関数
 * @returns フィルタリングされた配列
 */
function filterItems(items, predicate) {
    return items.filter(predicate);
}
// === テスト関数群 ===
/**
 * テスト用の関数1 - 基本的な計算
 * @param a 第一引数
 * @param b 第二引数
 * @returns a + b の結果
 */
function testFunction1(a, b) {
    console.log("testFunction1\u304C\u547C\u3073\u51FA\u3055\u308C\u307E\u3057\u305F: \u5F15\u6570 a=".concat(a, ", b=").concat(b));
    return a + b;
}
/**
 * テスト用の関数2 - 文字列処理
 * @param text 処理する文字列
 */
function testFunction2(text) {
    console.log("testFunction2\u304C\u547C\u3073\u51FA\u3055\u308C\u307E\u3057\u305F: \u6587\u5B57\u5217 \"".concat(text, "\""));
    console.log("\u6587\u5B57\u5217\u306E\u9577\u3055: ".concat(text.length, "\u6587\u5B57"));
    console.log("\u5927\u6587\u5B57\u5909\u63DB: ".concat(text.toUpperCase()));
    console.log("\u5358\u8A9E\u6570: ".concat(text.split(' ').length));
}
/**
 * テスト用の関数3 - 配列処理（関数型プログラミング）
 * @param numbers 整数の配列
 */
function testFunction3(numbers) {
    if (numbers.length === 0) {
        throw new Error('空の配列が渡されました');
    }
    console.log("testFunction3\u304C\u547C\u3073\u51FA\u3055\u308C\u307E\u3057\u305F: \u914D\u5217\u30B5\u30A4\u30BA=".concat(numbers.length));
    console.log("\u914D\u5217\u306E\u5185\u5BB9: [".concat(numbers.join(', '), "]"));
    // 関数型プログラミングスタイルでの操作
    var stats = calculateStats(numbers);
    Object.entries(stats).forEach(function (_a) {
        var key = _a[0], value = _a[1];
        console.log("".concat(key, ": ").concat(value));
    });
    // 偶数と奇数の分類
    var evenNumbers = numbers.filter(function (n) { return n % 2 === 0; });
    var oddNumbers = numbers.filter(function (n) { return n % 2 === 1; });
    console.log("\u5076\u6570: [".concat(evenNumbers.join(', '), "]"));
    console.log("\u5947\u6570: [".concat(oddNumbers.join(', '), "]"));
}
/**
 * Optional Chaining と Nullish Coalescing のサンプル
 * @param config 設定オブジェクト（null許容）
 * @returns 処理結果文字列
 */
function processOptionalConfig(config) {
    var _a, _b, _c, _d;
    // Optional Chaining と Nullish Coalescing を使用
    var name = (_a = config === null || config === void 0 ? void 0 : config.name) !== null && _a !== void 0 ? _a : 'Unknown';
    var age = (_b = config === null || config === void 0 ? void 0 : config.age) !== null && _b !== void 0 ? _b : 0;
    var email = (_c = config === null || config === void 0 ? void 0 : config.email) !== null && _c !== void 0 ? _c : 'なし';
    var phone = (_d = config === null || config === void 0 ? void 0 : config.phone) !== null && _d !== void 0 ? _d : 'なし';
    return "\u8A2D\u5B9A\u60C5\u5831 - \u540D\u524D: ".concat(name, ", \u5E74\u9F62: ").concat(age, ", \u30E1\u30FC\u30EB: ").concat(email, ", \u96FB\u8A71: ").concat(phone);
}
// === メイン関数 ===
/**
 * プログラムのエントリーポイント
 */
function main() {
    try {
        // Hello World の出力
        console.log('Hello World!');
        console.log('TypeScriptプログラミング学習を開始します。');
        console.log();
        // === 基本的な関数呼び出しのテスト ===
        console.log('=== 関数呼び出しのテスト ===');
        // テスト関数1の呼び出し
        var result1 = testFunction1(10, 20);
        console.log("testFunction1\u306E\u7D50\u679C: ".concat(result1));
        console.log();
        // テスト関数2の呼び出し
        testFunction2('TypeScriptの学習');
        console.log();
        // テスト関数3の呼び出し
        var numbers = [1, 2, 3, 4, 5, 10, 15, 20];
        testFunction3(numbers);
        console.log();
        // === オブジェクト指向プログラミングのサンプル ===
        console.log('=== オブジェクト指向プログラミングのサンプル ===');
        // インスタンスの作成
        var person1 = new Person('田中太郎', 25);
        var person2 = new Person('佐藤花子', 30);
        // メソッドの呼び出し
        console.log(person1.introduce());
        console.log(person2.introduce());
        console.log();
        // プロパティの変更とメソッド呼び出し
        console.log('年齢を増加させます...');
        person1.incrementAge();
        person2.incrementAge();
        console.log();
        // 変更後の状態確認
        console.log('変更後の情報:');
        console.log(person1.introduce());
        console.log(person2.introduce());
        console.log();
        // === 継承のサンプル ===
        console.log('=== 継承のサンプル ===');
        var student1 = new Student('山田次郎', 20, 'S001');
        var student2 = new Student('鈴木三郎', 22, 'S002');
        // 履修科目の追加
        student1.addSubject('数学');
        student1.addSubject('物理学');
        student1.addSubject('プログラミング');
        student2.addSubject('英語');
        student2.addSubject('歴史');
        console.log();
        console.log('学生の自己紹介:');
        console.log(student1.introduce());
        console.log(student2.introduce());
        console.log();
        // === ジェネリクスと関数型プログラミングのサンプル ===
        console.log('=== ジェネリクスと関数型プログラミングのサンプル ===');
        var people = [person1, person2, student1, student2];
        // 30歳以上の人をフィルタリング
        var adults = filterItems(people, function (person) { return person.age >= 25; });
        console.log('25歳以上の人:');
        adults.forEach(function (person) {
            console.log("  ".concat(person.name, " (").concat(person.age, "\u6B73, ").concat(person.getAgeGroup(), ")"));
        });
        console.log();
        // 年齢の統計情報
        var ages = people.map(function (person) { return person.age; });
        var ageStats = calculateStats(ages);
        console.log('年齢の統計情報:');
        Object.entries(ageStats).forEach(function (_a) {
            var key = _a[0], value = _a[1];
            console.log("  ".concat(key, ": ").concat(value));
        });
        console.log();
        // === 最新のECMAScript機能のサンプル ===
        console.log('=== 最新のECMAScript機能のサンプル ===');
        // Optional Chaining と Nullish Coalescing
        var config1 = { name: '設定太郎', age: 28, email: 'test@example.com' };
        var config2 = null;
        console.log(processOptionalConfig(config1));
        console.log(processOptionalConfig(config2));
        console.log();
        // === エラーハンドリングのテスト ===
        console.log('=== エラーハンドリングのテスト ===');
        try {
            // 無効な引数でPersonを作成
            new Person('', -5);
        }
        catch (error) {
            var errorMessage = error instanceof Error ? error.message : String(error);
            console.log("\u671F\u5F85\u901A\u308A\u306E\u30A8\u30E9\u30FC\u3092\u30AD\u30E3\u30C3\u30C1\u3057\u307E\u3057\u305F: ".concat(errorMessage));
        }
        try {
            // 空の配列で統計計算
            calculateStats([]);
        }
        catch (error) {
            var errorMessage = error instanceof Error ? error.message : String(error);
            console.log("\u671F\u5F85\u901A\u308A\u306E\u30A8\u30E9\u30FC\u3092\u30AD\u30E3\u30C3\u30C1\u3057\u307E\u3057\u305F: ".concat(errorMessage));
        }
        console.log();
        console.log('プログラムが正常に終了しました。');
    }
    catch (error) {
        var errorMessage = error instanceof Error ? error.message : String(error);
        console.error("\u4E88\u671F\u3057\u306A\u3044\u30A8\u30E9\u30FC\u304C\u767A\u751F\u3057\u307E\u3057\u305F - \u95A2\u6570: main, \u30A8\u30E9\u30FC: ".concat(errorMessage));
        if (error instanceof Error && error.stack) {
            console.error(error.stack);
        }
    }
}
// プログラムの実行
main();
