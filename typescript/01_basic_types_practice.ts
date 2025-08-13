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
function demonstrateBasicTypes(): void {
    console.log('=== 基本型の練習 ===');
    
    // 文字列型
    const userName: string = '田中太郎';
    const userMessage: string = `こんにちは、${userName}さん！`;
    
    // 数値型
    const userAge: number = 25;
    const userScore: number = 98.5;
    const hexValue: number = 0xFF;
    const binaryValue: number = 0b1010;
    
    // 真偽値型
    const isActive: boolean = true;
    const isComplete: boolean = false;
    
    // null と undefined
    const nullValue: null = null;
    const undefinedValue: undefined = undefined;
    
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
function demonstrateArrayTypes(): void {
    console.log('=== 配列型の練習 ===');
    
    // 配列型の宣言方法1: Type[]
    const numbers: number[] = [1, 2, 3, 4, 5];
    const fruits: string[] = ['りんご', 'バナナ', 'オレンジ'];
    
    // 配列型の宣言方法2: Array<Type>
    const scores: Array<number> = [85, 92, 78, 95];
    const names: Array<string> = ['太郎', '花子', '次郎'];
    
    // 読み取り専用配列
    const readonlyNumbers: readonly number[] = [1, 2, 3];
    const readonlyFruits: ReadonlyArray<string> = ['りんご', 'バナナ'];
    
    // タプル型（固定長で異なる型を持つ配列）
    const person: [string, number] = ['田中太郎', 30];
    const coordinate: [number, number] = [10, 20];
    const userInfo: [string, number, boolean] = ['佐藤花子', 25, true];
    
    // オプショナル要素を持つタプル
    const optionalTuple: [string, number?] = ['テスト'];
    const optionalTuple2: [string, number?] = ['テスト', 42];
    
    // 残余要素を持つタプル
    const restTuple: [string, ...number[]] = ['ラベル', 1, 2, 3, 4];
    
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

// === オブジェクト型 ===

/**
 * ユーザー情報の型定義
 */
interface UserInfo {
    readonly id: number;
    name: string;
    age: number;
    email?: string; // オプショナルプロパティ
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
function demonstrateObjectTypes(): void {
    console.log('=== オブジェクト型の練習 ===');
    
    // インターフェースを使用したオブジェクト
    const user1: UserInfo = {
        id: 1,
        name: '田中太郎',
        age: 30,
        email: 'tanaka@example.com',
        isActive: true
    };
    
    const user2: UserInfo = {
        id: 2,
        name: '佐藤花子',
        age: 25,
        isActive: false
        // email と phone は省略可能
    };
    
    // 型エイリアスを使用したオブジェクト
    const product1: Product = {
        productId: 'P001',
        productName: 'ノートパソコン',
        price: 89800,
        category: '電子機器',
        inStock: true,
        description: '高性能なノートパソコンです'
    };
    
    const product2: Product = {
        productId: 'P002',
        productName: 'マウス',
        price: 2980,
        category: '周辺機器',
        inStock: false
        // description は省略可能
    };
    
    // インデックスシグネチャを持つオブジェクト型
    const dynamicObject: { [key: string]: any } = {
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

// === Union型とIntersection型 ===

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
function demonstrateUnionAndIntersectionTypes(): void {
    console.log('=== Union型とIntersection型の練習 ===');
    
    // Union型の使用例
    const currentStatus: Status = 'processing';
    const mixedValue1: StringOrNumber = 'テキスト';
    const mixedValue2: StringOrNumber = 42;
    const nullableBoolean: BooleanOrNull = true;
    
    // Intersection型の使用例
    const employeePerson: EmployeePerson = {
        name: '山田太郎',
        age: 35,
        employeeId: 'EMP001',
        department: 'エンジニアリング'
    };
    
    // Union型の型ガード
    function processValue(value: StringOrNumber): string {
        if (typeof value === 'string') {
            return `文字列: ${value.toUpperCase()}`;
        } else {
            return `数値: ${value.toFixed(2)}`;
        }
    }
    
    console.log('ステータス:', currentStatus);
    console.log('混合値:', processValue(mixedValue1), processValue(mixedValue2));
    console.log('null許容真偽値:', nullableBoolean);
    console.log('従業員情報:', employeePerson);
    console.log();
}

// === リテラル型 ===

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
function demonstrateLiteralTypes(): void {
    console.log('=== リテラル型の練習 ===');
    
    // 文字列リテラル型
    const currentTheme: Theme = 'dark';
    const buttonSize: Size = 'medium';
    
    // 数値リテラル型
    const diceRoll: DiceValue = 6;
    const statusCode: HttpStatusCode = 200;
    
    // 真偽値リテラル型
    const isAlwaysTrue: AlwaysTrue = true;
    const isAlwaysFalse: AlwaysFalse = false;
    
    // テンプレートリテラル型（TypeScript 4.1以降）
    type EventName<T extends string> = `on${Capitalize<T>}`;
    type ClickEvent = EventName<'click'>; // 'onClick'
    type HoverEvent = EventName<'hover'>; // 'onHover'
    
    const clickEventName: ClickEvent = 'onClick';
    const hoverEventName: HoverEvent = 'onHover';
    
    console.log('テーマ:', currentTheme);
    console.log('ボタンサイズ:', buttonSize);
    console.log('サイコロ:', diceRoll);
    console.log('ステータスコード:', statusCode);
    console.log('リテラル真偽値:', isAlwaysTrue, isAlwaysFalse);
    console.log('テンプレートリテラル型:', clickEventName, hoverEventName);
    console.log();
}

// === 関数型 ===

/**
 * 関数の型定義
 */
type CalculatorFunction = (a: number, b: number) => number;
type ValidatorFunction = (value: string) => boolean;
type CallbackFunction = (message: string) => void;

/**
 * 関数型の練習
 */
function demonstrateFunctionTypes(): void {
    console.log('=== 関数型の練習 ===');
    
    // 関数型の実装
    const add: CalculatorFunction = (a, b) => a + b;
    const multiply: CalculatorFunction = (a, b) => a * b;
    
    const isEmail: ValidatorFunction = (value) => {
        return value.includes('@') && value.includes('.');
    };
    
    const logger: CallbackFunction = (message) => {
        console.log(`ログ: ${message}`);
    };
    
    // 関数の使用
    const sum = add(10, 20);
    const product = multiply(5, 8);
    const emailValid = isEmail('test@example.com');
    const emailInvalid = isEmail('invalid-email');
    
    logger(`計算結果: ${sum}, ${product}`);
    logger(`メール検証: ${emailValid}, ${emailInvalid}`);
    
    // オプショナル引数とデフォルト引数
    function greet(name: string, greeting: string = 'こんにちは', punctuation?: string): string {
        const punct = punctuation ?? '！';
        return `${greeting}、${name}さん${punct}`;
    }
    
    const greeting1 = greet('田中');
    const greeting2 = greet('佐藤', 'おはよう');
    const greeting3 = greet('鈴木', 'こんばんは', '。');
    
    console.log('挨拶1:', greeting1);
    console.log('挨拶2:', greeting2);
    console.log('挨拶3:', greeting3);
    console.log();
}

// === 実践的な練習問題 ===

/**
 * 練習問題: 学生管理システムの型定義
 */
interface StudentData {
    readonly studentId: string;
    name: string;
    age: number;
    grade: 1 | 2 | 3 | 4 | 5 | 6; // 学年（リテラル型）
    subjects: string[];
    gpa?: number; // オプショナル
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
function practiceExercises(): void {
    console.log('=== 実践的な練習問題 ===');
    
    // 学生データの作成
    const students: StudentData[] = [
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
    const subjectScores: SubjectScore[] = [
        { subject: '数学', score: 95, grade: 'A' },
        { subject: '英語', score: 87, grade: 'B' },
        { subject: '国語', score: 92, grade: 'A' }
    ];
    
    // 学生情報の表示関数
    function displayStudentInfo(student: StudentData): void {
        const gpaText = student.gpa ? `GPA: ${student.gpa}` : 'GPA: 未設定';
        console.log(`学生ID: ${student.studentId}, 名前: ${student.name}, 年齢: ${student.age}, 学年: ${student.grade}, ${gpaText}`);
        console.log(`履修科目: ${student.subjects.join(', ')}`);
    }
    
    // 成績の平均を計算する関数
    function calculateAverageScore(scores: SubjectScore[]): number {
        if (scores.length === 0) return 0;
        const total = scores.reduce((sum, score) => sum + score.score, 0);
        return Math.round((total / scores.length) * 100) / 100;
    }
    
    console.log('学生情報:');
    students.forEach(displayStudentInfo);
    
    console.log('\n成績情報:');
    subjectScores.forEach(score => {
        console.log(`${score.subject}: ${score.score}点 (${score.grade})`);
    });
    
    const averageScore = calculateAverageScore(subjectScores);
    console.log(`平均点: ${averageScore}点`);
    console.log();
}

// === メイン関数 ===

/**
 * 基本型システム練習のメイン関数
 */
function runBasicTypesMain(): void {
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
        
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        console.error(`エラーが発生しました - 関数: main, エラー: ${errorMessage}`);
        if (error instanceof Error && error.stack) {
            console.error(error.stack);
        }
    }
}

/**
 * フロントエンドアプリケーションの統合と実行
 * 基本型システムの学習後、実践的なWebアプリケーションを起動
 */
async function runWithFrontendIntegration(): Promise<void> {
    try {
        console.log('=== 基本型システム練習の実行 ===');
        
        // 基本的な型システムの練習を実行
        runBasicTypesMain();
        
        console.log('\n=== フロントエンドアプリケーションの起動 ===');
        
        // ブラウザ環境でのみフロントエンドアプリケーションを実行
        if (typeof window !== 'undefined' && typeof document !== 'undefined') {
            // 動的インポートを使用してフロントエンドモジュールを読み込み
            const { FrontendApp } = await import('./modules/FrontendApp.js');
            
            // アプリケーション設定
            const appConfig = {
                containerSelector: '#app-container',
                apiEndpoint: '/api/users',
                enableDebugMode: true,
                animationDuration: 300
            };
            
            // アプリケーションコンテナの作成
            const appContainer = document.createElement('div');
            appContainer.id = 'app-container';
            appContainer.className = 'typescript-frontend-app';
            
            // スタイルの追加
            const styles = `
                <style>
                    .typescript-frontend-app {
                        font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
                        max-width: 1200px;
                        margin: 0 auto;
                        padding: 20px;
                        line-height: 1.6;
                    }
                    
                    .app-header {
                        text-align: center;
                        margin-bottom: 2rem;
                        padding: 2rem;
                        background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                        color: white;
                        border-radius: 12px;
                    }
                    
                    .app-header h1 {
                        margin: 0 0 1rem 0;
                        font-size: 2.5rem;
                        font-weight: 700;
                    }
                    
                    .app-description {
                        margin: 0;
                        font-size: 1.1rem;
                        opacity: 0.9;
                    }
                    
                    .form-section, .users-section, .stats-section {
                        background: white;
                        border-radius: 12px;
                        padding: 2rem;
                        margin-bottom: 2rem;
                        box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
                    }
                    
                    .form-section h2, .users-section h2, .stats-section h2 {
                        margin-top: 0;
                        color: #333;
                        border-bottom: 2px solid #667eea;
                        padding-bottom: 0.5rem;
                    }
                    
                    .form-group {
                        margin-bottom: 1.5rem;
                    }
                    
                    .form-label {
                        display: block;
                        margin-bottom: 0.5rem;
                        font-weight: 600;
                        color: #555;
                    }
                    
                    .required {
                        color: #e74c3c;
                        cursor: help;
                    }
                    
                    .form-input, .form-textarea {
                        width: 100%;
                        padding: 0.75rem;
                        border: 2px solid #e1e5e9;
                        border-radius: 6px;
                        font-size: 1rem;
                        transition: border-color 0.3s;
                    }
                    
                    .form-input:focus, .form-textarea:focus {
                        outline: none;
                        border-color: #667eea;
                        box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
                    }
                    
                    .form-help {
                        font-size: 0.875rem;
                        color: #6c757d;
                        margin-top: 0.25rem;
                    }
                    
                    .form-error {
                        font-size: 0.875rem;
                        color: #e74c3c;
                        margin-top: 0.25rem;
                        display: none;
                    }
                    
                    .form-error.show {
                        display: block;
                    }
                    
                    .form-actions {
                        display: flex;
                        gap: 1rem;
                        margin-top: 2rem;
                    }
                    
                    .btn {
                        padding: 0.75rem 1.5rem;
                        border: none;
                        border-radius: 6px;
                        font-size: 1rem;
                        font-weight: 600;
                        cursor: pointer;
                        transition: all 0.3s;
                        text-decoration: none;
                        display: inline-block;
                    }
                    
                    .btn-primary {
                        background: #667eea;
                        color: white;
                    }
                    
                    .btn-primary:hover:not(:disabled) {
                        background: #5a67d8;
                        transform: translateY(-1px);
                    }
                    
                    .btn-secondary {
                        background: #6c757d;
                        color: white;
                    }
                    
                    .btn-secondary:hover:not(:disabled) {
                        background: #5a6268;
                    }
                    
                    .btn-danger {
                        background: #e74c3c;
                        color: white;
                    }
                    
                    .btn-danger:hover:not(:disabled) {
                        background: #c0392b;
                    }
                    
                    .btn:disabled {
                        opacity: 0.6;
                        cursor: not-allowed;
                    }
                    
                    .btn.loading {
                        position: relative;
                    }
                    
                    .btn.loading::after {
                        content: '';
                        position: absolute;
                        width: 16px;
                        height: 16px;
                        margin: auto;
                        border: 2px solid transparent;
                        border-top-color: #ffffff;
                        border-radius: 50%;
                        animation: spin 1s linear infinite;
                        top: 0;
                        left: 0;
                        bottom: 0;
                        right: 0;
                    }
                    
                    @keyframes spin {
                        0% { transform: rotate(0deg); }
                        100% { transform: rotate(360deg); }
                    }
                    
                    .alert {
                        padding: 1rem;
                        border-radius: 6px;
                        margin-bottom: 1rem;
                    }
                    
                    .alert-error {
                        background: #f8d7da;
                        color: #721c24;
                        border: 1px solid #f5c6cb;
                    }
                    
                    .alert-success {
                        background: #d4edda;
                        color: #155724;
                        border: 1px solid #c3e6cb;
                    }
                    
                    .users-grid {
                        display: grid;
                        grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
                        gap: 1.5rem;
                        margin-top: 1.5rem;
                    }
                    
                    .card {
                        border: 1px solid #e1e5e9;
                        border-radius: 8px;
                        overflow: hidden;
                        transition: transform 0.3s, box-shadow 0.3s;
                    }
                    
                    .card:hover {
                        transform: translateY(-2px);
                        box-shadow: 0 8px 25px rgba(0, 0, 0, 0.15);
                    }
                    
                    .card-content {
                        padding: 1.5rem;
                    }
                    
                    .card-title {
                        margin: 0 0 1rem 0;
                        font-size: 1.25rem;
                        font-weight: 600;
                        color: #333;
                    }
                    
                    .card-body {
                        color: #666;
                        margin-bottom: 1rem;
                    }
                    
                    .card-actions {
                        padding: 1rem 1.5rem;
                        background: #f8f9fa;
                        border-top: 1px solid #e1e5e9;
                        display: flex;
                        gap: 0.5rem;
                    }
                    
                    .user-card-content p {
                        margin: 0.5rem 0;
                    }
                    
                    .stats-grid {
                        display: grid;
                        grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
                        gap: 1.5rem;
                        margin-top: 1.5rem;
                    }
                    
                    .stat-card {
                        text-align: center;
                        padding: 1.5rem;
                        background: #f8f9fa;
                        border-radius: 8px;
                        border: 2px solid #e1e5e9;
                    }
                    
                    .stat-card h3 {
                        margin: 0 0 1rem 0;
                        font-size: 1rem;
                        color: #666;
                        text-transform: uppercase;
                        letter-spacing: 0.5px;
                    }
                    
                    .stat-value {
                        font-size: 2rem;
                        font-weight: 700;
                        color: #667eea;
                    }
                    
                    .modal {
                        position: fixed;
                        top: 0;
                        left: 0;
                        width: 100%;
                        height: 100%;
                        z-index: 1000;
                        display: flex;
                        align-items: center;
                        justify-content: center;
                    }
                    
                    .modal-backdrop {
                        position: fixed;
                        top: 0;
                        left: 0;
                        width: 100%;
                        height: 100%;
                        background: rgba(0, 0, 0, 0.5);
                        z-index: 999;
                    }
                    
                    .modal-dialog {
                        background: white;
                        border-radius: 12px;
                        max-width: 500px;
                        width: 90%;
                        max-height: 80vh;
                        overflow-y: auto;
                        z-index: 1001;
                        position: relative;
                    }
                    
                    .modal-header {
                        padding: 1.5rem;
                        border-bottom: 1px solid #e1e5e9;
                        display: flex;
                        justify-content: space-between;
                        align-items: center;
                    }
                    
                    .modal-title {
                        margin: 0;
                        font-size: 1.25rem;
                        color: #333;
                    }
                    
                    .modal-close {
                        background: none;
                        border: none;
                        font-size: 1.5rem;
                        cursor: pointer;
                        color: #666;
                        padding: 0;
                        width: 30px;
                        height: 30px;
                        border-radius: 50%;
                        display: flex;
                        align-items: center;
                        justify-content: center;
                    }
                    
                    .modal-close:hover {
                        background: #f8f9fa;
                        color: #333;
                    }
                    
                    .modal-body {
                        padding: 1.5rem;
                    }
                    
                    .modal-footer {
                        padding: 1rem 1.5rem;
                        border-top: 1px solid #e1e5e9;
                        display: flex;
                        justify-content: flex-end;
                        gap: 0.5rem;
                    }
                    
                    .user-detail .detail-row {
                        display: flex;
                        margin-bottom: 1rem;
                        padding-bottom: 1rem;
                        border-bottom: 1px solid #f0f0f0;
                    }
                    
                    .user-detail .detail-row:last-child {
                        border-bottom: none;
                        margin-bottom: 0;
                        padding-bottom: 0;
                    }
                    
                    .detail-label {
                        font-weight: 600;
                        color: #555;
                        min-width: 100px;
                        margin-right: 1rem;
                    }
                    
                    .detail-value {
                        color: #333;
                        flex: 1;
                    }
                    
                    .tooltip {
                        position: absolute;
                        background: #333;
                        color: white;
                        padding: 0.5rem 0.75rem;
                        border-radius: 4px;
                        font-size: 0.875rem;
                        pointer-events: none;
                        z-index: 1000;
                        white-space: nowrap;
                    }
                    
                    .tooltip::before {
                        content: '';
                        position: absolute;
                        width: 0;
                        height: 0;
                        border-style: solid;
                    }
                    
                    .tooltip-top::before {
                        bottom: -5px;
                        left: 50%;
                        margin-left: -5px;
                        border-width: 5px 5px 0;
                        border-color: #333 transparent transparent transparent;
                    }
                    
                    .tooltip-bottom::before {
                        top: -5px;
                        left: 50%;
                        margin-left: -5px;
                        border-width: 0 5px 5px;
                        border-color: transparent transparent #333 transparent;
                    }
                    
                    .app-footer {
                        text-align: center;
                        padding: 2rem;
                        color: #666;
                        border-top: 1px solid #e1e5e9;
                        margin-top: 2rem;
                    }
                    
                    @media (max-width: 768px) {
                        .typescript-frontend-app {
                            padding: 10px;
                        }
                        
                        .app-header h1 {
                            font-size: 2rem;
                        }
                        
                        .form-section, .users-section, .stats-section {
                            padding: 1.5rem;
                        }
                        
                        .users-grid {
                            grid-template-columns: 1fr;
                        }
                        
                        .stats-grid {
                            grid-template-columns: 1fr;
                        }
                        
                        .form-actions {
                            flex-direction: column;
                        }
                        
                        .modal-dialog {
                            width: 95%;
                            margin: 1rem;
                        }
                    }
                </style>
            `;
            
            // bodyにスタイルとコンテナを追加
            document.head.insertAdjacentHTML('beforeend', styles);
            document.body.appendChild(appContainer);
            
            // フロントエンドアプリケーションの初期化
            const app = new FrontendApp(appConfig);
            await app.initialize();
            
            console.log('フロントエンドアプリケーションが正常に起動しました！');
            console.log('ブラウザでこのページを開いて、実際のUIを確認してください。');
            
        } else {
            console.log('ブラウザ環境ではないため、フロントエンドアプリケーションはスキップされました。');
            console.log('ブラウザでHTMLファイルを開いて実際のUIを確認してください。');
        }
        
    } catch (error) {
        const errorMessage = error instanceof Error ? error.message : String(error);
        console.error('アプリケーション実行エラー:', errorMessage);
        
        if (error instanceof Error && error.stack) {
            console.error(error.stack);
        }
    }
}

// プログラムの実行
runWithFrontendIntegration();
