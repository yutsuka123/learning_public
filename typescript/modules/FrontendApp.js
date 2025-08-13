/**
 * メインフロントエンドアプリケーションクラス
 *
 * 🎯 目的:
 * - 全モジュールを統合したフロントエンドアプリケーション
 * - 実践的なWebアプリケーションの構築例
 * - プロフェッショナルな開発パターンの実装
 *
 * 📚 主な機能:
 * - ユーザー情報入力フォーム
 * - リアルタイムバリデーション
 * - 動的なカード表示
 * - モーダルダイアログ
 * - ツールチップ
 *
 * 💡 アーキテクチャ:
 * - MVC パターン
 * - モジュール分割
 * - 依存性注入
 * - イベント駆動
 *
 * ⚠️ 注意点:
 * - メモリリークの防止
 * - エラーハンドリングの徹底
 * - アクセシビリティの考慮
 */
import { DOMUtils } from './DOMUtils.js';
import { FormHandler, ValidationRules } from './FormHandler.js';
import { Card, Modal, Tooltip } from './UIComponents.js';
/**
 * メインフロントエンドアプリケーションクラス
 *
 * 💡 設計原則:
 * - 単一責任の原則
 * - 依存性の逆転
 * - 開放閉鎖の原則
 * - インターフェース分離
 */
