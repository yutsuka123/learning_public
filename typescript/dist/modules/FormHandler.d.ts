/**
 * フォーム処理・バリデーションクラス
 *
 * 🎯 目的:
 * - 型安全なフォーム処理の実装
 * - 再利用可能なバリデーション機能
 * - ユーザビリティを考慮したエラー表示
 *
 * 📚 主な機能:
 * - リアルタイムバリデーション
 * - カスタムバリデーションルール
 * - 型安全なフォームデータ処理
 * - アクセシビリティ対応
 *
 * 💡 設計パターン:
 * - Strategyパターン（バリデーション戦略）
 * - Observerパターン（フォーム状態の監視）
 * - Command パターン（フォーム送信処理）
 *
 * ⚠️ 注意点:
 * - XSS攻撃の防止
 * - 入力値のサニタイズ
 * - メモリリークの防止
 */
/**
 * バリデーション結果の型定義
 * 関数型プログラミングのResult型パターンを採用
 */
interface ValidationResult {
    readonly isValid: boolean;
    readonly message: string;
    readonly field?: string;
}
/**
 * バリデーションルールの型定義
 * 高階関数を使用した関数型アプローチ
 */
type ValidationRule = (value: string) => ValidationResult;
/**
 * フォームフィールドの設定
 * 不変オブジェクトとして設計
 */
interface FormFieldConfig {
    readonly name: string;
    readonly label: string;
    readonly type: 'text' | 'email' | 'password' | 'number' | 'tel';
    readonly required: boolean;
    readonly placeholder?: string;
    readonly validationRules: ReadonlyArray<ValidationRule>;
    readonly realTimeValidation: boolean;
}
/**
 * フォームデータの型定義
 * Record型を使用して型安全性を確保
 */
type FormData = Record<string, string>;
/**
 * 共通バリデーションルール集
 * 関数型プログラミングのアプローチで実装
 *
 * 💡 特徴:
 * - カリー化による部分適用
 * - 高階関数による再利用性
 * - 純粋関数による副作用の排除
 */
export declare class ValidationRules {
    /**
     * 必須入力チェック
     * @param fieldName - フィールド名
     * @returns バリデーションルール関数
     *
     * 💡 使用例:
     * const nameRequired = ValidationRules.required('名前');
     * const result = nameRequired('田中太郎');
     */
    static required(fieldName: string): ValidationRule;
    /**
     * 最小文字数チェック
     * @param minLength - 最小文字数
     * @param fieldName - フィールド名
     * @returns バリデーションルール関数
     */
    static minLength(minLength: number, fieldName: string): ValidationRule;
    /**
     * 最大文字数チェック
     * @param maxLength - 最大文字数
     * @param fieldName - フィールド名
     * @returns バリデーションルール関数
     */
    static maxLength(maxLength: number, fieldName: string): ValidationRule;
    /**
     * メールアドレス形式チェック
     * @param fieldName - フィールド名
     * @returns バリデーションルール関数
     *
     * ⚠️ 注意:
     * - RFC準拠の完全なメールバリデーションは非常に複雑
     * - 実用的なレベルでの検証を実装
     * - サーバーサイドでの最終検証が必須
     */
    static email(fieldName: string): ValidationRule;
    /**
     * 数値チェック（範囲指定可能）
     * @param fieldName - フィールド名
     * @param min - 最小値（省略可能）
     * @param max - 最大値（省略可能）
     * @returns バリデーションルール関数
     */
    static number(fieldName: string, min?: number, max?: number): ValidationRule;
}
/**
 * フォーム処理クラス
 * 型安全で再利用可能なフォーム機能を提供
 *
 * 💡 アーキテクチャ:
 * - 依存性注入によるテスタビリティ
 * - 設定駆動型の設計
 * - イベント駆動アーキテクチャ
 */
export declare class FormHandler {
    private readonly formElement;
    private readonly fields;
    private readonly submitCallback;
    private readonly errorContainer;
    /**
     * コンストラクタ
     * @param formSelector - フォームのCSSセレクタ
     * @param fields - フィールド設定配列
     * @param submitCallback - 送信時のコールバック関数
     * @param errorContainerSelector - エラー表示用コンテナのセレクタ
     *
     * 💡 設計のポイント:
     * - 依存性注入によるテスタビリティ向上
     * - 設定駆動型による柔軟性
     * - エラーハンドリングの一元化
     *
     * ⚠️ 例外:
     * - 必要な要素が見つからない場合はErrorを投げる
     * - 初期化時点でのバリデーション
     */
    constructor(formSelector: string, fields: ReadonlyArray<FormFieldConfig>, submitCallback: (data: FormData) => Promise<boolean>, errorContainerSelector?: string);
    /**
     * イベントリスナーの設定
     * プライベートメソッドで内部実装を隠蔽
     *
     * 💡 イベント戦略:
     * - submit: フォーム送信時の全体バリデーション
     * - blur: フィールド離脱時のリアルタイムバリデーション
     * - input: 入力中のエラー表示クリア
     */
    private setupEventListeners;
    /**
     * フォーム送信処理
     * @param event - 送信イベント
     *
     * 💡 処理フロー:
     * 1. デフォルト送信の防止
     * 2. 全フィールドバリデーション
     * 3. ローディング状態の表示
     * 4. 非同期送信処理
     * 5. 結果に応じた UI 更新
     *
     * ⚠️ エラーハンドリング:
     * - バリデーションエラー
     * - ネットワークエラー
     * - 予期しないエラー
     */
    private handleSubmit;
    /**
     * 単一フィールドのバリデーション
     * @param fieldName - フィールド名
     * @returns バリデーション結果
     *
     * 💡 バリデーション戦略:
     * - 設定されたルールを順次実行
     * - 最初のエラーで停止（fail-fast）
     * - UI への即座の反映
     */
    private validateField;
    /**
     * 全フィールドのバリデーション
     * @returns 全体のバリデーション結果
     *
     * 💡 処理方針:
     * - 全フィールドをチェック（早期終了しない）
     * - 全エラーを収集してユーザーに表示
     * - パフォーマンスとユーザビリティのバランス
     */
    private validateAllFields;
    /**
     * フォームデータの取得
     * @returns サニタイズされたフォームデータ
     *
     * 💡 セキュリティ対策:
     * - 基本的なサニタイズ
     * - 空白文字の除去
     * - XSS攻撃の防止
     */
    private getFormData;
    /**
     * フィールドエラーの表示
     * @param fieldName - フィールド名
     * @param message - エラーメッセージ
     */
    private displayFieldError;
    /**
     * フィールドエラーのクリア
     * @param fieldName - フィールド名
     */
    private clearFieldError;
    /**
     * エラー一覧の表示
     * @param errors - エラー配列
     */
    private displayErrors;
    /**
     * エラー表示のクリア
     */
    private clearErrors;
    /**
     * ローディング状態の設定
     * @param loading - ローディング中フラグ
     *
     * 💡 UX向上:
     * - ボタンの無効化
     * - ローディングテキストの表示
     * - 視覚的フィードバック
     */
    private setLoadingState;
    /**
     * 送信成功処理
     */
    private handleSubmitSuccess;
    /**
     * 送信エラー処理
     * @param message - エラーメッセージ
     */
    private handleSubmitError;
    /**
     * フォームのリセット
     * 外部からの制御用パブリックメソッド
     */
    reset(): void;
    /**
     * フォームの破棄処理
     * メモリリーク防止のためのクリーンアップ
     *
     * ⚠️ 実装上の注意:
     * - イベントリスナーの適切な削除
     * - 参照の解除
     * - リソースの解放
     */
    destroy(): void;
}
export {};
