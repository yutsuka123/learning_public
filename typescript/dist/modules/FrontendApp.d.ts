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
/**
 * ユーザー情報の型定義
 * アプリケーションで扱うデータモデル
 */
interface UserProfile {
    readonly id: string;
    readonly name: string;
    readonly email: string;
    readonly age: number;
    readonly bio: string;
    readonly createdAt: Date;
}
/**
 * アプリケーション設定の型定義
 */
interface AppConfig {
    readonly containerSelector: string;
    readonly apiEndpoint: string;
    readonly enableDebugMode: boolean;
    readonly animationDuration: number;
}
/**
 * アプリケーション状態の型定義
 * 状態管理のためのインターフェース
 */
interface AppState {
    users: UserProfile[];
    selectedUser: UserProfile | null;
    isLoading: boolean;
    error: string | null;
}
/**
 * メインフロントエンドアプリケーションクラス
 *
 * 💡 設計原則:
 * - 単一責任の原則
 * - 依存性の逆転
 * - 開放閉鎖の原則
 * - インターフェース分離
 */
export declare class FrontendApp {
    private readonly config;
    private readonly container;
    private formHandler;
    private userCards;
    private tooltips;
    private state;
    /**
     * コンストラクタ
     * @param config - アプリケーション設定
     *
     * 💡 初期化戦略:
     * - 依存性注入による設定
     * - エラーハンドリング付き初期化
     * - 状態の初期化
     */
    constructor(config: AppConfig);
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
    initialize(): Promise<void>;
    /**
     * UI構造の構築
     * HTMLテンプレートを使用した動的UI生成
     *
     * 💡 構造化:
     * - セマンティックHTML
     * - アクセシビリティ対応
     * - レスポンシブデザイン
     */
    private buildUIStructure;
    /**
     * フォームハンドラーの設定
     * バリデーションルールとフォーム処理の設定
     */
    private setupFormHandler;
    /**
     * リセットボタンの設定
     */
    private setupResetButton;
    /**
     * その他のUIコンポーネントの設定
     */
    private setupUIComponents;
    /**
     * ツールチップの設定
     */
    private setupTooltips;
    /**
     * インタラクティブ要素の設定
     */
    private setupInteractiveElements;
    /**
     * フォーム送信処理
     * @param formData - フォームデータ
     * @returns 送信成功フラグ
     */
    private handleFormSubmit;
    /**
     * ユーザーカードの追加
     * @param user - ユーザー情報
     */
    private addUserCard;
    /**
     * ユーザー詳細の表示
     * @param user - ユーザー情報
     */
    private showUserDetail;
    /**
     * ユーザーの削除
     * @param userId - ユーザーID
     */
    private deleteUser;
    /**
     * ユーザー削除の実行
     * @param userId - ユーザーID
     */
    private performDeleteUser;
    /**
     * 統計情報の更新
     */
    private updateStatistics;
    /**
     * サンプルデータの読み込み
     */
    private loadSampleData;
    /**
     * APIコールのシミュレーション
     * @param delay - 遅延時間（ミリ秒）
     */
    private simulateApiCall;
    /**
     * 状態の更新
     * @param newState - 新しい状態（部分更新）
     */
    private setState;
    /**
     * エラー表示
     * @param message - エラーメッセージ
     */
    private displayError;
    /**
     * アプリケーションの破棄
     * メモリリーク防止のためのクリーンアップ
     */
    destroy(): void;
    /**
     * 現在の状態を取得
     * デバッグ用のパブリックメソッド
     */
    getState(): Readonly<AppState>;
}
export {};
