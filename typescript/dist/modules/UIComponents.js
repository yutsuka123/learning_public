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
import { DOMUtils } from './DOMUtils.js';
/**
 * 基底コンポーネントクラス
 * 全てのUIコンポーネントの共通機能を提供
 */
class BaseComponent {
    constructor(element) {
        this.isDestroyed = false;
        this.element = element;
    }
    /**
     * コンポーネントの表示
     */
    show() {
        if (this.isDestroyed)
            return;
        DOMUtils.setVisibility(this.element, true);
    }
    /**
     * コンポーネントの非表示
     */
    hide() {
        if (this.isDestroyed)
            return;
        DOMUtils.setVisibility(this.element, false);
    }
    /**
     * コンポーネントの破棄
     * メモリリーク防止のための基底実装
     */
    destroy() {
        this.isDestroyed = true;
        this.element.remove();
    }
    /**
     * 要素の取得
     */
    getElement() {
        return this.element;
    }
}
/**
 * カードコンポーネントクラス
 * マテリアルデザイン風のカード要素を生成
 */
export class Card extends BaseComponent {
    /**
     * コンストラクタ
     * @param config - カードの設定
     *
     * 💡 ポイント:
     * - Builderパターンによる柔軟な構築
     * - 型安全な設定オブジェクト
     * - アクセシビリティの考慮
     */
    constructor(config) {
        // カード要素の作成
        const cardResult = DOMUtils.createElement('div', {
            className: `card ${config.className || ''}`,
            attributes: {
                'role': 'article',
                'aria-labelledby': `card-title-${Date.now()}`
            }
        });
        if (!cardResult.success) {
            throw new Error(`カードの作成に失敗しました: ${cardResult.error}`);
        }
        super(cardResult.data);
        this.config = config;
        this.buildCard();
    }
    /**
     * カード要素の構築
     * プライベートメソッドで内部実装を隠蔽
     */
    buildCard() {
        let cardHtml = '';
        // 画像セクション
        if (this.config.imageUrl) {
            cardHtml += `
                <div class="card-image">
                    <img src="${this.config.imageUrl}" 
                         alt="${this.config.title}" 
                         loading="lazy">
                </div>
            `;
        }
        // コンテンツセクション
        cardHtml += `
            <div class="card-content">
                <h3 class="card-title" id="card-title-${Date.now()}">
                    ${this.config.title}
                </h3>
                <div class="card-body">
                    ${this.config.content}
                </div>
            </div>
        `;
        // アクションセクション
        if (this.config.actions && this.config.actions.length > 0) {
            cardHtml += '<div class="card-actions">';
            this.config.actions.forEach((action, index) => {
                const variant = action.variant || 'primary';
                cardHtml += `
                    <button class="btn btn-${variant}" data-action="${index}">
                        ${action.text}
                    </button>
                `;
            });
            cardHtml += '</div>';
        }
        // HTMLの設定
        DOMUtils.setInnerHTML(this.element, cardHtml);
        // アクションボタンのイベントリスナー設定
        this.setupActionListeners();
    }
    /**
     * アクションボタンのイベントリスナー設定
     */
    setupActionListeners() {
        if (!this.config.actions)
            return;
        this.config.actions.forEach((action, index) => {
            const buttonResult = DOMUtils.querySelector(`[data-action="${index}"]`, this.element);
            if (buttonResult.success) {
                DOMUtils.addEventListener(buttonResult.data, 'click', () => {
                    if (!this.isDestroyed) {
                        action.onClick();
                    }
                });
            }
        });
    }
    /**
     * カードコンテンツの更新
     * @param content - 新しいコンテンツ
     */
    updateContent(content) {
        if (this.isDestroyed)
            return;
        const bodyResult = DOMUtils.querySelector('.card-body', this.element);
        if (bodyResult.success) {
            bodyResult.data.textContent = content;
        }
    }
    /**
     * カードタイトルの更新
     * @param title - 新しいタイトル
     */
    updateTitle(title) {
        if (this.isDestroyed)
            return;
        const titleResult = DOMUtils.querySelector('.card-title', this.element);
        if (titleResult.success) {
            titleResult.data.textContent = title;
        }
    }
}
/**
 * モーダルダイアログクラス
 * アクセシブルなモーダル実装
 */
