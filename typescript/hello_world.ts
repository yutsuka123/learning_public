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

// === 基本的な型定義 ===

/**
 * 人物情報のインターフェース
 */
interface IPerson {
    readonly name: string;
    age: number;
    introduce(): string;
    incrementAge(): void;
}

/**
 * 年齢グループの型定義
 */
type AgeGroup = '未成年' | '成人' | 'シニア';

/**
 * オプショナルなプロパティを持つ設定インターフェース
 */
interface PersonConfig {
    name: string;
    age: number;
    email?: string;
    phone?: string;
}

// === クラス定義 ===

/**
 * 練習用のサンプルクラス
 * 人物の情報を管理するクラス
 */
class Person implements IPerson {
    private _name: string;
    private _age: number;

    /**
     * コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @throws Error 無効な引数が渡された場合
     */
    constructor(name: string, age: number) {
        try {
            if (!name || name.trim().length === 0) {
                throw new Error(`名前が空文字列です: '${name}'`);
            }
            if (age < 0) {
                throw new Error(`年齢は0以上である必要があります: ${age}`);
            }
            
            this._name = name.trim();
            this._age = age;
            
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error(`エラーが発生しました - クラス: Person, メソッド: constructor, 引数: name=${name}, age=${age}, エラー: ${errorMessage}`);
            throw error;
        }
    }

    /**
     * 名前のゲッター
     */
    get name(): string {
        return this._name;
    }

    /**
     * 年齢のゲッター
     */
    get age(): number {
        return this._age;
    }

    /**
     * 年齢のセッター
     */
    set age(value: number) {
        try {
            if (value < 0) {
                throw new Error(`年齢は0以上である必要があります: ${value}`);
            }
            this._age = value;
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error(`エラーが発生しました - クラス: Person, メソッド: age setter, 引数: value=${value}, エラー: ${errorMessage}`);
            throw error;
        }
    }

    /**
     * 自己紹介メソッド
     * @returns 自己紹介文字列
     */
    introduce(): string {
        try {
            return `こんにちは！私の名前は${this._name}で、${this._age}歳です。`;
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error(`エラーが発生しました - クラス: Person, メソッド: introduce, エラー: ${errorMessage}`);
            throw error;
        }
    }

    /**
     * 年齢を1歳増加させるメソッド
     */
    incrementAge(): void {
        try {
            this._age++;
            console.log(`${this._name}さんの年齢が${this._age}歳になりました。`);
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error(`エラーが発生しました - クラス: Person, メソッド: incrementAge, エラー: ${errorMessage}`);
            throw error;
        }
    }

    /**
     * 年齢グループを判定するメソッド
     * @returns 年齢グループ
     */
    getAgeGroup(): AgeGroup {
        if (this._age < 18) return '未成年';
        if (this._age < 65) return '成人';
        return 'シニア';
    }

    /**
     * オブジェクトの文字列表現
     * @returns 文字列表現
     */
    toString(): string {
        return `Person { name: '${this._name}', age: ${this._age} }`;
    }
}

/**
 * 学生クラス（継承の例）
 * Personクラスを継承し、学生固有の機能を追加
 */
class Student extends Person {
    private _studentId: string;
    private _subjects: string[] = [];

    /**
     * コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @param studentId 学生ID
     */
    constructor(name: string, age: number, studentId: string) {
        super(name, age);
        
        try {
            if (!studentId || studentId.trim().length === 0) {
                throw new Error(`学生IDが空文字列です: '${studentId}'`);
            }
            
            this._studentId = studentId.trim();
            
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error(`エラーが発生しました - クラス: Student, メソッド: constructor, 引数: studentId=${studentId}, エラー: ${errorMessage}`);
            throw error;
        }
    }

    /**
     * 学生IDのゲッター
     */
    get studentId(): string {
        return this._studentId;
    }

    /**
     * 履修科目の読み取り専用配列
     */
    get subjects(): readonly string[] {
        return [...this._subjects];
    }

