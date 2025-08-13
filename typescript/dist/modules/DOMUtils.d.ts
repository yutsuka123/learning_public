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
 * DOM要素作成時の設定オプション
 * ジェネリクスを使用して型安全性を確保
 */
interface ElementOptions<T extends HTMLElement = HTMLElement> {
    readonly id?: string;
    readonly className?: string;
    readonly textContent?: string;
    readonly innerHTML?: string;
    readonly attributes?: Readonly<Record<string, string>>;
    readonly styles?: Readonly<Partial<CSSStyleDeclaration>>;
    readonly eventListeners?: ReadonlyArray<{
        readonly event: keyof HTMLElementEventMap;
        readonly handler: (event: Event) => void;
        readonly options?: boolean | AddEventListenerOptions;
    }>;
}
/**
 * DOM操作の結果を表すResult型
 * エラーハンドリングを型レベルで強制する
 */
type DOMResult<T> = {
    readonly success: true;
    readonly data: T;
} | {
    readonly success: false;
    readonly error: string;
    readonly element?: string;
};
/**
 * DOM操作ユーティリティクラス
 * 型安全で再利用可能なDOM操作を提供
 */
export declare class DOMUtils {
    /**
     * プライベートコンストラクタ（Singletonパターン）
     * 直接インスタンス化を防ぐ
     */
    private constructor();
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
    static querySelector<T extends Element = Element>(selector: string, parent?: ParentNode): DOMResult<T>;
    /**
     * 複数要素の型安全な取得メソッド
     * @param selector - CSS selector
     * @param parent - 親要素（省略時はdocument）
     * @returns 要素の配列
     */
    static querySelectorAll<T extends Element = Element>(selector: string, parent?: ParentNode): DOMResult<T[]>;
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
    static createElement<K extends keyof HTMLElementTagNameMap>(tagName: K, options?: ElementOptions<HTMLElementTagNameMap[K]>): DOMResult<HTMLElementTagNameMap[K]>;
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
    static addEventListener<K extends keyof HTMLElementEventMap>(element: HTMLElement, event: K, handler: (event: HTMLElementEventMap[K]) => void, options?: boolean | AddEventListenerOptions): DOMResult<void>;
    /**
     * 型安全なイベントリスナー削除メソッド
     * @param element - 対象要素
     * @param event - イベント名
     * @param handler - イベントハンドラー
     * @param options - イベントオプション
     * @returns 処理結果
     */
    static removeEventListener<K extends keyof HTMLElementEventMap>(element: HTMLElement, event: K, handler: (event: HTMLElementEventMap[K]) => void, options?: boolean | EventListenerOptions): DOMResult<void>;
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
    static setVisibility(element: HTMLElement, visible: boolean): DOMResult<void>;
    /**
     * 要素のクラス操作メソッド
     * @param element - 対象要素
     * @param className - クラス名
     * @param action - 操作種別
     * @returns 処理結果
     */
    static toggleClass(element: HTMLElement, className: string, action?: 'add' | 'remove' | 'toggle'): DOMResult<boolean>;
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
    static setInnerHTML(element: HTMLElement, html: string, sanitize?: boolean): DOMResult<void>;
}
export {};