export class Modal extends BaseComponent {
    /**
     * コンストラクタ
     * @param config - モーダルの設定
     *
     * 💡 ポイント:
     * - アクセシビリティ（フォーカス管理）
     * - キーボードナビゲーション
     * - WAI-ARIA準拠
     */
    constructor(config) {
        // モーダル要素の作成
        const modalResult = DOMUtils.createElement('div', {
            className: 'modal',
            attributes: {
                'role': 'dialog',
                'aria-modal': 'true',
                'aria-labelledby': `modal-title-${Date.now()}`,
                'tabindex': '-1'
            }
        });
        if (!modalResult.success) {
            throw new Error(`モーダルの作成に失敗しました: ${modalResult.error}`);
        }
        super(modalResult.data);
        this.previousFocusElement = null;
        this.config = config;
        // バックドロップの作成
        const backdropResult = DOMUtils.createElement('div', {
            className: 'modal-backdrop'
        });
        if (!backdropResult.success) {
            throw new Error(`バックドロップの作成に失敗しました: ${backdropResult.error}`);
        }
        this.backdrop = backdropResult.data;
        this.buildModal();
        this.setupEventListeners();
    }
    /**
     * モーダル要素の構築
     */
    buildModal() {
        let modalHtml = `
            <div class="modal-dialog">
                <div class="modal-content">
                    <div class="modal-header">
                        <h2 class="modal-title" id="modal-title-${Date.now()}">
                            ${this.config.title}
                        </h2>
        `;
        if (this.config.showCloseButton !== false) {
            modalHtml += `
                <button class="modal-close" aria-label="閉じる">
                    <span aria-hidden="true">&times;</span>
                </button>
            `;
        }
        modalHtml += `
                    </div>
                    <div class="modal-body">
                        ${this.config.content}
                    </div>
        `;
        if (this.config.actions && this.config.actions.length > 0) {
            modalHtml += '<div class="modal-footer">';
            this.config.actions.forEach((action, index) => {
                const variant = action.variant || 'primary';
                modalHtml += `
                    <button class="btn btn-${variant}" data-modal-action="${index}">
                        ${action.text}
                    </button>
                `;
            });
            modalHtml += '</div>';
        }
        modalHtml += `
                </div>
            </div>
        `;
        DOMUtils.setInnerHTML(this.element, modalHtml);
    }
    /**
     * イベントリスナーの設定
     */
    setupEventListeners() {
        // 閉じるボタン
        if (this.config.showCloseButton !== false) {
            const closeButtonResult = DOMUtils.querySelector('.modal-close', this.element);
            if (closeButtonResult.success) {
                DOMUtils.addEventListener(closeButtonResult.data, 'click', () => this.close());
            }
        }
        // バックドロップクリック
        if (this.config.closeOnBackdropClick !== false) {
            DOMUtils.addEventListener(this.backdrop, 'click', () => this.close());
        }
        // Escapeキー
        if (this.config.closeOnEscape !== false) {
            DOMUtils.addEventListener(document, 'keydown', (event) => {
                const keyEvent = event;
                if (keyEvent.key === 'Escape') {
                    this.close();
                }
            });
        }
        // アクションボタン
        if (this.config.actions) {
            this.config.actions.forEach((action, index) => {
                const buttonResult = DOMUtils.querySelector(`[data-modal-action="${index}"]`, this.element);
                if (buttonResult.success) {
                    DOMUtils.addEventListener(buttonResult.data, 'click', () => {
                        if (!this.isDestroyed) {
                            action.onClick();
                        }
                    });
                }
            });
        }
    }
    /**
     * モーダルの表示
     */
    show() {
        if (this.isDestroyed)
            return;
        // 現在のフォーカス要素を記憶
        this.previousFocusElement = document.activeElement;
        // DOMに追加
        document.body.appendChild(this.backdrop);
        document.body.appendChild(this.element);
        // 表示
        super.show();
        DOMUtils.setVisibility(this.backdrop, true);
        // body のスクロールを無効化
        document.body.style.overflow = 'hidden';
        // フォーカスをモーダルに移動
        this.element.focus();
    }
    /**
     * モーダルの非表示・閉じる
     */
    close() {
        if (this.isDestroyed)
            return;
        // 表示状態を非表示に
        super.hide();
        DOMUtils.setVisibility(this.backdrop, false);
        // body のスクロールを復元
        document.body.style.overflow = '';
        // フォーカスを元の要素に戻す
        if (this.previousFocusElement) {
            this.previousFocusElement.focus();
        }
        // DOMから削除
        setTimeout(() => {
            if (!this.isDestroyed) {
                this.backdrop.remove();
                this.element.remove();
            }
        }, 300); // アニメーション時間を考慮
    }
    /**
     * モーダルの破棄
     */
    destroy() {
        this.close();
        super.destroy();
        this.backdrop.remove();
    }
}
/**
 * ツールチップクラス
 * アクセシブルなツールチップ実装
 */