    /**
     * 履修科目を追加
     * @param subject 科目名
     */
    addSubject(subject: string): void {
        try {
            if (!subject || subject.trim().length === 0) {
                throw new Error(`科目名が空文字列です: '${subject}'`);
            }
            
            const trimmedSubject = subject.trim();
            if (!this._subjects.includes(trimmedSubject)) {
                this._subjects.push(trimmedSubject);
                console.log(`${this.name}さんが${trimmedSubject}を履修しました。`);
            } else {
                console.log(`${this.name}さんは既に${trimmedSubject}を履修しています。`);
            }
            
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error(`エラーが発生しました - クラス: Student, メソッド: addSubject, 引数: subject=${subject}, エラー: ${errorMessage}`);
            throw error;
        }
    }

    /**
     * 自己紹介メソッド（オーバーライド）
     * @returns 学生用の自己紹介文字列
     */
    override introduce(): string {
        const baseIntro = super.introduce();
        const subjectsText = this._subjects.length > 0 
            ? `, 履修科目: ${this._subjects.join(', ')}`
            : '';
        return `${baseIntro} 学生ID: ${this._studentId}${subjectsText}`;
    }
}

// === ジェネリクス関数 ===

/**
 * 配列の統計情報を計算するジェネリック関数
 * @param items 数値の配列
 * @returns 統計情報オブジェクト
 */
function calculateStats<T extends number>(items: T[]): {
    sum: number;
    average: number;
    min: number;
    max: number;
    count: number;
} {
    if (items.length === 0) {
        throw new Error('空の配列が渡されました');
    }

    const sum = items.reduce((acc, item) => acc + item, 0);
    const average = sum / items.length;
    const min = Math.min(...items);
    const max = Math.max(...items);

    return {
        sum,
        average: Math.round(average * 100) / 100, // 小数点以下2桁に丸める
        min,
        max,
        count: items.length
    };
}

/**
 * 条件に一致する要素をフィルタリングするジェネリック関数
 * @param items 配列
 * @param predicate 条件関数
 * @returns フィルタリングされた配列
 */
function filterItems<T>(items: T[], predicate: (item: T) => boolean): T[] {
    return items.filter(predicate);
}

// === ユーティリティ型のサンプル ===

/**
 * Personの部分的な更新用の型
 */
type PartialPersonUpdate = Partial<Pick<Person, 'age'>>;

/**
 * 必須プロパティのみを持つPerson型
 */
type RequiredPersonInfo = Required<PersonConfig>;

// === テスト関数群 ===

/**
 * テスト用の関数1 - 基本的な計算
 * @param a 第一引数
 * @param b 第二引数
 * @returns a + b の結果
 */
function testFunction1(a: number, b: number): number {
    console.log(`testFunction1が呼び出されました: 引数 a=${a}, b=${b}`);
    return a + b;
}

/**
 * テスト用の関数2 - 文字列処理
 * @param text 処理する文字列
 */
function testFunction2(text: string): void {
    console.log(`testFunction2が呼び出されました: 文字列 "${text}"`);
    console.log(`文字列の長さ: ${text.length}文字`);
    console.log(`大文字変換: ${text.toUpperCase()}`);
    console.log(`単語数: ${text.split(' ').length}`);
}

/**
 * テスト用の関数3 - 配列処理（関数型プログラミング）
 * @param numbers 整数の配列
 */
function testFunction3(numbers: number[]): void {
    if (numbers.length === 0) {
        throw new Error('空の配列が渡されました');
    }
    
    console.log(`testFunction3が呼び出されました: 配列サイズ=${numbers.length}`);
    console.log(`配列の内容: [${numbers.join(', ')}]`);
    
    // 関数型プログラミングスタイルでの操作
    const stats = calculateStats(numbers);
    
    Object.entries(stats).forEach(([key, value]) => {
        console.log(`${key}: ${value}`);
    });
    
    // 偶数と奇数の分類
    const evenNumbers = numbers.filter(n => n % 2 === 0);
    const oddNumbers = numbers.filter(n => n % 2 === 1);
    
    console.log(`偶数: [${evenNumbers.join(', ')}]`);
    console.log(`奇数: [${oddNumbers.join(', ')}]`);
}