export class FrontendApp {
    /**
     * コンストラクタ
     * @param config - アプリケーション設定
     *
     * 💡 初期化戦略:
     * - 依存性注入による設定
     * - エラーハンドリング付き初期化
     * - 状態の初期化
     */
    constructor(config) {
        this.formHandler = null;
        this.userCards = [];
        this.tooltips = [];
        this.config = config;
        // 初期状態の設定
        this.state = {
            users: [],
            selectedUser: null,
            isLoading: false,
            error: null
        };
        // コンテナ要素の取得
        const containerResult = DOMUtils.querySelector(config.containerSelector);
        if (!containerResult.success) {
            throw new Error(`アプリケーションコンテナが見つかりません: ${containerResult.error}`);
        }
        this.container = containerResult.data;
        // デバッグモードの設定
        if (config.enableDebugMode) {
            console.log('FrontendApp initialized in debug mode', {
                config,
                container: this.container
            });
        }
    }
    /**
     * アプリケーションの初期化
     * パブリックメソッドとしてエントリーポイントを提供
     *
     * 💡 初期化フロー:
     * 1. UI構造の構築
     * 2. フォームハンドラーの設定
     * 3. イベントリスナーの設定
     * 4. 初期データの読み込み
     */
    async initialize() {
        try {
            console.log('アプリケーション初期化開始');
            // ローディング状態の設定
            this.setState({ isLoading: true, error: null });
            // UI構造の構築
            await this.buildUIStructure();
            // フォームハンドラーの設定
            this.setupFormHandler();
            // その他のUIコンポーネントの設定
            this.setupUIComponents();
            // サンプルデータの読み込み
            await this.loadSampleData();
            console.log('アプリケーション初期化完了');
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error('アプリケーション初期化エラー:', errorMessage);
            this.setState({ error: errorMessage, isLoading: false });
            this.displayError(errorMessage);
        }
        finally {
            this.setState({ isLoading: false });
        }
    }
    /**
     * UI構造の構築
     * HTMLテンプレートを使用した動的UI生成
     *
     * 💡 構造化:
     * - セマンティックHTML
     * - アクセシビリティ対応
     * - レスポンシブデザイン
     */
    async buildUIStructure() {
        const uiTemplate = `
            <div class="frontend-app">
                <!-- ヘッダーセクション -->
                <header class="app-header">
                    <h1>TypeScript フロントエンドアプリケーション</h1>
                    <p class="app-description">
                        プロフェッショナルなTypeScript開発の実践例
                    </p>
                </header>

                <!-- メインコンテンツエリア -->
                <main class="app-main">
                    <!-- ユーザー登録フォーム -->
                    <section class="form-section" aria-labelledby="form-title">
                        <h2 id="form-title">ユーザー情報登録</h2>
                        
                        <form id="user-form" class="user-form" novalidate>
                            <div class="form-group">
                                <label for="name" class="form-label">
                                    名前 <span class="required">*</span>
                                </label>
                                <input 
                                    type="text" 
                                    id="name" 
                                    name="name" 
                                    class="form-input"
                                    placeholder="山田太郎"
                                    required
                                    aria-describedby="name-help name-error"
                                >
                                <div id="name-help" class="form-help">
                                    2文字以上50文字以内で入力してください
                                </div>
                                <div id="name-error" class="form-error" data-error="name"></div>
                            </div>

                            <div class="form-group">
                                <label for="email" class="form-label">
                                    メールアドレス <span class="required">*</span>
                                </label>
                                <input 
                                    type="email" 
                                    id="email" 
                                    name="email" 
                                    class="form-input"
                                    placeholder="yamada@example.com"
                                    required
                                    aria-describedby="email-help email-error"
                                >
                                <div id="email-help" class="form-help">
                                    有効なメールアドレスを入力してください
                                </div>
                                <div id="email-error" class="form-error" data-error="email"></div>
                            </div>

                            <div class="form-group">
                                <label for="age" class="form-label">
                                    年齢 <span class="required">*</span>
                                </label>
                                <input 
                                    type="number" 
                                    id="age" 
                                    name="age" 
                                    class="form-input"
                                    placeholder="25"
                                    min="1"
                                    max="120"
                                    required
                                    aria-describedby="age-help age-error"
                                >
                                <div id="age-help" class="form-help">
                                    1歳以上120歳以下で入力してください
                                </div>
                                <div id="age-error" class="form-error" data-error="age"></div>
                            </div>

                            <div class="form-group">
                                <label for="bio" class="form-label">
                                    自己紹介
                                </label>
                                <textarea 
                                    id="bio" 
                                    name="bio" 
                                    class="form-textarea"
                                    placeholder="自己紹介を入力してください（任意）"
                                    rows="4"
                                    maxlength="500"
                                    aria-describedby="bio-help bio-error"
                                ></textarea>
                                <div id="bio-help" class="form-help">
                                    500文字以内で入力してください（任意）
                                </div>
                                <div id="bio-error" class="form-error" data-error="bio"></div>
                            </div>

                            <div class="form-actions">
                                <button type="submit" class="btn btn-primary">
                                    登録
                                </button>
                                <button type="button" class="btn btn-secondary" id="reset-btn">
                                    リセット
                                </button>
                            </div>
                        </form>

                        <!-- エラー・成功メッセージ表示エリア -->
                        <div class="error-container" style="display: none;"></div>
                    </section>

                    <!-- ユーザー一覧表示セクション -->
                    <section class="users-section" aria-labelledby="users-title">
                        <h2 id="users-title">登録済みユーザー</h2>
                        <div id="users-grid" class="users-grid">
                            <!-- 動的に生成されるユーザーカード -->
                        </div>
                    </section>

                    <!-- 統計情報セクション -->
                    <section class="stats-section" aria-labelledby="stats-title">
                        <h2 id="stats-title">統計情報</h2>
                        <div class="stats-grid">
                            <div class="stat-card">
                                <h3>総ユーザー数</h3>
                                <span class="stat-value" id="total-users">0</span>
                            </div>
                            <div class="stat-card">
                                <h3>平均年齢</h3>
                                <span class="stat-value" id="average-age">0</span>
                            </div>
                            <div class="stat-card">
                                <h3>最新登録</h3>
                                <span class="stat-value" id="latest-user">-</span>
                            </div>
                        </div>
                    </section>
                </main>

                <!-- フッター -->
                <footer class="app-footer">
                    <p>&copy; 2024 TypeScript Learning Project</p>
                </footer>
            </div>
        `;
        // UIテンプレートの挿入
        DOMUtils.setInnerHTML(this.container, uiTemplate, false);
    }
    /**
     * フォームハンドラーの設定
     * バリデーションルールとフォーム処理の設定
     */
    setupFormHandler() {
        const formFields = [
            {
                name: 'name',
                label: '名前',
                type: 'text',
                required: true,
                validationRules: [
                    ValidationRules.required('名前'),
                    ValidationRules.minLength(2, '名前'),
                    ValidationRules.maxLength(50, '名前')
                ],
                realTimeValidation: true
            },
            {
                name: 'email',
                label: 'メールアドレス',
                type: 'email',
                required: true,
                validationRules: [
                    ValidationRules.required('メールアドレス'),
                    ValidationRules.email('メールアドレス')
                ],
                realTimeValidation: true
            },
            {
                name: 'age',
                label: '年齢',
                type: 'number',
                required: true,
                validationRules: [
                    ValidationRules.required('年齢'),
                    ValidationRules.number('年齢', 1, 120)
                ],
                realTimeValidation: true
            },
            {
                name: 'bio',
                label: '自己紹介',
                type: 'text',
                required: false,
                validationRules: [
                    ValidationRules.maxLength(500, '自己紹介')
                ],
                realTimeValidation: false
            }
        ];
        // フォームハンドラーの作成
        this.formHandler = new FormHandler('#user-form', formFields, this.handleFormSubmit.bind(this), '.error-container');
        // リセットボタンの設定
        this.setupResetButton();
    }
    /**
     * リセットボタンの設定
     */
    setupResetButton() {
        const resetBtnResult = DOMUtils.querySelector('#reset-btn');
        if (resetBtnResult.success) {
            DOMUtils.addEventListener(resetBtnResult.data, 'click', () => {
                if (this.formHandler) {
                    this.formHandler.reset();
                }
            });
        }
    }
    /**
     * その他のUIコンポーネントの設定
     */
    setupUIComponents() {
        // ツールチップの設定
        this.setupTooltips();
        // その他のインタラクティブ要素の設定
        this.setupInteractiveElements();
    }
    /**
     * ツールチップの設定
     */
    setupTooltips() {
        const tooltipTargets = [
            { selector: '.required', text: 'この項目は必須です', position: 'top' },
            { selector: '#total-users', text: '登録されたユーザーの総数', position: 'bottom' },
            { selector: '#average-age', text: '全ユーザーの平均年齢', position: 'bottom' }
        ];
        tooltipTargets.forEach(({ selector, text, position }) => {
            const elementsResult = DOMUtils.querySelectorAll(selector);
            if (elementsResult.success) {
                elementsResult.data.forEach(element => {
                    const tooltip = new Tooltip(element, {
                        text,
                        position,
                        trigger: 'hover',
                        delay: 300
                    });
                    this.tooltips.push(tooltip);
                });
            }
        });
    }
    /**
     * インタラクティブ要素の設定
     */
    setupInteractiveElements() {
        // 今後の機能拡張用
        console.log('インタラクティブ要素を設定しました');
    }
    /**
     * フォーム送信処理
     * @param formData - フォームデータ
     * @returns 送信成功フラグ
     */
    async handleFormSubmit(formData) {
        try {
            console.log('フォーム送信処理開始:', formData);
            // 送信のシミュレーション（実際のAPIコール）
            await this.simulateApiCall(1000);
            // ユーザーデータの作成
            const newUser = {
                id: `user-${Date.now()}`,
                name: formData.name,
                email: formData.email,
                age: parseInt(formData.age, 10),
                bio: formData.bio || '',
                createdAt: new Date()
            };
            // 状態の更新
            this.setState({
                users: [...this.state.users, newUser]
            });
            // UIの更新
            this.addUserCard(newUser);
            this.updateStatistics();
            return true;
        }
        catch (error) {
            const errorMessage = error instanceof Error ? error.message : String(error);
            console.error('フォーム送信エラー:', errorMessage);
            return false;
        }
    }
    /**
     * ユーザーカードの追加
     * @param user - ユーザー情報
     */
    addUserCard(user) {
        const card = new Card({
            title: user.name,
            content: `
                <div class="user-card-content">
                    <p><strong>メール:</strong> ${user.email}</p>
                    <p><strong>年齢:</strong> ${user.age}歳</p>
                    <p><strong>登録日:</strong> ${user.createdAt.toLocaleDateString('ja-JP')}</p>
                    ${user.bio ? `<p><strong>自己紹介:</strong> ${user.bio}</p>` : ''}
                </div>
            `,
            actions: [
                {
                    text: '詳細',
                    onClick: () => this.showUserDetail(user),
                    variant: 'primary'
                },
                {
                    text: '削除',
                    onClick: () => this.deleteUser(user.id),
                    variant: 'danger'
                }
            ],
            className: 'user-card'
        });
        // グリッドに追加
        const gridResult = DOMUtils.querySelector('#users-grid');
        if (gridResult.success) {
            gridResult.data.appendChild(card.getElement());
            this.userCards.push(card);
        }
    }
    /**
     * ユーザー詳細の表示
     * @param user - ユーザー情報
     */
    showUserDetail(user) {
        const modal = new Modal({
            title: `${user.name}さんの詳細情報`,
            content: `
                <div class="user-detail">
                    <div class="detail-row">
                        <span class="detail-label">ID:</span>
                        <span class="detail-value">${user.id}</span>
                    </div>
                    <div class="detail-row">
                        <span class="detail-label">名前:</span>
                        <span class="detail-value">${user.name}</span>
                    </div>
                    <div class="detail-row">
                        <span class="detail-label">メールアドレス:</span>
                        <span class="detail-value">${user.email}</span>
                    </div>
                    <div class="detail-row">
                        <span class="detail-label">年齢:</span>
                        <span class="detail-value">${user.age}歳</span>
                    </div>
                    <div class="detail-row">
                        <span class="detail-label">登録日時:</span>
                        <span class="detail-value">${user.createdAt.toLocaleString('ja-JP')}</span>
                    </div>
                    ${user.bio ? `
                    <div class="detail-row">
                        <span class="detail-label">自己紹介:</span>
                        <span class="detail-value">${user.bio}</span>
                    </div>
                    ` : ''}
                </div>
            `,
            actions: [
                {
                    text: '閉じる',
                    onClick: () => modal.close(),
                    variant: 'secondary'
                }
            ]
        });
        modal.show();
    }
    /**
     * ユーザーの削除
     * @param userId - ユーザーID
     */
    deleteUser(userId) {
        const modal = new Modal({
            title: '削除確認',
            content: 'このユーザーを削除してもよろしいですか？',
            actions: [
                {
                    text: 'キャンセル',
                    onClick: () => modal.close(),
                    variant: 'secondary'
                },
                {
                    text: '削除',
                    onClick: () => {
                        this.performDeleteUser(userId);
                        modal.close();
                    },
                    variant: 'danger'
                }
            ]
        });
        modal.show();
    }
    /**
     * ユーザー削除の実行
     * @param userId - ユーザーID
     */
    performDeleteUser(userId) {
        // 状態から削除
        this.setState({
            users: this.state.users.filter(user => user.id !== userId)
        });
        // UIから削除
        const cardIndex = this.userCards.findIndex(card => card.getElement().querySelector('.card-title')?.textContent ===
            this.state.users.find(user => user.id === userId)?.name);
        if (cardIndex >= 0) {
            this.userCards[cardIndex].destroy();
            this.userCards.splice(cardIndex, 1);
        }
        // 統計情報の更新
        this.updateStatistics();
    }
    /**
     * 統計情報の更新
     */
    updateStatistics() {
        const users = this.state.users;
        // 総ユーザー数
        const totalUsersResult = DOMUtils.querySelector('#total-users');
        if (totalUsersResult.success) {
            totalUsersResult.data.textContent = users.length.toString();
        }
        // 平均年齢
        if (users.length > 0) {
            const averageAge = users.reduce((sum, user) => sum + user.age, 0) / users.length;
            const averageAgeResult = DOMUtils.querySelector('#average-age');
            if (averageAgeResult.success) {
                averageAgeResult.data.textContent = Math.round(averageAge).toString();
            }
            // 最新登録ユーザー
            const latestUser = users[users.length - 1];
            const latestUserResult = DOMUtils.querySelector('#latest-user');
            if (latestUserResult.success) {
                latestUserResult.data.textContent = latestUser.name;
            }
        }
        else {
            const averageAgeResult = DOMUtils.querySelector('#average-age');
            const latestUserResult = DOMUtils.querySelector('#latest-user');
            if (averageAgeResult.success)
                averageAgeResult.data.textContent = '0';
            if (latestUserResult.success)
                latestUserResult.data.textContent = '-';
        }
    }
    /**
     * サンプルデータの読み込み
     */
    async loadSampleData() {
        const sampleUsers = [
            {
                id: 'sample-1',
                name: '田中太郎',
                email: 'tanaka@example.com',
                age: 28,
                bio: 'フロントエンドエンジニアです。TypeScriptが大好きです。',
                createdAt: new Date('2024-01-15')
            },
            {
                id: 'sample-2',
                name: '佐藤花子',
                email: 'sato@example.com',
                age: 32,
                bio: 'UIデザイナーとして働いています。',
                createdAt: new Date('2024-01-20')
            }
        ];
        // 状態の更新
        this.setState({ users: sampleUsers });
        // UIの更新
        sampleUsers.forEach(user => this.addUserCard(user));
        this.updateStatistics();
    }
    /**
     * APIコールのシミュレーション
     * @param delay - 遅延時間（ミリ秒）
     */
    async simulateApiCall(delay) {
        return new Promise((resolve) => {
            setTimeout(resolve, delay);
        });
    }
    /**
     * 状態の更新
     * @param newState - 新しい状態（部分更新）
     */
    setState(newState) {
        this.state = { ...this.state, ...newState };
        if (this.config.enableDebugMode) {
            console.log('State updated:', this.state);
        }
    }
    /**
     * エラー表示
     * @param message - エラーメッセージ
     */
    displayError(message) {
        const errorHtml = `
            <div class="alert alert-error" role="alert">
                <h3>エラーが発生しました</h3>
                <p>${message}</p>
            </div>
        `;
        DOMUtils.setInnerHTML(this.container, errorHtml);
    }
    /**
     * アプリケーションの破棄
     * メモリリーク防止のためのクリーンアップ
     */
    destroy() {
        // フォームハンドラーの破棄
        if (this.formHandler) {
            this.formHandler.destroy();
            this.formHandler = null;
        }
        // ユーザーカードの破棄
        this.userCards.forEach(card => card.destroy());
        this.userCards = [];
        // ツールチップの破棄
        this.tooltips.forEach(tooltip => tooltip.destroy());
        this.tooltips = [];
        console.log('FrontendAppが破棄されました');
    }
    /**
     * 現在の状態を取得
     * デバッグ用のパブリックメソッド
     */
    getState() {
        return { ...this.state };
    }
}
