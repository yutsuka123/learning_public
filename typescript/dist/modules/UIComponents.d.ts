/**
 * 再利用可能UIコンポーネントクラス集
 *
 * 🎯 目的:
 * - モダンなUIコンポーネントの提供
 * - 型安全で再利用可能なUI部品
 * - アクセシビリティとユーザビリティの向上
 *
 * 📚 主な機能:
 * - カード型コンポーネント
 * - モーダルダイアログ
 * - ツールチップ
 * - プログレスバー
 * - アコーディオン
 *
 * 💡 設計パターン:
 * - Builderパターン（コンポーネント構築）
 * - Factoryパターン（コンポーネント生成）
 * - Decoratorパターン（機能拡張）
 *
 * ⚠️ 注意点:
 * - アクセシビリティ（ARIA属性）の考慮
 * - レスポンシブデザインの対応
 * - メモリリークの防止
 */
/**
 * 基底コンポーネントクラス
 * 全てのUIコンポーネントの共通機能を提供
 */
declare abstract class BaseComponent {
    protected element: HTMLElement;
    protected isDestroyed: boolean;
    constructor(element: HTMLElement);
    /**
     * コンポーネントの表示
     */
    show(): void;
    /**
     * コンポーネントの非表示
     */
    hide(): void;
    /**
     * コンポーネントの破棄
     * メモリリーク防止のための基底実装
     */
    destroy(): void;
    /**
     * 要素の取得
     */
    getElement(): HTMLElement;
}
/**
 * カードコンポーネントの設定
 */
interface CardConfig {
    readonly title: string;
    readonly content: string;
    readonly imageUrl?: string;
    readonly actions?: ReadonlyArray<{
        readonly text: string;
        readonly onClick: () => void;
        readonly variant?: 'primary' | 'secondary' | 'danger';
    }>;
    readonly className?: string;
}
/**
 * カードコンポーネントクラス
 * マテリアルデザイン風のカード要素を生成
 */
export declare class Card extends BaseComponent {
    private readonly config;
    /**
     * コンストラクタ
     * @param config - カードの設定
     *
     * 💡 ポイント:
     * - Builderパターンによる柔軟な構築
     * - 型安全な設定オブジェクト
     * - アクセシビリティの考慮
     */
    constructor(config: CardConfig);
    /**
     * カード要素の構築
     * プライベートメソッドで内部実装を隠蔽
     */
    private buildCard;
    /**
     * アクションボタンのイベントリスナー設定
     */
    private setupActionListeners;
    /**
     * カードコンテンツの更新
     * @param content - 新しいコンテンツ
     */
    updateContent(content: string): void;
    /**
     * カードタイトルの更新
     * @param title - 新しいタイトル
     */
    updateTitle(title: string): void;
}
/**
 * モーダルダイアログの設定
 */
interface ModalConfig {
    readonly title: string;
    readonly content: string;
    readonly showCloseButton?: boolean;
    readonly closeOnBackdropClick?: boolean;
    readonly closeOnEscape?: boolean;
    readonly actions?: ReadonlyArray<{
        readonly text: string;
        readonly onClick: () => void;
        readonly variant?: 'primary' | 'secondary' | 'danger';
    }>;
}
/**
 * モーダルダイアログクラス
 * アクセシブルなモーダル実装
 */
export declare class Modal extends BaseComponent {
    private readonly config;
    private readonly backdrop;
    private previousFocusElement;
    /**
     * コンストラクタ
     * @param config - モーダルの設定
     *
     * 💡 ポイント:
     * - アクセシビリティ（フォーカス管理）
     * - キーボードナビゲーション
     * - WAI-ARIA準拠
     */
    constructor(config: ModalConfig);
    /**
     * モーダル要素の構築
     */
    private buildModal;
    /**
     * イベントリスナーの設定
     */
    private setupEventListeners;
    /**
     * モーダルの表示
     */
    show(): void;
    /**
     * モーダルの非表示・閉じる
     */
    close(): void;
    /**
     * モーダルの破棄
     */
    destroy(): void;
}
/**
 * ツールチップの設定
 */
interface TooltipConfig {
    readonly text: string;
    readonly position?: 'top' | 'bottom' | 'left' | 'right';
    readonly trigger?: 'hover' | 'click';
    readonly delay?: number;
}
/**
 * ツールチップクラス
 * アクセシブルなツールチップ実装
 */
export declare class Tooltip extends BaseComponent {
    private readonly targetElement;
    private readonly config;
    private showTimeout;
    private hideTimeout;
    /**
     * コンストラクタ
     * @param targetElement - ツールチップを表示する対象要素
     * @param config - ツールチップの設定
     */
    constructor(targetElement: HTMLElement, config: TooltipConfig);
    /**
     * 対象要素の設定
     */
    private setupTargetElement;
    /**
     * イベントリスナーの設定
     */
    private setupEventListeners;
    /**
     * 遅延付きでツールチップを表示
     */
    private showWithDelay;
    /**
     * 遅延付きでツールチップを非表示
     */
    private hideWithDelay;
    /**
     * ツールチップの表示
     */
    private showTooltip;
    /**
     * ツールチップの非表示
     */
    private hideTooltip;
    /**
     * ツールチップの位置調整
     */
    private positionTooltip;
    /**
     * ツールチップの表示/非表示切り替え
     */
    toggle(): void;
    /**
     * ツールチップテキストの更新
     * @param text - 新しいテキスト
     */
    updateText(text: string): void;
    /**
     * ツールチップの破棄
     */
    destroy(): void;
}
export {};