/**
 * Optional Chaining と Nullish Coalescing のサンプル
 * @param config 設定オブジェクト（null許容）
 * @returns 処理結果文字列
 */
function processOptionalConfig(config: PersonConfig | null | undefined): string {
    // Optional Chaining と Nullish Coalescing を使用
    const name = config?.name ?? 'Unknown';
    const age = config?.age ?? 0;
    const email = config?.email ?? 'なし';
    const phone = config?.phone ?? 'なし';
    
    return `設定情報 - 名前: ${name}, 年齢: ${age}, メール: ${email}, 電話: ${phone}`;
}

// === メイン関数 ===

/**
 * プログラムのエントリーポイント
 */
function main(): void {
    try {
        // Hello World の出力
        console.log('Hello World!');
        console.log('TypeScriptプログラミング学習を開始します。');
        console.log();

        // === 基本的な関数呼び出しのテスト ===
        console.log('=== 関数呼び出しのテスト ===');
        
        // テスト関数1の呼び出し
        const result1 = testFunction1(10, 20);
        console.log(`testFunction1の結果: ${result1}`);
        console.log();
        
        // テスト関数2の呼び出し
        testFunction2('TypeScriptの学習');
        console.log();
        
        // テスト関数3の呼び出し
        const numbers: number[] = [1, 2, 3, 4, 5, 10, 15, 20];
        testFunction3(numbers);
        console.log();

        // === オブジェクト指向プログラミングのサンプル ===
        console.log('=== オブジェクト指向プログラミングのサンプル ===');

        // インスタンスの作成
        const person1 = new Person('田中太郎', 25);
        const person2 = new Person('佐藤花子', 30);

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
        
        const student1 = new Student('山田次郎', 20, 'S001');
        const student2 = new Student('鈴木三郎', 22, 'S002');
        
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
        
        const people: Person[] = [person1, person2, student1, student2];
        
        // 30歳以上の人をフィルタリング
        const adults = filterItems(people, person => person.age >= 25);
        console.log('25歳以上の人:');
        adults.forEach(person => {
            console.log(`  ${person.name} (${person.age}歳, ${person.getAgeGroup()})`);
        });
        console.log();

        // 年齢の統計情報
        const ages = people.map(person => person.age);
        const ageStats = calculateStats(ages);
        console.log('年齢の統計情報:');
        Object.entries(ageStats).forEach(([key, value]) => {
            console.log(`  ${key}: ${value}`);
        });
        console.log();

        // === 最新のECMAScript機能のサンプル ===
        console.log('=== 最新のECMAScript機能のサンプル ===');
        
        // Optional Chaining と Nullish Coalescing
        const config1: PersonConfig = { name: '設定太郎', age: 28, email: 'test@example.com' };
        const config2: PersonConfig | null = null;
        
        console.log(processOptionalConfig(config1));
        console.log(processOptionalConfig(config2));
        console.log();

        // === エラーハンドリングのテスト ===
        console.log('=== エラーハンドリングのテスト ===');
        
        try {
            // 無効な引数でPersonを作成
            new Person('', -5);
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.log(`期待通りのエラーをキャッチしました: ${errorMessage}`);
        }
        
        try {
            // 空の配列で統計計算
            calculateStats([]);
        } catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.log(`期待通りのエラーをキャッチしました: ${errorMessage}`);
        }

        console.log();
        console.log('プログラムが正常に終了しました。');

    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        console.error(`予期しないエラーが発生しました - 関数: main, エラー: ${errorMessage}`);
        if (error instanceof Error && error.stack) {
            console.error(error.stack);
        }
    }
}

// プログラムの実行
main();