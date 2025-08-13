/**
 * DOM操作ユーティリティクラス
 *
 * 🎯 目的:
 * - 型安全なDOM操作の提供
 * - 再利用可能なDOM操作メソッドの集約
 * - エラーハンドリングを含む堅牢なDOM操作
 *
 * 📚 主な機能:
 * - 要素の検索と作成
 * - イベントリスナーの管理
 * - 型安全な属性操作
 * - エラーハンドリング付きDOM操作
 *
 * 💡 設計パターン:
 * - Singletonパターン（必要に応じて）
 * - Builderパターン（要素作成時）
 * - 関数型プログラミングの要素を含む
 *
 * ⚠️ 注意点:
 * - DOM要素の存在チェックを必ず行う
 * - 型キャストは最小限に抑える
 * - メモリリークを防ぐためのイベントリスナー管理
 */
/**
 * DOM操作ユーティリティクラス
 * 型安全で再利用可能なDOM操作を提供
 */
export class DOMUtils {
    /**
     * プライベートコンストラクタ（Singletonパターン）
     * 直接インスタンス化を防ぐ
     */
    constructor() {
        // プライベートコンストラクタ
        // 静的メソッドのみを使用することを強制
    }
    /**
     * 型安全な要素取得メソッド
     * @param selector - CSS selector
     * @param parent - 親要素（省略時はdocument）
     * @returns 要素またはnull
     *
     * 💡 ポイント:
     * - ジェネリクスで戻り値の型を指定可能
     * - null安全性を保証
     * - 詳細なエラー情報を提供
     */
    static querySelector(selector, parent = document) {
        try {
            if (!selector || selector.trim().length === 0) {
                return {
                    success: false,
                    error: 'セレクタが空です',
                    element: selector
                };
            }
            const element = parent.querySelector(selector);
            if (!element) {
                return {
                    success: false,
                    error: `要素が見つかりません: ${selector}`,
                    element: selector
                };
            }
            return { success: true, data: element };
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            return {
                success: false,
                error: `DOM要素の取得に失敗しました: ${errorMessage}`,
                element: selector
            };
        }
    }
    /**
     * 複数要素の型安全な取得メソッド
     * @param selector - CSS selector
     * @param parent - 親要素（省略時はdocument）
     * @returns 要素の配列
     */
    static querySelectorAll(selector, parent = document) {
        try {
            if (!selector || selector.trim().length === 0) {
                return {
                    success: false,
                    error: 'セレクタが空です',
                    element: selector
                };
            }
            const elements = Array.from(parent.querySelectorAll(selector));
            return { success: true, data: elements };
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            return {
                success: false,
                error: `DOM要素の取得に失敗しました: ${errorMessage}`,
                element: selector
            };
        }
    }
    /**
     * 型安全な要素作成メソッド（Builderパターン）
     * @param tagName - HTMLタグ名
     * @param options - 要素のオプション設定
     * @returns 作成された要素
     *
     * 💡 ポイント:
     * - ジェネリクスで具体的な要素型を指定
     * - オプション設定で柔軟な要素作成
     * - イベントリスナーの自動登録
     */
    static createElement(tagName, options = {}) {
        try {
            const element = document.createElement(tagName);
            // 基本属性の設定
            if (options.id) {
                element.id = options.id;
            }
            if (options.className) {
                element.className = options.className;
            }
            if (options.textContent) {
                element.textContent = options.textContent;
            }
            if (options.innerHTML) {
                element.innerHTML = options.innerHTML;
            }
            // カスタム属性の設定
            if (options.attributes) {
                Object.entries(options.attributes).forEach(([key, value]) => {
                    element.setAttribute(key, value);
                });
            }
            // スタイルの設定
            if (options.styles) {
                Object.entries(options.styles).forEach(([key, value]) => {
                    if (value !== undefined) {
                        // 型キャストを最小限に抑制
                        element.style[key] = value;
                    }
                });
            }
            // イベントリスナーの設定
            if (options.eventListeners) {
                options.eventListeners.forEach(({ event, handler, options: listenerOptions }) => {
                    element.addEventListener(event, handler, listenerOptions);
                });
            }
            return { success: true, data: element };
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            return {
                success: false,
                error: `要素の作成に失敗しました: ${errorMessage}`,
                element: tagName
            };
        }
    }
    /**
     * 型安全なイベントリスナー追加メソッド
     * @param element - 対象要素
     * @param event - イベント名
     * @param handler - イベントハンドラー
     * @param options - イベントオプション
     * @returns 処理結果
     *
     * 💡 ポイント:
     * - 型安全なイベントハンドリング
     * - メモリリーク防止のための管理機能
     * - エラーハンドリング付き
     */
    static addEventListener(element, event, handler, options) {
        try {
            if (!element) {
                return {
                    success: false,
                    error: '要素がnullまたはundefinedです'
                };
            }
            element.addEventListener(event, handler, options);
            return { success: true, data: undefined };
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            return {
                success: false,
                error: `イベントリスナーの追加に失敗しました: ${errorMessage}`
            };
        }
    }
    /**
     * 型安全なイベントリスナー削除メソッド
     * @param element - 対象要素
     * @param event - イベント名
     * @param handler - イベントハンドラー
     * @param options - イベントオプション
     * @returns 処理結果
     */
    static removeEventListener(element, event, handler, options) {
        try {
            if (!element) {
                return {
                    success: false,
                    error: '要素がnullまたはundefinedです'
                };
            }
            element.removeEventListener(event, handler, options);
            return { success: true, data: undefined };
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            return {
                success: false,
                error: `イベントリスナーの削除に失敗しました: ${errorMessage}`
            };
        }
    }
    /**
     * 要素の表示/非表示切り替えメソッド
     * @param element - 対象要素
     * @param visible - 表示フラグ
     * @returns 処理結果
     *
     * 💡 ポイント:
     * - アクセシビリティを考慮した実装
     * - スタイルとaria属性の同期
     */
    static setVisibility(element, visible) {
        try {
            if (!element) {
                return {
                    success: false,
                    error: '要素がnullまたはundefinedです'
                };
            }
            element.style.display = visible ? '' : 'none';
            element.setAttribute('aria-hidden', visible ? 'false' : 'true');
            return { success: true, data: undefined };
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            return {
                success: false,
                error: `表示状態の変更に失敗しました: ${errorMessage}`
            };
        }
    }
    /**
     * 要素のクラス操作メソッド
     * @param element - 対象要素
     * @param className - クラス名
     * @param action - 操作種別
     * @returns 処理結果
     */
    static toggleClass(element, className, action = 'toggle') {
        try {
            if (!element) {
                return {
                    success: false,
                    error: '要素がnullまたはundefinedです'
                };
            }
            if (!className || className.trim().length === 0) {
                return {
                    success: false,
                    error: 'クラス名が空です'
                };
            }
            let result;
            switch (action) {
                case 'add':
                    element.classList.add(className);
                    result = true;
                    break;
                case 'remove':
                    element.classList.remove(className);
                    result = false;
                    break;
                case 'toggle':
                default:
                    result = element.classList.toggle(className);
                    break;
            }
            return { success: true, data: result };
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            return {
                success: false,
                error: `クラス操作に失敗しました: ${errorMessage}`
            };
        }
    }
    /**
     * 安全な innerHTML 設定メソッド
     * @param element - 対象要素
     * @param html - HTML文字列
     * @param sanitize - サニタイズフラグ
     * @returns 処理結果
     *
     * ⚠️ セキュリティ注意:
     * - XSS攻撃を防ぐため、信頼できないHTMLは使用しない
     * - 可能な限りtextContentを使用する
     */
    static setInnerHTML(element, html, sanitize = true) {
        try {
            if (!element) {
                return {
                    success: false,
                    error: '要素がnullまたはundefinedです'
                };
            }
            if (sanitize) {
                // 基本的なサニタイズ（実際のプロジェクトではDOMPurifyなどを使用）
                const sanitizedHtml = html
                    .replace(/<script\b[^<]*(?:(?!<\/script>)<[^<]*)*<\/script>/gi, '')
                    .replace(/javascript:/gi, '')
                    .replace(/on\w+\s*=/gi, '');
                element.innerHTML = sanitizedHtml;
            }
            else {
                element.innerHTML = html;
            }
            return { success: true, data: undefined };
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            return {
                success: false,
                error: `HTMLの設定に失敗しました: ${errorMessage}`
            };
        }
    }
}
//# sourceMappingURL=DOMUtils.js.map