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
/**
 * 練習用のサンプルクラス
 * 人物の情報を管理するクラス
 */
declare class Person implements IPerson {
    private _name;
    private _age;
    /**
     * コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @throws Error 無効な引数が渡された場合
     */
    constructor(name: string, age: number);
    /**
     * 名前のゲッター
     */
    get name(): string;
    /**
     * 年齢のゲッター
     */
    get age(): number;
    /**
     * 年齢のセッター
     */
    set age(value: number);
    /**
     * 自己紹介メソッド
     * @returns 自己紹介文字列
     */
    introduce(): string;
    /**
     * 年齢を1歳増加させるメソッド
     */
    incrementAge(): void;
    /**
     * 年齢グループを判定するメソッド
     * @returns 年齢グループ
     */
    getAgeGroup(): AgeGroup;
    /**
     * オブジェクトの文字列表現
     * @returns 文字列表現
     */
    toString(): string;
}
/**
 * 学生クラス（継承の例）
 * Personクラスを継承し、学生固有の機能を追加
 */
declare class Student extends Person {
    private _studentId;
    private _subjects;
    /**
     * コンストラクタ
     * @param name 名前
     * @param age 年齢
     * @param studentId 学生ID
     */
    constructor(name: string, age: number, studentId: string);
    /**
     * 学生IDのゲッター
     */
    get studentId(): string;
    /**
     * 履修科目の読み取り専用配列
     */
    get subjects(): readonly string[];
    /**
     * 履修科目を追加
     * @param subject 科目名
     */
    addSubject(subject: string): void;
    /**
     * 自己紹介メソッド（オーバーライド）
     * @returns 学生用の自己紹介文字列
     */
    introduce(): string;
}
/**
 * 配列の統計情報を計算するジェネリック関数
 * @param items 数値の配列
 * @returns 統計情報オブジェクト
 */
declare function calculateStats<T extends number>(items: T[]): {
    sum: number;
    average: number;
    min: number;
    max: number;
    count: number;
};
/**
 * 条件に一致する要素をフィルタリングするジェネリック関数
 * @param items 配列
 * @param predicate 条件関数
 * @returns フィルタリングされた配列
 */
declare function filterItems<T>(items: T[], predicate: (item: T) => boolean): T[];
/**
 * Personの部分的な更新用の型
 */
type PartialPersonUpdate = Partial<Pick<Person, 'age'>>;
/**
 * 必須プロパティのみを持つPerson型
 */
type RequiredPersonInfo = Required<PersonConfig>;
/**
 * テスト用の関数1 - 基本的な計算
 * @param a 第一引数
 * @param b 第二引数
 * @returns a + b の結果
 */
declare function testFunction1(a: number, b: number): number;
/**
 * テスト用の関数2 - 文字列処理
 * @param text 処理する文字列
 */
declare function testFunction2(text: string): void;
/**
 * テスト用の関数3 - 配列処理（関数型プログラミング）
 * @param numbers 整数の配列
 */
declare function testFunction3(numbers: number[]): void;
/**
 * Optional Chaining と Nullish Coalescing のサンプル
 * @param config 設定オブジェクト（null許容）
 * @returns 処理結果文字列
 */
declare function processOptionalConfig(config: PersonConfig | null | undefined): string;
/**
 * プログラムのエントリーポイント
 */
declare function main(): void;