export class Tooltip extends BaseComponent {
    /**
     * コンストラクタ
     * @param targetElement - ツールチップを表示する対象要素
     * @param config - ツールチップの設定
     */
    constructor(targetElement, config) {
        // ツールチップ要素の作成
        const tooltipResult = DOMUtils.createElement('div', {
            className: `tooltip tooltip-${config.position || 'top'}`,
            textContent: config.text,
            attributes: {
                'role': 'tooltip',
                'aria-hidden': 'true'
            }
        });
        if (!tooltipResult.success) {
            throw new Error(`ツールチップの作成に失敗しました: ${tooltipResult.error}`);
        }
        super(tooltipResult.data);
        this.showTimeout = null;
        this.hideTimeout = null;
        this.targetElement = targetElement;
        this.config = config;
        this.setupTargetElement();
        this.setupEventListeners();
        // DOMに追加
        document.body.appendChild(this.element);
        this.hide();
    }
    /**
     * 対象要素の設定
     */
    setupTargetElement() {
        // ARIA属性の設定
        const tooltipId = `tooltip-${Date.now()}`;
        this.element.id = tooltipId;
        this.targetElement.setAttribute('aria-describedby', tooltipId);
    }
    /**
     * イベントリスナーの設定
     */
    setupEventListeners() {
        const trigger = this.config.trigger || 'hover';
        if (trigger === 'hover') {
            DOMUtils.addEventListener(this.targetElement, 'mouseenter', () => this.showWithDelay());
            DOMUtils.addEventListener(this.targetElement, 'mouseleave', () => this.hideWithDelay());
            // キーボードアクセシビリティ
            DOMUtils.addEventListener(this.targetElement, 'focus', () => this.showWithDelay());
            DOMUtils.addEventListener(this.targetElement, 'blur', () => this.hideWithDelay());
        }
        else if (trigger === 'click') {
            DOMUtils.addEventListener(this.targetElement, 'click', () => this.toggle());
        }
    }
    /**
     * 遅延付きでツールチップを表示
     */
    showWithDelay() {
        if (this.hideTimeout) {
            clearTimeout(this.hideTimeout);
            this.hideTimeout = null;
        }
        const delay = this.config.delay || 500;
        this.showTimeout = window.setTimeout(() => {
            this.showTooltip();
        }, delay);
    }
    /**
     * 遅延付きでツールチップを非表示
     */
    hideWithDelay() {
        if (this.showTimeout) {
            clearTimeout(this.showTimeout);
            this.showTimeout = null;
        }
        this.hideTimeout = window.setTimeout(() => {
            this.hideTooltip();
        }, 100);
    }
    /**
     * ツールチップの表示
     */
    showTooltip() {
        if (this.isDestroyed)
            return;
        this.positionTooltip();
        super.show();
        this.element.setAttribute('aria-hidden', 'false');
    }
    /**
     * ツールチップの非表示
     */
    hideTooltip() {
        if (this.isDestroyed)
            return;
        super.hide();
        this.element.setAttribute('aria-hidden', 'true');
    }
    /**
     * ツールチップの位置調整
     */
    positionTooltip() {
        const targetRect = this.targetElement.getBoundingClientRect();
        const tooltipRect = this.element.getBoundingClientRect();
        const position = this.config.position || 'top';
        let left = 0;
        let top = 0;
        switch (position) {
            case 'top':
                left = targetRect.left + (targetRect.width / 2) - (tooltipRect.width / 2);
                top = targetRect.top - tooltipRect.height - 8;
                break;
            case 'bottom':
                left = targetRect.left + (targetRect.width / 2) - (tooltipRect.width / 2);
                top = targetRect.bottom + 8;
                break;
            case 'left':
                left = targetRect.left - tooltipRect.width - 8;
                top = targetRect.top + (targetRect.height / 2) - (tooltipRect.height / 2);
                break;
            case 'right':
                left = targetRect.right + 8;
                top = targetRect.top + (targetRect.height / 2) - (tooltipRect.height / 2);
                break;
        }
        // 画面外に出ないように調整
        left = Math.max(8, Math.min(left, window.innerWidth - tooltipRect.width - 8));
        top = Math.max(8, Math.min(top, window.innerHeight - tooltipRect.height - 8));
        this.element.style.left = `${left}px`;
        this.element.style.top = `${top}px`;
    }
    /**
     * ツールチップの表示/非表示切り替え
     */
    toggle() {
        const isVisible = this.element.getAttribute('aria-hidden') === 'false';
        if (isVisible) {
            this.hideTooltip();
        }
        else {
            this.showTooltip();
        }
    }
    /**
     * ツールチップテキストの更新
     * @param text - 新しいテキスト
     */
    updateText(text) {
        if (this.isDestroyed)
            return;
        this.element.textContent = text;
    }
    /**
     * ツールチップの破棄
     */
    destroy() {
        if (this.showTimeout) {
            clearTimeout(this.showTimeout);
        }
        if (this.hideTimeout) {
            clearTimeout(this.hideTimeout);
        }
        // ARIA属性の削除
        this.targetElement.removeAttribute('aria-describedby');
        super.destroy();
    }
}
//# sourceMappingURL=UIComponents.js.map