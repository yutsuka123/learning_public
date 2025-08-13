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
/**
 * 基本型の変数宣言と初期化
 */
declare function demonstrateBasicTypes(): void;
/**
 * 配列型の定義と操作
 */
declare function demonstrateArrayTypes(): void;
/**
 * ユーザー情報の型定義
 */
interface UserInfo {
    readonly id: number;
    name: string;
    age: number;
    email?: string;
    phone?: string;
    isActive: boolean;
}
/**
 * 商品情報の型定義
 */
type Product = {
    readonly productId: string;
    productName: string;
    price: number;
    category: string;
    inStock: boolean;
    description?: string;
};
/**
 * オブジェクト型の練習
 */
declare function demonstrateObjectTypes(): void;
/**
 * Union型（または型）の定義
 */
type Status = 'pending' | 'processing' | 'completed' | 'failed';
type StringOrNumber = string | number;
type BooleanOrNull = boolean | null;
/**
 * Intersection型（かつ型）の定義
 */
type PersonBasic = {
    name: string;
    age: number;
};
type Employee = {
    employeeId: string;
    department: string;
};
type EmployeePerson = PersonBasic & Employee;
/**
 * Union型とIntersection型の練習
 */
declare function demonstrateUnionAndIntersectionTypes(): void;
/**
 * 文字列リテラル型
 */
type Theme = 'light' | 'dark' | 'auto';
type Size = 'small' | 'medium' | 'large';
/**
 * 数値リテラル型
 */
type DiceValue = 1 | 2 | 3 | 4 | 5 | 6;
type HttpStatusCode = 200 | 404 | 500;
/**
 * 真偽値リテラル型
 */
type AlwaysTrue = true;
type AlwaysFalse = false;
/**
 * リテラル型の練習
 */
declare function demonstrateLiteralTypes(): void;
/**
 * 関数の型定義
 */
type CalculatorFunction = (a: number, b: number) => number;
type ValidatorFunction = (value: string) => boolean;
type CallbackFunction = (message: string) => void;
/**
 * 関数型の練習
 */
declare function demonstrateFunctionTypes(): void;
/**
 * 練習問題: 学生管理システムの型定義
 */
interface StudentData {
    readonly studentId: string;
    name: string;
    age: number;
    grade: 1 | 2 | 3 | 4 | 5 | 6;
    subjects: string[];
    gpa?: number;
}
/**
 * 練習問題: 成績評価の型定義
 */
type Grade = 'A' | 'B' | 'C' | 'D' | 'F';
type SubjectScore = {
    subject: string;
    score: number;
    grade: Grade;
};
/**
 * 実践的な練習問題の実装
 */
declare function practiceExercises(): void;
/**
 * 基本型システム練習のメイン関数
 */
declare function runBasicTypesMain(): void;
/**
 * フロントエンドアプリケーションの統合と実行
 * 基本型システムの学習後、実践的なWebアプリケーションを起動
 */
declare function runWithFrontendIntegration(): Promise<void>;
