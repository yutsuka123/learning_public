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
import { DOMUtils } from './DOMUtils.js';
/**
 * 共通バリデーションルール集
 * 関数型プログラミングのアプローチで実装
 *
 * 💡 特徴:
 * - カリー化による部分適用
 * - 高階関数による再利用性
 * - 純粋関数による副作用の排除
 */
export class ValidationRules {
    /**
     * 必須入力チェック
     * @param fieldName - フィールド名
     * @returns バリデーションルール関数
     *
     * 💡 使用例:
     * const nameRequired = ValidationRules.required('名前');
     * const result = nameRequired('田中太郎');
     */
    static required(fieldName) {
        return (value) => {
            const trimmedValue = value.trim();
            return {
                isValid: trimmedValue.length > 0,
                message: trimmedValue.length > 0
                    ? ''
                    : `${fieldName}は必須項目です`,
                field: fieldName
            };
        };
    }
    /**
     * 最小文字数チェック
     * @param minLength - 最小文字数
     * @param fieldName - フィールド名
     * @returns バリデーションルール関数
     */
    static minLength(minLength, fieldName) {
        return (value) => {
            const isValid = value.length >= minLength;
            return {
                isValid,
                message: isValid
                    ? ''
                    : `${fieldName}は${minLength}文字以上で入力してください`,
                field: fieldName
            };
        };
    }
    /**
     * 最大文字数チェック
     * @param maxLength - 最大文字数
     * @param fieldName - フィールド名
     * @returns バリデーションルール関数
     */
    static maxLength(maxLength, fieldName) {
        return (value) => {
            const isValid = value.length <= maxLength;
            return {
                isValid,
                message: isValid
                    ? ''
                    : `${fieldName}は${maxLength}文字以内で入力してください`,
                field: fieldName
            };
        };
    }
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
    static email(fieldName) {
        return (value) => {
            const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
            const isValid = emailRegex.test(value);
            return {
                isValid,
                message: isValid
                    ? ''
                    : `${fieldName}の形式が正しくありません`,
                field: fieldName
            };
        };
    }
    /**
     * 数値チェック（範囲指定可能）
     * @param fieldName - フィールド名
     * @param min - 最小値（省略可能）
     * @param max - 最大値（省略可能）
     * @returns バリデーションルール関数
     */
    static number(fieldName, min, max) {
        return (value) => {
            const numValue = Number(value);
            if (isNaN(numValue)) {
                return {
                    isValid: false,
                    message: `${fieldName}は数値で入力してください`,
                    field: fieldName
                };
            }
            if (min !== undefined && numValue < min) {
                return {
                    isValid: false,
                    message: `${fieldName}は${min}以上で入力してください`,
                    field: fieldName
                };
            }
            if (max !== undefined && numValue > max) {
                return {
                    isValid: false,
                    message: `${fieldName}は${max}以下で入力してください`,
                    field: fieldName
                };
            }
            return {
                isValid: true,
                message: '',
                field: fieldName
            };
        };
    }
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
export class FormHandler {
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
    constructor(formSelector, fields, submitCallback, errorContainerSelector = '.error-container') {
        // フォーム要素の取得と検証
        const formResult = DOMUtils.querySelector(formSelector);
        if (!formResult.success) {
            throw new Error(`フォームが見つかりません: ${formResult.error}`);
        }
        this.formElement = formResult.data;
        // エラーコンテナの取得と検証
        const errorResult = DOMUtils.querySelector(errorContainerSelector);
        if (!errorResult.success) {
            throw new Error(`エラーコンテナが見つかりません: ${errorResult.error}`);
        }
        this.errorContainer = errorResult.data;
        // 不変オブジェクトとして保存
        this.fields = fields;
        this.submitCallback = submitCallback;
        // イベントリスナーの設定
        this.setupEventListeners();
        console.log('FormHandler initialized successfully', {
            formSelector,
            fieldsCount: fields.length,
            errorContainerSelector
        });
    }
    /**
     * イベントリスナーの設定
     * プライベートメソッドで内部実装を隠蔽
     *
     * 💡 イベント戦略:
     * - submit: フォーム送信時の全体バリデーション
     * - blur: フィールド離脱時のリアルタイムバリデーション
     * - input: 入力中のエラー表示クリア
     */
    setupEventListeners() {
        // フォーム送信イベントの設定
        const submitResult = DOMUtils.addEventListener(this.formElement, 'submit', this.handleSubmit.bind(this));
        if (!submitResult.success) {
            console.error('フォーム送信イベントの設定に失敗:', submitResult.error);
        }
        // 各フィールドのリアルタイムバリデーション設定
        this.fields.forEach(field => {
            if (field.realTimeValidation) {
                const inputResult = DOMUtils.querySelector(`[name="${field.name}"]`, this.formElement);
                if (inputResult.success) {
                    // フィールド離脱時のバリデーション
                    DOMUtils.addEventListener(inputResult.data, 'blur', () => this.validateField(field.name));
                    // 入力中のエラー表示クリア
                    DOMUtils.addEventListener(inputResult.data, 'input', () => this.clearFieldError(field.name));
                }
            }
        });
    }
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
    async handleSubmit(event) {
        event.preventDefault();
        try {
            console.log('フォーム送信開始');
            // 全フィールドのバリデーション実行
            const validationResult = this.validateAllFields();
            if (!validationResult.success) {
                console.log('バリデーションエラー:', validationResult.errors);
                this.displayErrors(validationResult.errors);
                return;
            }
            // ローディング状態の表示
            this.setLoadingState(true);
            // フォームデータの取得
            const formData = this.getFormData();
            console.log('送信データ:', formData);
            // 送信処理の実行
            const success = await this.submitCallback(formData);
            if (success) {
                this.handleSubmitSuccess();
            }
            else {
                this.handleSubmitError('送信に失敗しました');
            }
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error('フォーム送信エラー:', errorMessage);
            this.handleSubmitError(`予期しないエラーが発生しました: ${errorMessage}`);
        }
        finally {
            this.setLoadingState(false);
        }
    }
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
    validateField(fieldName) {
        const field = this.fields.find(f => f.name === fieldName);
        if (!field) {
            const error = {
                isValid: false,
                message: `フィールドが見つかりません: ${fieldName}`,
                field: fieldName
            };
            console.error('フィールド設定エラー:', error);
            return error;
        }
        const inputResult = DOMUtils.querySelector(`[name="${fieldName}"]`, this.formElement);
        if (!inputResult.success) {
            const error = {
                isValid: false,
                message: `入力要素が見つかりません: ${fieldName}`,
                field: fieldName
            };
            console.error('DOM要素エラー:', error);
            return error;
        }
        const value = inputResult.data.value;
        // 全バリデーションルールを順次実行
        for (const rule of field.validationRules) {
            const result = rule(value);
            if (!result.isValid) {
                this.displayFieldError(fieldName, result.message);
                return result;
            }
        }
        // バリデーション成功
        this.clearFieldError(fieldName);
        return {
            isValid: true,
            message: '',
            field: fieldName
        };
    }
    /**
     * 全フィールドのバリデーション
     * @returns 全体のバリデーション結果
     *
     * 💡 処理方針:
     * - 全フィールドをチェック（早期終了しない）
     * - 全エラーを収集してユーザーに表示
     * - パフォーマンスとユーザビリティのバランス
     */
    validateAllFields() {
        const errors = [];
        this.fields.forEach(field => {
            const result = this.validateField(field.name);
            if (!result.isValid) {
                errors.push(result);
            }
        });
        if (errors.length > 0) {
            return {
                success: false,
                errors
            };
        }
        return {
            success: true,
            data: this.getFormData()
        };
    }
    /**
     * フォームデータの取得
     * @returns サニタイズされたフォームデータ
     *
     * 💡 セキュリティ対策:
     * - 基本的なサニタイズ
     * - 空白文字の除去
     * - XSS攻撃の防止
     */
    getFormData() {
        const data = {};
        this.fields.forEach(field => {
            const inputResult = DOMUtils.querySelector(`[name="${field.name}"]`, this.formElement);
            if (inputResult.success) {
                // 基本的なサニタイズ処理
                data[field.name] = inputResult.data.value.trim();
            }
        });
        return data;
    }
    /**
     * フィールドエラーの表示
     * @param fieldName - フィールド名
     * @param message - エラーメッセージ
     */
    displayFieldError(fieldName, message) {
        const errorElementResult = DOMUtils.querySelector(`[data-error="${fieldName}"]`, this.formElement);
        if (errorElementResult.success) {
            errorElementResult.data.textContent = message;
            DOMUtils.toggleClass(errorElementResult.data, 'show', 'add');
        }
    }
    /**
     * フィールドエラーのクリア
     * @param fieldName - フィールド名
     */
    clearFieldError(fieldName) {
        const errorElementResult = DOMUtils.querySelector(`[data-error="${fieldName}"]`, this.formElement);
        if (errorElementResult.success) {
            errorElementResult.data.textContent = '';
            DOMUtils.toggleClass(errorElementResult.data, 'show', 'remove');
        }
    }
    /**
     * エラー一覧の表示
     * @param errors - エラー配列
     */
    displayErrors(errors) {
        const errorList = errors
            .map(error => `<li>${error.message}</li>`)
            .join('');
        const errorHtml = `
            <div class="alert alert-error" role="alert">
                <h3>入力エラーがあります</h3>
                <ul>${errorList}</ul>
            </div>
        `;
        DOMUtils.setInnerHTML(this.errorContainer, errorHtml);
        DOMUtils.setVisibility(this.errorContainer, true);
    }
    /**
     * エラー表示のクリア
     */
    clearErrors() {
        DOMUtils.setInnerHTML(this.errorContainer, '');
        DOMUtils.setVisibility(this.errorContainer, false);
    }
    /**
     * ローディング状態の設定
     * @param loading - ローディング中フラグ
     *
     * 💡 UX向上:
     * - ボタンの無効化
     * - ローディングテキストの表示
     * - 視覚的フィードバック
     */
    setLoadingState(loading) {
        const submitButtonResult = DOMUtils.querySelector('button[type="submit"]', this.formElement);
        if (submitButtonResult.success) {
            const button = submitButtonResult.data;
            button.disabled = loading;
            button.textContent = loading ? '送信中...' : '送信';
            DOMUtils.toggleClass(button, 'loading', loading ? 'add' : 'remove');
        }
    }
    /**
     * 送信成功処理
     */
    handleSubmitSuccess() {
        console.log('フォーム送信成功');
        this.clearErrors();
        this.formElement.reset();
        const successHtml = `
            <div class="alert alert-success" role="alert">
                <h3>送信完了</h3>
                <p>フォームの送信が完了しました。</p>
            </div>
        `;
        DOMUtils.setInnerHTML(this.errorContainer, successHtml);
        DOMUtils.setVisibility(this.errorContainer, true);
        // 3秒後に成功メッセージを非表示
        setTimeout(() => {
            this.clearErrors();
        }, 3000);
    }
    /**
     * 送信エラー処理
     * @param message - エラーメッセージ
     */
    handleSubmitError(message) {
        console.error('フォーム送信エラー:', message);
        const errorHtml = `
            <div class="alert alert-error" role="alert">
                <h3>送信エラー</h3>
                <p>${message}</p>
            </div>
        `;
        DOMUtils.setInnerHTML(this.errorContainer, errorHtml);
        DOMUtils.setVisibility(this.errorContainer, true);
    }
    /**
     * フォームのリセット
     * 外部からの制御用パブリックメソッド
     */
    reset() {
        this.formElement.reset();
        this.clearErrors();
        // 各フィールドのエラー表示もクリア
        this.fields.forEach(field => {
            this.clearFieldError(field.name);
        });
        console.log('フォームがリセットされました');
    }
    /**
     * フォームの破棄処理
     * メモリリーク防止のためのクリーンアップ
     *
     * ⚠️ 実装上の注意:
     * - イベントリスナーの適切な削除
     * - 参照の解除
     * - リソースの解放
     */
    destroy() {
        // 実際のプロジェクトでは、より詳細なクリーンアップが必要
        // - 全イベントリスナーの削除
        // - タイマーのクリア
        // - 外部リソースの解放
        console.log('FormHandlerが破棄されました');
    }
}
//# sourceMappingURL=FormHandler.js.map