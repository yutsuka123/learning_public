# TypeScript フロントエンドアプリケーション 詳細使い方ガイド

## 📖 目次

1. [環境準備](#環境準備)
2. [基本的な実行方法](#基本的な実行方法)
3. [ステップバイステップ実行](#ステップバイステップ実行)
4. [各モジュールの詳細使用法](#各モジュールの詳細使用法)
5. [カスタマイズ方法](#カスタマイズ方法)
6. [デバッグ方法](#デバッグ方法)
7. [トラブルシューティング](#トラブルシューティング)
8. [応用例](#応用例)

---

## 🛠️ 環境準備

### 必要なソフトウェア

#### 1. Node.js のインストール
```bash
# Node.js バージョン確認
node --version  # v18.0.0 以上推奨
npm --version   # v9.0.0 以上推奨
```

**インストール方法**:
- [Node.js公式サイト](https://nodejs.org/) からダウンロード
- Windows: インストーラーを実行
- macOS: `brew install node`
- Linux: `sudo apt install nodejs npm`

#### 2. TypeScript のグローバルインストール（推奨）
```bash
# TypeScript をグローバルにインストール
npm install -g typescript

# バージョン確認
tsc --version  # v5.0.0 以上推奨
```

#### 3. 開発用ツールのインストール
```bash
# プロジェクトディレクトリで実行
npm install

# または個別インストール
npm install -D typescript @types/node eslint prettier
```

### エディタ設定

#### VS Code 推奨拡張機能
```json
{
  "recommendations": [
    "ms-vscode.vscode-typescript-next",
    "esbenp.prettier-vscode",
    "ms-vscode.vscode-eslint",
    "usernamehw.errorlens",
    "bradlc.vscode-tailwindcss",
    "formulahendry.auto-rename-tag",
    "christian-kohler.path-intellisense"
  ]
}
```

#### VS Code 設定（.vscode/settings.json）
```json
{
  "typescript.preferences.quoteStyle": "single",
  "typescript.updateImportsOnFileMove.enabled": "always",
  "typescript.inlayHints.parameterNames.enabled": "all",
  "typescript.inlayHints.variableTypes.enabled": true,
  "typescript.inlayHints.functionLikeReturnTypes.enabled": true,
  "editor.formatOnSave": true,
  "editor.codeActionsOnSave": {
    "source.fixAll.eslint": true,
    "source.organizeImports": true
  }
}
```

---

## 🚀 基本的な実行方法

### 方法1: npmスクリプトを使用（最も簡単）

```bash
# プロジェクトルートディレクトリで実行
cd learning_public

# 基本型システム練習を実行
npm run ts:run-basic

# 利用可能なコマンド一覧を表示
npm run ts:help
```

**利用可能なコマンド**:
```bash
npm run ts:run-basic    # 基本型システム練習を実行
npm run ts:run-hello    # Hello Worldを実行
npm run ts:build-all    # 全てのTSファイルをコンパイル
npm run ts:watch        # ファイル変更を監視してコンパイル
npm run ts:clean        # 生成されたJSファイルを削除
```

### 方法2: 手動コンパイル・実行

```bash
# TypeScriptディレクトリに移動
cd typescript

# 1. TypeScriptファイルをコンパイル
npx tsc 01_basic_types_practice.ts

# 2. 生成されたJavaScriptファイルを実行
node 01_basic_types_practice.js
```

### 方法3: ブラウザでの実行

```bash
# 1. 全ファイルをコンパイル
cd typescript
npx tsc --build

# 2. ローカルサーバーを起動
npx http-server . -p 3000

# 3. ブラウザで開く
# http://localhost:3000/frontend-demo.html
```

---

## 📝 ステップバイステップ実行

### ステップ1: 環境確認

```bash
# 1. プロジェクトディレクトリの確認
pwd
ls -la

# 2. 必要なファイルの存在確認
ls typescript/
ls typescript/modules/

# 3. 依存関係の確認
npm list --depth=0
```

**期待される出力**:
```
typescript/
├── 01_basic_types_practice.ts
├── modules/
│   ├── DOMUtils.ts
│   ├── FormHandler.ts
│   ├── UIComponents.ts
│   └── FrontendApp.ts
├── frontend-demo.html
└── tsconfig.json
```

### ステップ2: コンパイル確認

```bash
cd typescript

# TypeScript設定の確認
cat tsconfig.json

# コンパイルテスト（エラーチェックのみ）
npx tsc --noEmit

# 成功時の出力: 何も表示されない
# エラー時の出力: エラーメッセージが表示される
```

### ステップ3: 基本練習の実行

```bash
# 基本型システム練習を実行
npx tsc 01_basic_types_practice.ts && node 01_basic_types_practice.js
```

**期待される出力例**:
```
TypeScript 基本型システム練習プログラム
==========================================

=== 基本型の練習 ===
文字列: 田中太郎 こんにちは、田中太郎さん！
数値: 25 98.5 255 10
真偽値: true false
null/undefined: null undefined

=== 配列型の練習 ===
数値配列: [ 1, 2, 3, 4, 5 ]
文字列配列: [ 'りんご', 'バナナ', 'オレンジ' ]
...
```

### ステップ4: フロントエンド機能のテスト

```bash
# モジュールファイルのコンパイル確認
npx tsc modules/DOMUtils.ts
npx tsc modules/FormHandler.ts
npx tsc modules/UIComponents.ts
npx tsc modules/FrontendApp.ts

# 生成されたJavaScriptファイルの確認
ls -la modules/*.js
```

### ステップ5: ブラウザでの動作確認

```bash
# ローカルサーバーの起動
npx http-server . -p 3000

# 別のターミナルでブラウザを開く（macOS）
open http://localhost:3000/frontend-demo.html

# 別のターミナルでブラウザを開く（Windows）
start http://localhost:3000/frontend-demo.html

# 別のターミナルでブラウザを開く（Linux）
xdg-open http://localhost:3000/frontend-demo.html
```

---

## 🔧 各モジュールの詳細使用法

### 1. DOMUtils.ts - DOM操作ユーティリティ

#### 基本的な使用方法

```typescript
import { DOMUtils } from './modules/DOMUtils.js';

// 要素の取得
const buttonResult = DOMUtils.querySelector<HTMLButtonElement>('#submit-btn');
if (buttonResult.success) {
    const button = buttonResult.data;
    console.log('ボタンが見つかりました:', button.textContent);
} else {
    console.error('ボタンが見つかりません:', buttonResult.error);
}

// 要素の作成
const divResult = DOMUtils.createElement('div', {
    id: 'my-div',
    className: 'custom-div',
    textContent: 'Hello World',
    styles: {
        backgroundColor: '#f0f0f0',
        padding: '1rem',
        borderRadius: '4px'
    }
});

if (divResult.success) {
    document.body.appendChild(divResult.data);
}
```

#### 高度な使用例

```typescript
// イベントリスナーの追加
const inputResult = DOMUtils.querySelector<HTMLInputElement>('#name-input');
if (inputResult.success) {
    DOMUtils.addEventListener(
        inputResult.data,
        'input',
        (event) => {
            const target = event.target as HTMLInputElement;
            console.log('入力値:', target.value);
        }
    );
}

// 複数要素の取得と操作
const buttonsResult = DOMUtils.querySelectorAll<HTMLButtonElement>('.btn');
if (buttonsResult.success) {
    buttonsResult.data.forEach((button, index) => {
        DOMUtils.addEventListener(button, 'click', () => {
            console.log(`ボタン ${index + 1} がクリックされました`);
        });
    });
}
```

#### カスタマイズ例

```typescript
// カスタムボタンの作成
function createCustomButton(text: string, onClick: () => void) {
    const buttonResult = DOMUtils.createElement('button', {
        textContent: text,
        className: 'btn btn-primary',
        eventListeners: [
            {
                event: 'click',
                handler: onClick
            }
        ],
        styles: {
            padding: '0.75rem 1.5rem',
            backgroundColor: '#007bff',
            color: 'white',
            border: 'none',
            borderRadius: '4px',
            cursor: 'pointer'
        }
    });

    return buttonResult.success ? buttonResult.data : null;
}

// 使用例
const customButton = createCustomButton('カスタムボタン', () => {
    alert('カスタムボタンがクリックされました！');
});

if (customButton) {
    document.body.appendChild(customButton);
}
```

### 2. FormHandler.ts - フォーム処理・バリデーション

#### 基本的なフォーム設定

```typescript
import { FormHandler, ValidationRules } from './modules/FormHandler.js';

// フィールド設定の定義
const formFields = [
    {
        name: 'username',
        label: 'ユーザー名',
        type: 'text' as const,
        required: true,
        validationRules: [
            ValidationRules.required('ユーザー名'),
            ValidationRules.minLength(3, 'ユーザー名'),
            ValidationRules.maxLength(20, 'ユーザー名')
        ],
        realTimeValidation: true
    },
    {
        name: 'email',
        label: 'メールアドレス',
        type: 'email' as const,
        required: true,
        validationRules: [
            ValidationRules.required('メールアドレス'),
            ValidationRules.email('メールアドレス')
        ],
        realTimeValidation: true
    }
];

// フォームハンドラーの作成
const formHandler = new FormHandler(
    '#user-form',
    formFields,
    async (formData) => {
        // 送信処理のカスタム実装
        console.log('送信データ:', formData);
        
        // APIコールのシミュレーション
        await new Promise(resolve => setTimeout(resolve, 1000));
        
        // 成功時はtrue、失敗時はfalseを返す
        return true;
    },
    '.form-errors'
);
```

#### カスタムバリデーションルールの作成

```typescript
// カスタムバリデーションルールの例
const customValidationRules = {
    // 日本の郵便番号チェック
    japanesePostalCode: (fieldName: string) => (value: string) => {
        const postalCodeRegex = /^\d{3}-\d{4}$/;
        return {
            isValid: postalCodeRegex.test(value),
            message: postalCodeRegex.test(value) 
                ? '' 
                : `${fieldName}は「123-4567」の形式で入力してください`,
            field: fieldName
        };
    },

    // 日本の電話番号チェック
    japanesePhoneNumber: (fieldName: string) => (value: string) => {
        const phoneRegex = /^0\d{1,4}-\d{1,4}-\d{4}$/;
        return {
            isValid: phoneRegex.test(value),
            message: phoneRegex.test(value) 
                ? '' 
                : `${fieldName}は「090-1234-5678」の形式で入力してください`,
            field: fieldName
        };
    },

    // パスワード確認
    passwordConfirmation: (originalPasswordFieldName: string, fieldName: string) => (value: string) => {
        const originalPasswordElement = document.querySelector<HTMLInputElement>(`[name="${originalPasswordFieldName}"]`);
        const originalPassword = originalPasswordElement?.value || '';
        
        return {
            isValid: value === originalPassword,
            message: value === originalPassword 
                ? '' 
                : `${fieldName}が一致しません`,
            field: fieldName
        };
    }
};

// カスタムルールの使用例
const advancedFormFields = [
    {
        name: 'postalCode',
        label: '郵便番号',
        type: 'text' as const,
        required: true,
        validationRules: [
            ValidationRules.required('郵便番号'),
            customValidationRules.japanesePostalCode('郵便番号')
        ],
        realTimeValidation: true
    },
    {
        name: 'phone',
        label: '電話番号',
        type: 'tel' as const,
        required: false,
        validationRules: [
            customValidationRules.japanesePhoneNumber('電話番号')
        ],
        realTimeValidation: true
    }
];
```

#### フォーム送信の高度な処理

```typescript
// 高度なフォーム送信処理の例
async function advancedFormSubmit(formData: Record<string, string>): Promise<boolean> {
    try {
        // 1. ローディング状態の表示
        showLoadingSpinner();

        // 2. データの前処理
        const processedData = {
            ...formData,
            timestamp: new Date().toISOString(),
            userAgent: navigator.userAgent
        };

        // 3. APIエンドポイントへの送信
        const response = await fetch('/api/users', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            },
            body: JSON.stringify(processedData)
        });

        // 4. レスポンスの処理
        if (!response.ok) {
            throw new Error(`HTTP Error: ${response.status} ${response.statusText}`);
        }

        const result = await response.json();
        
        // 5. 成功時の処理
        showSuccessMessage('ユーザー登録が完了しました！');
        
        // 6. 追加の処理（アナリティクス送信など）
        trackUserRegistration(result.userId);

        return true;

    } catch (error) {
        console.error('フォーム送信エラー:', error);
        
        // エラーの種類に応じた処理
        if (error instanceof TypeError) {
            showErrorMessage('ネットワークエラーが発生しました。インターネット接続を確認してください。');
        } else if (error instanceof Error) {
            showErrorMessage(`送信エラー: ${error.message}`);
        } else {
            showErrorMessage('予期しないエラーが発生しました。');
        }

        return false;

    } finally {
        // 7. クリーンアップ処理
        hideLoadingSpinner();
    }
}

// ヘルパー関数
function showLoadingSpinner() {
    const spinner = document.querySelector('.loading-spinner');
    if (spinner) {
        spinner.style.display = 'block';
    }
}

function hideLoadingSpinner() {
    const spinner = document.querySelector('.loading-spinner');
    if (spinner) {
        spinner.style.display = 'none';
    }
}

function showSuccessMessage(message: string) {
    const alertDiv = document.createElement('div');
    alertDiv.className = 'alert alert-success';
    alertDiv.textContent = message;
    document.body.appendChild(alertDiv);
    
    setTimeout(() => alertDiv.remove(), 5000);
}

function showErrorMessage(message: string) {
    const alertDiv = document.createElement('div');
    alertDiv.className = 'alert alert-error';
    alertDiv.textContent = message;
    document.body.appendChild(alertDiv);
}

function trackUserRegistration(userId: string) {
    // アナリティクスサービスへの送信例
    if (typeof gtag !== 'undefined') {
        gtag('event', 'user_registration', {
            user_id: userId,
            timestamp: Date.now()
        });
    }
}
```

### 3. UIComponents.ts - UIコンポーネント

#### Card コンポーネントの使用

```typescript
import { Card } from './modules/UIComponents.js';

// 基本的なカードの作成
const basicCard = new Card({
    title: 'サンプルカード',
    content: 'これはサンプルのカードコンテンツです。',
    actions: [
        {
            text: '詳細を見る',
            onClick: () => console.log('詳細ボタンがクリックされました'),
            variant: 'primary'
        },
        {
            text: '削除',
            onClick: () => console.log('削除ボタンがクリックされました'),
            variant: 'danger'
        }
    ]
});

// カードをDOMに追加
document.getElementById('card-container')?.appendChild(basicCard.getElement());

// 画像付きカードの作成
const imageCard = new Card({
    title: '商品カード',
    content: `
        <div class="product-info">
            <p class="price">¥2,980</p>
            <p class="description">高品質なTypeScript学習教材です。</p>
            <div class="rating">
                <span class="stars">★★★★★</span>
                <span class="count">(128件のレビュー)</span>
            </div>
        </div>
    `,
    imageUrl: 'https://example.com/product-image.jpg',
    actions: [
        {
            text: 'カートに追加',
            onClick: () => addToCart('product-123'),
            variant: 'primary'
        },
        {
            text: 'お気に入り',
            onClick: () => toggleFavorite('product-123'),
            variant: 'secondary'
        }
    ],
    className: 'product-card'
});

// カードの動的更新
function updateCardContent(card: Card, newContent: string) {
    card.updateContent(newContent);
}

// カードのアニメーション付き表示
function showCardWithAnimation(card: Card, container: HTMLElement) {
    const cardElement = card.getElement();
    cardElement.style.opacity = '0';
    cardElement.style.transform = 'translateY(20px)';
    
    container.appendChild(cardElement);
    
    // アニメーション
    setTimeout(() => {
        cardElement.style.transition = 'all 0.3s ease-out';
        cardElement.style.opacity = '1';
        cardElement.style.transform = 'translateY(0)';
    }, 10);
}
```

#### Modal コンポーネントの使用

```typescript
import { Modal } from './modules/UIComponents.js';

// 基本的なモーダルの作成
const confirmModal = new Modal({
    title: '削除確認',
    content: 'この項目を削除してもよろしいですか？この操作は取り消せません。',
    actions: [
        {
            text: 'キャンセル',
            onClick: () => confirmModal.close(),
            variant: 'secondary'
        },
        {
            text: '削除する',
            onClick: () => {
                performDelete();
                confirmModal.close();
            },
            variant: 'danger'
        }
    ]
});

// フォーム付きモーダルの作成
const formModal = new Modal({
    title: '新規項目の追加',
    content: `
        <form id="modal-form">
            <div class="form-group">
                <label for="modal-name">名前:</label>
                <input type="text" id="modal-name" name="name" required>
            </div>
            <div class="form-group">
                <label for="modal-description">説明:</label>
                <textarea id="modal-description" name="description" rows="3"></textarea>
            </div>
        </form>
    `,
    actions: [
        {
            text: 'キャンセル',
            onClick: () => formModal.close(),
            variant: 'secondary'
        },
        {
            text: '追加',
            onClick: () => {
                const form = document.getElementById('modal-form') as HTMLFormElement;
                const formData = new FormData(form);
                const data = Object.fromEntries(formData);
                
                if (validateModalForm(data)) {
                    addNewItem(data);
                    formModal.close();
                }
            },
            variant: 'primary'
        }
    ],
    closeOnBackdropClick: false,
    closeOnEscape: true
});

// モーダルの表示
function showConfirmDialog(message: string, onConfirm: () => void) {
    const modal = new Modal({
        title: '確認',
        content: message,
        actions: [
            {
                text: 'キャンセル',
                onClick: () => modal.close(),
                variant: 'secondary'
            },
            {
                text: 'OK',
                onClick: () => {
                    onConfirm();
                    modal.close();
                },
                variant: 'primary'
            }
        ]
    });
    
    modal.show();
    return modal;
}

// 使用例
showConfirmDialog('設定を保存しますか？', () => {
    saveSettings();
    console.log('設定が保存されました');
});
```

#### Tooltip コンポーネントの使用

```typescript
import { Tooltip } from './modules/UIComponents.js';

// 基本的なツールチップの作成
const helpButton = document.getElementById('help-button') as HTMLElement;
const helpTooltip = new Tooltip(helpButton, {
    text: 'このボタンをクリックするとヘルプが表示されます',
    position: 'top',
    trigger: 'hover',
    delay: 500
});

// 動的なツールチップテキスト
const dynamicButton = document.getElementById('dynamic-button') as HTMLElement;
const dynamicTooltip = new Tooltip(dynamicButton, {
    text: '読み込み中...',
    position: 'bottom',
    trigger: 'hover'
});

// ツールチップテキストの動的更新
async function updateTooltipWithData() {
    try {
        const data = await fetchSomeData();
        dynamicTooltip.updateText(`最終更新: ${data.lastUpdated}`);
    } catch (error) {
        dynamicTooltip.updateText('データの取得に失敗しました');
    }
}

// 複数要素へのツールチップ一括適用
function addTooltipsToElements(selector: string, tooltipConfig: any) {
    const elements = document.querySelectorAll(selector);
    const tooltips: Tooltip[] = [];
    
    elements.forEach((element, index) => {
        const tooltip = new Tooltip(element as HTMLElement, {
            ...tooltipConfig,
            text: `${tooltipConfig.text} (${index + 1})`
        });
        tooltips.push(tooltip);
    });
    
    return tooltips;
}

// 使用例
const buttonTooltips = addTooltipsToElements('.action-button', {
    text: 'アクションボタン',
    position: 'top',
    trigger: 'hover',
    delay: 300
});
```

### 4. FrontendApp.ts - メインアプリケーション

#### 基本的なアプリケーション設定

```typescript
import { FrontendApp } from './modules/FrontendApp.js';

// アプリケーション設定
const appConfig = {
    containerSelector: '#app-container',
    apiEndpoint: 'https://api.example.com/users',
    enableDebugMode: true,
    animationDuration: 300
};

// アプリケーションの初期化
const app = new FrontendApp(appConfig);

async function startApplication() {
    try {
        await app.initialize();
        console.log('アプリケーションが正常に起動しました');
    } catch (error) {
        console.error('アプリケーションの起動に失敗しました:', error);
    }
}

startApplication();
```

#### カスタム設定での使用

```typescript
// 本番環境用の設定
const productionConfig = {
    containerSelector: '#main-app',
    apiEndpoint: 'https://api.mysite.com/v1/users',
    enableDebugMode: false,
    animationDuration: 200
};

// 開発環境用の設定
const developmentConfig = {
    containerSelector: '#dev-app',
    apiEndpoint: 'http://localhost:3001/api/users',
    enableDebugMode: true,
    animationDuration: 500
};

// 環境に応じた設定の選択
const config = process.env.NODE_ENV === 'production' 
    ? productionConfig 
    : developmentConfig;

const app = new FrontendApp(config);
```

---

## 🎨 カスタマイズ方法

### スタイルのカスタマイズ

#### CSSカスタムプロパティの活用

```css
/* カスタムテーマの定義 */
:root {
    /* カラーパレット */
    --primary-color: #667eea;
    --secondary-color: #764ba2;
    --success-color: #28a745;
    --danger-color: #dc3545;
    --warning-color: #ffc107;
    --info-color: #17a2b8;
    
    /* フォント */
    --font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    --font-size-base: 1rem;
    --font-size-large: 1.25rem;
    --font-size-small: 0.875rem;
    
    /* スペーシング */
    --spacing-xs: 0.25rem;
    --spacing-sm: 0.5rem;
    --spacing-md: 1rem;
    --spacing-lg: 1.5rem;
    --spacing-xl: 2rem;
    
    /* ボーダー */
    --border-radius: 6px;
    --border-width: 1px;
    --border-color: #e1e5e9;
    
    /* シャドウ */
    --shadow-sm: 0 2px 4px rgba(0, 0, 0, 0.1);
    --shadow-md: 0 4px 6px rgba(0, 0, 0, 0.1);
    --shadow-lg: 0 8px 25px rgba(0, 0, 0, 0.15);
}

/* ダークテーマ */
[data-theme="dark"] {
    --primary-color: #4f46e5;
    --background-color: #1a1a1a;
    --text-color: #ffffff;
    --border-color: #333333;
}

/* カスタムボタンスタイル */
.btn-custom {
    background: linear-gradient(135deg, var(--primary-color), var(--secondary-color));
    color: white;
    border: none;
    padding: var(--spacing-sm) var(--spacing-md);
    border-radius: var(--border-radius);
    font-family: var(--font-family);
    font-size: var(--font-size-base);
    cursor: pointer;
    transition: all 0.3s ease;
}

.btn-custom:hover {
    transform: translateY(-1px);
    box-shadow: var(--shadow-lg);
}
```

#### レスポンシブデザインの拡張

```css
/* ブレークポイントの定義 */
:root {
    --breakpoint-sm: 576px;
    --breakpoint-md: 768px;
    --breakpoint-lg: 992px;
    --breakpoint-xl: 1200px;
}

/* モバイルファーストのレスポンシブデザイン */
.container {
    width: 100%;
    padding: var(--spacing-md);
    margin: 0 auto;
}

@media (min-width: 576px) {
    .container {
        max-width: 540px;
    }
}

@media (min-width: 768px) {
    .container {
        max-width: 720px;
        padding: var(--spacing-lg);
    }
}

@media (min-width: 992px) {
    .container {
        max-width: 960px;
    }
}

@media (min-width: 1200px) {
    .container {
        max-width: 1140px;
        padding: var(--spacing-xl);
    }
}

/* フレックスボックスユーティリティ */
.d-flex { display: flex; }
.flex-column { flex-direction: column; }
.justify-content-center { justify-content: center; }
.align-items-center { align-items: center; }
.flex-wrap { flex-wrap: wrap; }

/* グリッドユーティリティ */
.grid {
    display: grid;
    gap: var(--spacing-md);
}

.grid-cols-1 { grid-template-columns: repeat(1, 1fr); }
.grid-cols-2 { grid-template-columns: repeat(2, 1fr); }
.grid-cols-3 { grid-template-columns: repeat(3, 1fr); }

@media (max-width: 768px) {
    .grid-cols-2,
    .grid-cols-3 {
        grid-template-columns: 1fr;
    }
}
```

### 機能のカスタマイズ

#### カスタムバリデーションルールの追加

```typescript
// バリデーションルールの拡張
export class ExtendedValidationRules extends ValidationRules {
    /**
     * 日本語文字チェック
     */
    static japanese(fieldName: string): ValidationRule {
        return (value: string): ValidationResult => {
            const japaneseRegex = /^[ひらがなカタカナ漢字ー\s]*$/;
            const isValid = japaneseRegex.test(value);
            return {
                isValid,
                message: isValid ? '' : `${fieldName}は日本語で入力してください`,
                field: fieldName
            };
        };
    }

    /**
     * 年齢範囲チェック（より詳細）
     */
    static ageRange(fieldName: string, minAge: number = 0, maxAge: number = 150): ValidationRule {
        return (value: string): ValidationResult => {
            const age = parseInt(value, 10);
            
            if (isNaN(age)) {
                return {
                    isValid: false,
                    message: `${fieldName}は数値で入力してください`,
                    field: fieldName
                };
            }

            if (age < minAge) {
                return {
                    isValid: false,
                    message: `${fieldName}は${minAge}歳以上で入力してください`,
                    field: fieldName
                };
            }

            if (age > maxAge) {
                return {
                    isValid: false,
                    message: `${fieldName}は${maxAge}歳以下で入力してください`,
                    field: fieldName
                };
            }

            // 年齢に応じた追加チェック
            if (age < 18) {
                console.warn('未成年者の登録です');
            }

            return {
                isValid: true,
                message: '',
                field: fieldName
            };
        };
    }

    /**
     * ファイルサイズチェック
     */
    static fileSize(fieldName: string, maxSizeMB: number): ValidationRule {
        return (value: string): ValidationResult => {
            const fileInput = document.querySelector<HTMLInputElement>(`[name="${fieldName}"]`);
            const file = fileInput?.files?.[0];
            
            if (!file) {
                return {
                    isValid: true,
                    message: '',
                    field: fieldName
                };
            }

            const maxSizeBytes = maxSizeMB * 1024 * 1024;
            const isValid = file.size <= maxSizeBytes;

            return {
                isValid,
                message: isValid 
                    ? '' 
                    : `${fieldName}のファイルサイズは${maxSizeMB}MB以下にしてください`,
                field: fieldName
            };
        };
    }

    /**
     * ファイル形式チェック
     */
    static fileType(fieldName: string, allowedTypes: string[]): ValidationRule {
        return (value: string): ValidationResult => {
            const fileInput = document.querySelector<HTMLInputElement>(`[name="${fieldName}"]`);
            const file = fileInput?.files?.[0];
            
            if (!file) {
                return {
                    isValid: true,
                    message: '',
                    field: fieldName
                };
            }

            const fileExtension = file.name.split('.').pop()?.toLowerCase() || '';
            const isValid = allowedTypes.includes(fileExtension);

            return {
                isValid,
                message: isValid 
                    ? '' 
                    : `${fieldName}は${allowedTypes.join(', ')}形式のファイルのみアップロード可能です`,
                field: fieldName
            };
        };
    }
}
```

#### カスタムUIコンポーネントの作成

```typescript
import { BaseComponent } from './modules/UIComponents.js';

/**
 * プログレスバーコンポーネント
 */
export class ProgressBar extends BaseComponent {
    private progress: number = 0;
    private label: string = '';

    constructor(initialProgress: number = 0, label: string = '') {
        const progressElement = document.createElement('div');
        progressElement.className = 'progress-bar-container';
        
        super(progressElement);
        this.progress = Math.max(0, Math.min(100, initialProgress));
        this.label = label;
        
        this.buildProgressBar();
    }

    private buildProgressBar(): void {
        this.element.innerHTML = `
            <div class="progress-bar-label">${this.label}</div>
            <div class="progress-bar-track">
                <div class="progress-bar-fill" style="width: ${this.progress}%"></div>
            </div>
            <div class="progress-bar-text">${this.progress}%</div>
        `;
    }

    /**
     * プログレス値を更新
     */
    public setProgress(value: number, animated: boolean = true): void {
        this.progress = Math.max(0, Math.min(100, value));
        
        const fillElement = this.element.querySelector('.progress-bar-fill') as HTMLElement;
        const textElement = this.element.querySelector('.progress-bar-text') as HTMLElement;
        
        if (fillElement && textElement) {
            if (animated) {
                fillElement.style.transition = 'width 0.3s ease-out';
            }
            
            fillElement.style.width = `${this.progress}%`;
            textElement.textContent = `${Math.round(this.progress)}%`;
        }
    }

    /**
     * ラベルを更新
     */
    public setLabel(label: string): void {
        this.label = label;
        const labelElement = this.element.querySelector('.progress-bar-label') as HTMLElement;
        if (labelElement) {
            labelElement.textContent = label;
        }
    }

    /**
     * アニメーション付きでプログレスを増加
     */
    public async animateToProgress(targetProgress: number, duration: number = 1000): Promise<void> {
        return new Promise((resolve) => {
            const startProgress = this.progress;
            const progressDiff = targetProgress - startProgress;
            const startTime = Date.now();

            const animate = () => {
                const elapsed = Date.now() - startTime;
                const progress = Math.min(elapsed / duration, 1);
                
                const currentProgress = startProgress + (progressDiff * progress);
                this.setProgress(currentProgress, false);

                if (progress < 1) {
                    requestAnimationFrame(animate);
                } else {
                    resolve();
                }
            };

            requestAnimationFrame(animate);
        });
    }
}

/**
 * 通知コンポーネント
 */
export class Notification extends BaseComponent {
    private timeout: number | null = null;

    constructor(
        message: string, 
        type: 'success' | 'error' | 'warning' | 'info' = 'info',
        duration: number = 5000
    ) {
        const notificationElement = document.createElement('div');
        notificationElement.className = `notification notification-${type}`;
        
        super(notificationElement);
        
        this.buildNotification(message, type);
        this.setupAutoHide(duration);
    }

    private buildNotification(message: string, type: string): void {
        const icons = {
            success: '✓',
            error: '✗',
            warning: '⚠',
            info: 'ℹ'
        };

        this.element.innerHTML = `
            <div class="notification-icon">${icons[type as keyof typeof icons]}</div>
            <div class="notification-message">${message}</div>
            <button class="notification-close" aria-label="閉じる">×</button>
        `;

        // 閉じるボタンのイベントリスナー
        const closeButton = this.element.querySelector('.notification-close') as HTMLButtonElement;
        closeButton?.addEventListener('click', () => this.hide());
    }

    private setupAutoHide(duration: number): void {
        if (duration > 0) {
            this.timeout = window.setTimeout(() => {
                this.hide();
            }, duration);
        }
    }

    public show(): void {
        super.show();
        document.body.appendChild(this.element);
        
        // アニメーション
        this.element.style.transform = 'translateX(100%)';
        this.element.style.transition = 'transform 0.3s ease-out';
        
        setTimeout(() => {
            this.element.style.transform = 'translateX(0)';
        }, 10);
    }

    public hide(): void {
        this.element.style.transform = 'translateX(100%)';
        
        setTimeout(() => {
            super.hide();
            this.element.remove();
        }, 300);

        if (this.timeout) {
            clearTimeout(this.timeout);
            this.timeout = null;
        }
    }

    public override destroy(): void {
        if (this.timeout) {
            clearTimeout(this.timeout);
        }
        super.destroy();
    }
}

// 使用例
const progressBar = new ProgressBar(0, 'ファイルアップロード中...');
document.body.appendChild(progressBar.getElement());

// プログレスバーのアニメーション
progressBar.animateToProgress(100, 2000).then(() => {
    const notification = new Notification('アップロードが完了しました！', 'success');
    notification.show();
});
```

---

## 🔍 デバッグ方法

### ブラウザ開発者ツールの活用

#### 1. コンソールでのデバッグ

```typescript
// デバッグ用の関数を追加
class DebugUtils {
    static log(message: string, data?: any): void {
        if (process.env.NODE_ENV !== 'production') {
            console.log(`[DEBUG] ${new Date().toISOString()}: ${message}`, data);
        }
    }

    static error(message: string, error?: any): void {
        console.error(`[ERROR] ${new Date().toISOString()}: ${message}`, error);
    }

    static warn(message: string, data?: any): void {
        console.warn(`[WARN] ${new Date().toISOString()}: ${message}`, data);
    }

    static table(data: any): void {
        if (process.env.NODE_ENV !== 'production') {
            console.table(data);
        }
    }
}

// 使用例
DebugUtils.log('フォーム送信開始', formData);
DebugUtils.error('API呼び出しエラー', error);
DebugUtils.table(userList);
```

#### 2. パフォーマンス測定

```typescript
class PerformanceTracker {
    private static markers: Map<string, number> = new Map();

    static start(label: string): void {
        this.markers.set(label, performance.now());
        console.time(label);
    }

    static end(label: string): number {
        const startTime = this.markers.get(label);
        if (startTime) {
            const duration = performance.now() - startTime;
            console.timeEnd(label);
            this.markers.delete(label);
            return duration;
        }
        return 0;
    }

    static measure(label: string, fn: () => void): number {
        this.start(label);
        fn();
        return this.end(label);
    }

    static async measureAsync(label: string, fn: () => Promise<void>): Promise<number> {
        this.start(label);
        await fn();
        return this.end(label);
    }
}

// 使用例
PerformanceTracker.start('form-validation');
const isValid = validateForm();
const validationTime = PerformanceTracker.end('form-validation');
console.log(`バリデーション時間: ${validationTime.toFixed(2)}ms`);
```

#### 3. メモリ使用量の監視

```typescript
class MemoryMonitor {
    static logMemoryUsage(): void {
        if ('memory' in performance) {
            const memory = (performance as any).memory;
            console.log('メモリ使用量:', {
                used: `${(memory.usedJSHeapSize / 1024 / 1024).toFixed(2)} MB`,
                total: `${(memory.totalJSHeapSize / 1024 / 1024).toFixed(2)} MB`,
                limit: `${(memory.jsHeapSizeLimit / 1024 / 1024).toFixed(2)} MB`
            });
        }
    }

    static startMemoryMonitoring(interval: number = 5000): number {
        return window.setInterval(() => {
            this.logMemoryUsage();
        }, interval);
    }

    static stopMemoryMonitoring(intervalId: number): void {
        clearInterval(intervalId);
    }
}

// 使用例
const memoryIntervalId = MemoryMonitor.startMemoryMonitoring();
// アプリケーション終了時
// MemoryMonitor.stopMemoryMonitoring(memoryIntervalId);
```

### TypeScript特有のデバッグ

#### 1. 型情報の確認

```typescript
// 型の確認用ユーティリティ
type TypeCheck<T> = T;

// 使用例：型が期待通りかチェック
type UserType = TypeCheck<{
    id: string;
    name: string;
    email: string;
}>;

// コンパイル時の型チェック
function assertType<T>(_value: T): void {
    // 実行時には何もしないが、コンパイル時に型チェックされる
}

// 使用例
const user = { id: '1', name: 'Test', email: 'test@example.com' };
assertType<UserType>(user); // 型が一致しない場合はコンパイルエラー
```

#### 2. 実行時型チェック

```typescript
// 実行時型ガード
function isString(value: unknown): value is string {
    return typeof value === 'string';
}

function isNumber(value: unknown): value is number {
    return typeof value === 'number' && !isNaN(value);
}

function isValidUser(value: unknown): value is UserProfile {
    return (
        typeof value === 'object' &&
        value !== null &&
        'id' in value &&
        'name' in value &&
        'email' in value &&
        isString((value as any).id) &&
        isString((value as any).name) &&
        isString((value as any).email)
    );
}

// 使用例
function processUserData(data: unknown): void {
    if (isValidUser(data)) {
        // この時点でdataはUserProfile型として扱われる
        console.log(`ユーザー: ${data.name} (${data.email})`);
    } else {
        console.error('無効なユーザーデータ:', data);
    }
}
```

### エラートラッキング

```typescript
// グローバルエラーハンドラー
class ErrorTracker {
    private static errors: Array<{
        message: string;
        stack?: string;
        timestamp: Date;
        url: string;
        userAgent: string;
    }> = [];

    static initialize(): void {
        // 未処理のJavaScriptエラーをキャッチ
        window.addEventListener('error', (event) => {
            this.logError({
                message: event.message,
                stack: event.error?.stack,
                timestamp: new Date(),
                url: window.location.href,
                userAgent: navigator.userAgent
            });
        });

        // 未処理のPromise拒否をキャッチ
        window.addEventListener('unhandledrejection', (event) => {
            this.logError({
                message: `Unhandled Promise Rejection: ${event.reason}`,
                stack: event.reason?.stack,
                timestamp: new Date(),
                url: window.location.href,
                userAgent: navigator.userAgent
            });
        });
    }

    private static logError(error: any): void {
        this.errors.push(error);
        console.error('エラーをキャッチしました:', error);

        // エラーレポートサービスに送信（例：Sentry、LogRocket等）
        this.sendErrorReport(error);
    }

    private static async sendErrorReport(error: any): Promise<void> {
        try {
            // 実際のプロジェクトではエラーレポートサービスのAPIを使用
            await fetch('/api/errors', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(error)
            });
        } catch (reportError) {
            console.error('エラーレポートの送信に失敗:', reportError);
        }
    }

    static getErrors(): any[] {
        return [...this.errors];
    }

    static clearErrors(): void {
        this.errors = [];
    }
}

// アプリケーション開始時に初期化
ErrorTracker.initialize();
```

---

## 🚨 トラブルシューティング

### よくある問題と解決方法

#### 1. TypeScriptコンパイルエラー

**問題**: `Cannot find module './modules/DOMUtils.js'`

**原因**: 
- TypeScriptファイルがコンパイルされていない
- インポートパスが間違っている

**解決方法**:
```bash
# 1. すべてのTypeScriptファイルをコンパイル
npx tsc --build

# 2. 生成されたJavaScriptファイルを確認
ls -la modules/*.js

# 3. インポートパスを確認（.js拡張子が必要）
import { DOMUtils } from './modules/DOMUtils.js'; // ✓ 正しい
import { DOMUtils } from './modules/DOMUtils';    // ✗ 間違い
```

#### 2. ブラウザでのCORSエラー

**問題**: `Access to script blocked by CORS policy`

**原因**: 
- ファイルプロトコル（file://）でHTMLファイルを開いている
- モジュールの読み込みにはHTTPサーバーが必要

**解決方法**:
```bash
# ローカルHTTPサーバーを起動
npx http-server . -p 3000

# または
python -m http.server 3000

# ブラウザでアクセス
# http://localhost:3000/frontend-demo.html
```

#### 3. モジュールが見つからないエラー

**問題**: `Module not found: Can't resolve './modules/FormHandler'`

**解決方法**:
```bash
# 1. ファイルの存在確認
ls -la typescript/modules/

# 2. TypeScriptコンパイル
cd typescript
npx tsc --build

# 3. 生成されたJavaScriptファイルの確認
ls -la modules/*.js

# 4. インポート文の確認
# TypeScriptファイル内で .js 拡張子を使用
import { FormHandler } from './modules/FormHandler.js';
```

#### 4. DOM要素が見つからないエラー

**問題**: `Cannot read property 'addEventListener' of null`

**原因**:
- DOM要素がまだ読み込まれていない
- セレクタが間違っている

**解決方法**:
```typescript
// 1. DOMContentLoadedイベントを待つ
document.addEventListener('DOMContentLoaded', () => {
    // DOM操作のコードをここに記述
    initializeApp();
});

// 2. 要素の存在確認
const element = document.getElementById('my-element');
if (element) {
    element.addEventListener('click', handleClick);
} else {
    console.error('要素が見つかりません: #my-element');
}

// 3. DOMUtilsを使用した安全な要素取得
const elementResult = DOMUtils.querySelector('#my-element');
if (elementResult.success) {
    // 要素が見つかった場合の処理
    const element = elementResult.data;
} else {
    // 要素が見つからない場合の処理
    console.error('要素取得エラー:', elementResult.error);
}
```

#### 5. フォームバリデーションが動作しない

**問題**: バリデーションが実行されない

**原因**:
- HTMLフォーム構造が期待と異なる
- エラー表示要素が存在しない

**解決方法**:
```html
<!-- 正しいHTMLフォーム構造 -->
<form id="user-form">
    <div class="form-group">
        <label for="name">名前</label>
        <input type="text" id="name" name="name" required>
        <div class="form-error" data-error="name"></div>
    </div>
    
    <div class="form-group">
        <label for="email">メール</label>
        <input type="email" id="email" name="email" required>
        <div class="form-error" data-error="email"></div>
    </div>
    
    <button type="submit">送信</button>
</form>

<!-- エラー表示コンテナ -->
<div class="error-container"></div>
```

```typescript
// FormHandlerの正しい初期化
const formHandler = new FormHandler(
    '#user-form',           // フォームセレクタ
    formFields,             // フィールド設定
    handleFormSubmit,       // 送信処理関数
    '.error-container'      // エラーコンテナセレクタ
);
```

#### 6. パフォーマンスの問題

**問題**: アプリケーションが重い、反応が遅い

**解決方法**:

```typescript
// 1. イベントリスナーの適切な管理
class ComponentManager {
    private components: BaseComponent[] = [];
    
    addComponent(component: BaseComponent): void {
        this.components.push(component);
    }
    
    // アプリケーション終了時にすべてのコンポーネントを破棄
    destroyAll(): void {
        this.components.forEach(component => {
            component.destroy();
        });
        this.components = [];
    }
}

// 2. デバウンス処理の実装
function debounce<T extends (...args: any[]) => void>(
    func: T,
    delay: number
): (...args: Parameters<T>) => void {
    let timeoutId: number;
    
    return (...args: Parameters<T>) => {
        clearTimeout(timeoutId);
        timeoutId = window.setTimeout(() => func(...args), delay);
    };
}

// 使用例：検索入力のデバウンス
const debouncedSearch = debounce((query: string) => {
    performSearch(query);
}, 300);

searchInput.addEventListener('input', (event) => {
    const target = event.target as HTMLInputElement;
    debouncedSearch(target.value);
});

// 3. 仮想スクロールの実装（大量データの場合）
class VirtualList {
    private container: HTMLElement;
    private items: any[];
    private itemHeight: number;
    private visibleCount: number;
    
    constructor(container: HTMLElement, items: any[], itemHeight: number) {
        this.container = container;
        this.items = items;
        this.itemHeight = itemHeight;
        this.visibleCount = Math.ceil(container.clientHeight / itemHeight) + 2;
        
        this.setupScrollListener();
        this.render();
    }
    
    private setupScrollListener(): void {
        this.container.addEventListener('scroll', () => {
            this.render();
        });
    }
    
    private render(): void {
        const scrollTop = this.container.scrollTop;
        const startIndex = Math.floor(scrollTop / this.itemHeight);
        const endIndex = Math.min(startIndex + this.visibleCount, this.items.length);
        
        // 表示する項目のみをレンダリング
        this.renderItems(startIndex, endIndex);
    }
    
    private renderItems(startIndex: number, endIndex: number): void {
        // 実装は省略
    }
}
```

### デバッグチェックリスト

#### 開発時のチェック項目

- [ ] **TypeScriptコンパイル**: エラーなくコンパイルできるか
- [ ] **ブラウザコンソール**: エラーメッセージが出ていないか
- [ ] **ネットワークタブ**: リソースが正しく読み込まれているか
- [ ] **DOM構造**: 期待するHTML構造になっているか
- [ ] **CSS適用**: スタイルが正しく適用されているか
- [ ] **イベントリスナー**: イベントが正しく登録されているか
- [ ] **メモリリーク**: 不要なイベントリスナーが残っていないか

#### 本番環境デプロイ前のチェック項目

- [ ] **ビルド最適化**: プロダクションビルドが正常に完了するか
- [ ] **ファイルサイズ**: バンドルサイズが適切か
- [ ] **ブラウザ互換性**: ターゲットブラウザで動作するか
- [ ] **アクセシビリティ**: スクリーンリーダーで操作できるか
- [ ] **パフォーマンス**: Lighthouse スコアが良好か
- [ ] **セキュリティ**: XSS対策が適切に実装されているか
- [ ] **エラーハンドリング**: 予期しないエラーが適切に処理されるか

---

## 🎯 応用例

### 1. REST API との統合

```typescript
// APIクライアントクラス
class ApiClient {
    private baseUrl: string;
    private headers: Record<string, string>;

    constructor(baseUrl: string, headers: Record<string, string> = {}) {
        this.baseUrl = baseUrl;
        this.headers = {
            'Content-Type': 'application/json',
            ...headers
        };
    }

    private async request<T>(
        endpoint: string,
        options: RequestInit = {}
    ): Promise<T> {
        const url = `${this.baseUrl}${endpoint}`;
        const config: RequestInit = {
            ...options,
            headers: {
                ...this.headers,
                ...options.headers
            }
        };

        try {
            const response = await fetch(url, config);
            
            if (!response.ok) {
                throw new Error(`HTTP Error: ${response.status} ${response.statusText}`);
            }

            const data = await response.json();
            return data;
        } catch (error) {
            console.error(`API Request Error: ${endpoint}`, error);
            throw error;
        }
    }

    async get<T>(endpoint: string): Promise<T> {
        return this.request<T>(endpoint, { method: 'GET' });
    }

    async post<T>(endpoint: string, data: any): Promise<T> {
        return this.request<T>(endpoint, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    }

    async put<T>(endpoint: string, data: any): Promise<T> {
        return this.request<T>(endpoint, {
            method: 'PUT',
            body: JSON.stringify(data)
        });
    }

    async delete<T>(endpoint: string): Promise<T> {
        return this.request<T>(endpoint, { method: 'DELETE' });
    }
}

// ユーザーAPIサービス
class UserApiService {
    private api: ApiClient;

    constructor(baseUrl: string, authToken?: string) {
        const headers = authToken ? { 'Authorization': `Bearer ${authToken}` } : {};
        this.api = new ApiClient(baseUrl, headers);
    }

    async getUsers(): Promise<UserProfile[]> {
        return this.api.get<UserProfile[]>('/users');
    }

    async getUser(id: string): Promise<UserProfile> {
        return this.api.get<UserProfile>(`/users/${id}`);
    }

    async createUser(userData: Omit<UserProfile, 'id' | 'createdAt'>): Promise<UserProfile> {
        return this.api.post<UserProfile>('/users', userData);
    }

    async updateUser(id: string, userData: Partial<UserProfile>): Promise<UserProfile> {
        return this.api.put<UserProfile>(`/users/${id}`, userData);
    }

    async deleteUser(id: string): Promise<void> {
        return this.api.delete<void>(`/users/${id}`);
    }
}

// フロントエンドアプリケーションでの使用
class EnhancedFrontendApp extends FrontendApp {
    private userService: UserApiService;

    constructor(config: any) {
        super(config);
        this.userService = new UserApiService(
            config.apiEndpoint,
            localStorage.getItem('authToken') || undefined
        );
    }

    // フォーム送信処理をAPI統合版に変更
    protected async handleFormSubmit(formData: Record<string, string>): Promise<boolean> {
        try {
            const userData = {
                name: formData.name,
                email: formData.email,
                age: parseInt(formData.age, 10),
                bio: formData.bio || ''
            };

            const createdUser = await this.userService.createUser(userData);
            console.log('ユーザー作成成功:', createdUser);

            // UIの更新
            this.addUserCard(createdUser);
            this.updateStatistics();

            return true;
        } catch (error) {
            console.error('ユーザー作成エラー:', error);
            return false;
        }
    }

    // 初期データをAPIから読み込み
    protected async loadInitialData(): Promise<void> {
        try {
            const users = await this.userService.getUsers();
            this.setState({ users });

            users.forEach(user => this.addUserCard(user));
            this.updateStatistics();
        } catch (error) {
            console.error('初期データ読み込みエラー:', error);
            this.displayError('データの読み込みに失敗しました');
        }
    }
}
```

### 2. 状態管理の実装

```typescript
// 状態管理ライブラリの実装
type Listener<T> = (state: T) => void;

class Store<T> {
    private state: T;
    private listeners: Listener<T>[] = [];

    constructor(initialState: T) {
        this.state = { ...initialState };
    }

    getState(): T {
        return { ...this.state };
    }

    setState(newState: Partial<T>): void {
        const prevState = { ...this.state };
        this.state = { ...this.state, ...newState };
        
        this.listeners.forEach(listener => {
            listener(this.state);
        });

        console.log('State updated:', { prevState, newState: this.state });
    }

    subscribe(listener: Listener<T>): () => void {
        this.listeners.push(listener);
        
        // アンサブスクライブ関数を返す
        return () => {
            const index = this.listeners.indexOf(listener);
            if (index > -1) {
                this.listeners.splice(index, 1);
            }
        };
    }
}

// アプリケーション状態の定義
interface AppState {
    users: UserProfile[];
    currentUser: UserProfile | null;
    loading: boolean;
    error: string | null;
    filters: {
        searchQuery: string;
        ageRange: [number, number];
        sortBy: 'name' | 'age' | 'createdAt';
    };
}

// 状態管理を使用したアプリケーション
class StatefulFrontendApp {
    private store: Store<AppState>;
    private unsubscribe: (() => void)[] = [];

    constructor() {
        // 初期状態の設定
        this.store = new Store<AppState>({
            users: [],
            currentUser: null,
            loading: false,
            error: null,
            filters: {
                searchQuery: '',
                ageRange: [0, 100],
                sortBy: 'name'
            }
        });

        this.setupStateSubscriptions();
    }

    private setupStateSubscriptions(): void {
        // ユーザーリストの変更を監視
        const unsubscribeUsers = this.store.subscribe((state) => {
            this.renderUserList(state.users, state.filters);
        });

        // ローディング状態の変更を監視
        const unsubscribeLoading = this.store.subscribe((state) => {
            this.updateLoadingUI(state.loading);
        });

        // エラー状態の変更を監視
        const unsubscribeError = this.store.subscribe((state) => {
            if (state.error) {
                this.displayError(state.error);
            }
        });

        this.unsubscribe.push(unsubscribeUsers, unsubscribeLoading, unsubscribeError);
    }

    // アクション（状態を変更する関数）
    public async loadUsers(): Promise<void> {
        this.store.setState({ loading: true, error: null });

        try {
            // API呼び出しのシミュレーション
            const users = await this.fetchUsers();
            this.store.setState({ users, loading: false });
        } catch (error) {
            this.store.setState({ 
                loading: false, 
                error: error instanceof Error ? error.message : 'Unknown error'
            });
        }
    }

    public addUser(user: UserProfile): void {
        const currentState = this.store.getState();
        const updatedUsers = [...currentState.users, user];
        this.store.setState({ users: updatedUsers });
    }

    public removeUser(userId: string): void {
        const currentState = this.store.getState();
        const updatedUsers = currentState.users.filter(user => user.id !== userId);
        this.store.setState({ users: updatedUsers });
    }

    public setSearchQuery(query: string): void {
        const currentState = this.store.getState();
        this.store.setState({
            filters: {
                ...currentState.filters,
                searchQuery: query
            }
        });
    }

    public setSortBy(sortBy: 'name' | 'age' | 'createdAt'): void {
        const currentState = this.store.getState();
        this.store.setState({
            filters: {
                ...currentState.filters,
                sortBy
            }
        });
    }

    private renderUserList(users: UserProfile[], filters: AppState['filters']): void {
        // フィルタリングとソート
        let filteredUsers = users.filter(user => 
            user.name.toLowerCase().includes(filters.searchQuery.toLowerCase()) &&
            user.age >= filters.ageRange[0] &&
            user.age <= filters.ageRange[1]
        );

        // ソート
        filteredUsers.sort((a, b) => {
            switch (filters.sortBy) {
                case 'name':
                    return a.name.localeCompare(b.name);
                case 'age':
                    return a.age - b.age;
                case 'createdAt':
                    return new Date(a.createdAt).getTime() - new Date(b.createdAt).getTime();
                default:
                    return 0;
            }
        });

        // UI更新
        this.updateUserListUI(filteredUsers);
    }

    private updateUserListUI(users: UserProfile[]): void {
        const container = document.getElementById('users-grid');
        if (!container) return;

        // 既存の要素をクリア
        container.innerHTML = '';

        // ユーザーカードを作成
        users.forEach(user => {
            const card = new Card({
                title: user.name,
                content: `
                    <p>年齢: ${user.age}歳</p>
                    <p>メール: ${user.email}</p>
                    <p>登録日: ${user.createdAt.toLocaleDateString('ja-JP')}</p>
                `,
                actions: [
                    {
                        text: '詳細',
                        onClick: () => this.showUserDetail(user),
                        variant: 'primary'
                    },
                    {
                        text: '削除',
                        onClick: () => this.removeUser(user.id),
                        variant: 'danger'
                    }
                ]
            });

            container.appendChild(card.getElement());
        });
    }

    private updateLoadingUI(loading: boolean): void {
        const loadingElement = document.getElementById('loading-indicator');
        if (loadingElement) {
            loadingElement.style.display = loading ? 'block' : 'none';
        }
    }

    private displayError(error: string): void {
        const errorElement = document.getElementById('error-message');
        if (errorElement) {
            errorElement.textContent = error;
            errorElement.style.display = 'block';
        }
    }

    private async fetchUsers(): Promise<UserProfile[]> {
        // API呼び出しのシミュレーション
        await new Promise(resolve => setTimeout(resolve, 1000));
        
        return [
            {
                id: '1',
                name: '田中太郎',
                email: 'tanaka@example.com',
                age: 28,
                bio: 'エンジニアです',
                createdAt: new Date('2024-01-15')
            },
            {
                id: '2',
                name: '佐藤花子',
                email: 'sato@example.com',
                age: 32,
                bio: 'デザイナーです',
                createdAt: new Date('2024-01-20')
            }
        ];
    }

    private showUserDetail(user: UserProfile): void {
        const modal = new Modal({
            title: `${user.name}さんの詳細`,
            content: `
                <div class="user-detail">
                    <p><strong>ID:</strong> ${user.id}</p>
                    <p><strong>名前:</strong> ${user.name}</p>
                    <p><strong>メール:</strong> ${user.email}</p>
                    <p><strong>年齢:</strong> ${user.age}歳</p>
                    <p><strong>自己紹介:</strong> ${user.bio}</p>
                    <p><strong>登録日:</strong> ${user.createdAt.toLocaleDateString('ja-JP')}</p>
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

    public destroy(): void {
        // 状態監視の解除
        this.unsubscribe.forEach(unsubscribe => unsubscribe());
        this.unsubscribe = [];
    }
}
```

### 3. テストの実装

```typescript
// テストユーティリティ
class TestUtils {
    static createMockElement(tagName: string, attributes: Record<string, string> = {}): HTMLElement {
        const element = document.createElement(tagName);
        Object.entries(attributes).forEach(([key, value]) => {
            element.setAttribute(key, value);
        });
        return element;
    }

    static createMockForm(fields: Array<{ name: string, type: string, value?: string }>): HTMLFormElement {
        const form = document.createElement('form');
        
        fields.forEach(field => {
            const input = document.createElement('input');
            input.type = field.type;
            input.name = field.name;
            input.id = field.name;
            if (field.value) input.value = field.value;

            const errorDiv = document.createElement('div');
            errorDiv.className = 'form-error';
            errorDiv.setAttribute('data-error', field.name);

            form.appendChild(input);
            form.appendChild(errorDiv);
        });

        const submitButton = document.createElement('button');
        submitButton.type = 'submit';
        submitButton.textContent = '送信';
        form.appendChild(submitButton);

        return form;
    }

    static async simulateUserInput(element: HTMLInputElement, value: string): Promise<void> {
        element.value = value;
        element.dispatchEvent(new Event('input', { bubbles: true }));
        element.dispatchEvent(new Event('blur', { bubbles: true }));
        
        // 非同期処理を待つ
        await new Promise(resolve => setTimeout(resolve, 0));
    }

    static async simulateFormSubmit(form: HTMLFormElement): Promise<void> {
        const submitEvent = new Event('submit', { bubbles: true, cancelable: true });
        form.dispatchEvent(submitEvent);
        
        // 非同期処理を待つ
        await new Promise(resolve => setTimeout(resolve, 0));
    }
}

// DOMUtilsのテスト例
describe('DOMUtils', () => {
    beforeEach(() => {
        document.body.innerHTML = '';
    });

    describe('querySelector', () => {
        test('should find existing element', () => {
            const testDiv = TestUtils.createMockElement('div', { id: 'test-div' });
            document.body.appendChild(testDiv);

            const result = DOMUtils.querySelector('#test-div');
            
            expect(result.success).toBe(true);
            if (result.success) {
                expect(result.data).toBe(testDiv);
            }
        });

        test('should return error for non-existing element', () => {
            const result = DOMUtils.querySelector('#non-existing');
            
            expect(result.success).toBe(false);
            if (!result.success) {
                expect(result.error).toContain('要素が見つかりません');
            }
        });

        test('should handle empty selector', () => {
            const result = DOMUtils.querySelector('');
            
            expect(result.success).toBe(false);
            if (!result.success) {
                expect(result.error).toContain('セレクタが空です');
            }
        });
    });

    describe('createElement', () => {
        test('should create element with basic properties', () => {
            const result = DOMUtils.createElement('div', {
                id: 'test-div',
                className: 'test-class',
                textContent: 'Test Content'
            });

            expect(result.success).toBe(true);
            if (result.success) {
                const element = result.data;
                expect(element.tagName).toBe('DIV');
                expect(element.id).toBe('test-div');
                expect(element.className).toBe('test-class');
                expect(element.textContent).toBe('Test Content');
            }
        });

        test('should create element with styles', () => {
            const result = DOMUtils.createElement('div', {
                styles: {
                    backgroundColor: 'red',
                    fontSize: '16px'
                }
            });

            expect(result.success).toBe(true);
            if (result.success) {
                const element = result.data;
                expect(element.style.backgroundColor).toBe('red');
                expect(element.style.fontSize).toBe('16px');
            }
        });
    });
});

// FormHandlerのテスト例
describe('FormHandler', () => {
    let mockForm: HTMLFormElement;
    let mockErrorContainer: HTMLElement;

    beforeEach(() => {
        document.body.innerHTML = '';
        
        mockForm = TestUtils.createMockForm([
            { name: 'name', type: 'text' },
            { name: 'email', type: 'email' }
        ]);
        mockForm.id = 'test-form';
        
        mockErrorContainer = TestUtils.createMockElement('div', { class: 'error-container' });
        
        document.body.appendChild(mockForm);
        document.body.appendChild(mockErrorContainer);
    });

    test('should initialize FormHandler correctly', () => {
        const fields = [
            {
                name: 'name',
                label: '名前',
                type: 'text' as const,
                required: true,
                validationRules: [ValidationRules.required('名前')],
                realTimeValidation: true
            }
        ];

        const mockSubmitCallback = jest.fn().mockResolvedValue(true);

        expect(() => {
            new FormHandler('#test-form', fields, mockSubmitCallback, '.error-container');
        }).not.toThrow();
    });

    test('should validate required field correctly', async () => {
        const fields = [
            {
                name: 'name',
                label: '名前',
                type: 'text' as const,
                required: true,
                validationRules: [ValidationRules.required('名前')],
                realTimeValidation: true
            }
        ];

        const mockSubmitCallback = jest.fn().mockResolvedValue(true);
        const formHandler = new FormHandler('#test-form', fields, mockSubmitCallback, '.error-container');

        const nameInput = document.querySelector('[name="name"]') as HTMLInputElement;
        
        // 空の値でテスト
        await TestUtils.simulateUserInput(nameInput, '');
        
        const errorElement = document.querySelector('[data-error="name"]') as HTMLElement;
        expect(errorElement.textContent).toContain('必須項目です');
        
        // 有効な値でテスト
        await TestUtils.simulateUserInput(nameInput, '田中太郎');
        expect(errorElement.textContent).toBe('');
    });

    test('should handle form submission correctly', async () => {
        const fields = [
            {
                name: 'name',
                label: '名前',
                type: 'text' as const,
                required: true,
                validationRules: [ValidationRules.required('名前')],
                realTimeValidation: true
            }
        ];

        const mockSubmitCallback = jest.fn().mockResolvedValue(true);
        const formHandler = new FormHandler('#test-form', fields, mockSubmitCallback, '.error-container');

        const nameInput = document.querySelector('[name="name"]') as HTMLInputElement;
        await TestUtils.simulateUserInput(nameInput, '田中太郎');

        await TestUtils.simulateFormSubmit(mockForm);

        expect(mockSubmitCallback).toHaveBeenCalledWith({ name: '田中太郎' });
    });
});

// UIコンポーネントのテスト例
describe('Card Component', () => {
    test('should create card with title and content', () => {
        const card = new Card({
            title: 'Test Card',
            content: 'Test Content'
        });

        const cardElement = card.getElement();
        expect(cardElement.querySelector('.card-title')?.textContent).toBe('Test Card');
        expect(cardElement.querySelector('.card-body')?.textContent).toBe('Test Content');
    });

    test('should handle action button clicks', () => {
        const mockOnClick = jest.fn();
        
        const card = new Card({
            title: 'Test Card',
            content: 'Test Content',
            actions: [
                {
                    text: 'Test Action',
                    onClick: mockOnClick,
                    variant: 'primary'
                }
            ]
        });

        const cardElement = card.getElement();
        const actionButton = cardElement.querySelector('.btn') as HTMLButtonElement;
        
        expect(actionButton.textContent).toBe('Test Action');
        
        actionButton.click();
        expect(mockOnClick).toHaveBeenCalled();
    });

    test('should update content dynamically', () => {
        const card = new Card({
            title: 'Test Card',
            content: 'Original Content'
        });

        card.updateContent('Updated Content');

        const cardElement = card.getElement();
        expect(cardElement.querySelector('.card-body')?.textContent).toBe('Updated Content');
    });
});
```

---

この詳細な使い方ガイドにより、初心者から上級者まで、TypeScriptフロントエンドアプリケーションを効果的に活用できるようになります。各セクションは実用的な例とともに、実際の開発現場で役立つ知識を提供しています。

何か特定の部分について、さらに詳しい説明が必要でしたらお知らせください！
