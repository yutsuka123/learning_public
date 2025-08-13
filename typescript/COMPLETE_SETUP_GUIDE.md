# TypeScript フロントエンドアプリケーション 完全セットアップガイド

## 📋 目次

1. [環境要件](#環境要件)
2. [初回セットアップ](#初回セットアップ)
3. [実行方法](#実行方法)
4. [コマンド一覧](#コマンド一覧)
5. [トラブルシューティング](#トラブルシューティング)
6. [ファイル構成](#ファイル構成)
7. [開発環境設定](#開発環境設定)
8. [デプロイ方法](#デプロイ方法)

---

## 🛠️ 環境要件

### 必須ソフトウェア

#### 1. Node.js
- **バージョン**: 18.0.0 以上推奨
- **インストール方法**:
  ```bash
  # バージョン確認
  node --version
  npm --version
  
  # インストールが必要な場合
  # Windows: https://nodejs.org/ からダウンロード
  # macOS: brew install node
  # Linux: sudo apt install nodejs npm
  ```

#### 2. TypeScript
- **バージョン**: 5.0.0 以上推奨
- **インストール方法**:
  ```bash
  # グローバルインストール（推奨）
  npm install -g typescript
  
  # バージョン確認
  tsc --version
  ```

#### 3. ウェブブラウザ
- **推奨**: Chrome, Firefox, Edge の最新版
- **必要機能**: ES2020対応、ES Modules対応

### 推奨ソフトウェア

#### エディタ
- **VS Code** (最も推奨)
- **WebStorm**
- **Sublime Text**

#### VS Code 拡張機能
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

---

## 🚀 初回セットアップ

### ステップ1: プロジェクトの準備

```bash
# プロジェクトディレクトリに移動
cd learning_public

# 依存関係のインストール（初回のみ）
npm install

# TypeScriptがグローバルにインストールされていない場合
npm install -g typescript
```

### ステップ2: 環境確認

```bash
# 必要なファイルが存在するか確認
ls typescript/
# 期待される出力:
# 01_basic_types_practice.ts
# modules/
# frontend-demo.html
# tsconfig.json

# TypeScript設定の確認
cat typescript/tsconfig.json
```

### ステップ3: 初回コンパイル

```bash
# TypeScriptディレクトリに移動
cd typescript

# 全ファイルをコンパイル
npx tsc --build

# 生成されたファイルを確認
ls modules/*.js
# 期待される出力:
# DOMUtils.js, FormHandler.js, FrontendApp.js, UIComponents.js
```

---

## 🎯 実行方法

### 方法1: コンソール実行（基本練習）

```bash
# プロジェクトルートで実行
npm run ts:run-basic

# または手動実行
cd typescript
npx tsc 01_basic_types_practice.ts
node 01_basic_types_practice.js
```

**期待される出力**:
```
TypeScript 基本型システム練習プログラム
==========================================

=== 基本型の練習 ===
文字列: 田中太郎 こんにちは、田中太郎さん！
数値: 25 98.5 255 10
...
```

### 方法2: ブラウザ実行（フル機能）

#### A. ローカルサーバー起動

```bash
# 方法1: http-server（推奨）
cd typescript
npx http-server . -p 3000

# 方法2: Python
python -m http.server 3000

# 方法3: Node.js (18+)
npx serve . -p 3000
```

#### B. ブラウザアクセス

```
http://localhost:3000/frontend-demo.html
```

**期待される画面**:
- ✅ ヘッダー: "TypeScript フロントエンドアプリケーション"
- ✅ ユーザー登録フォーム
- ✅ 登録済みユーザー一覧
- ✅ 統計情報

### 方法3: 開発モード（ファイル監視）

```bash
# ファイル変更を監視してコンパイル
npm run ts:watch

# 別のターミナルでサーバー起動
npx http-server typescript -p 3000
```

---

## 📝 コマンド一覧

### npm スクリプト

```bash
# ヘルプ表示
npm run ts:help

# 基本練習実行
npm run ts:run-basic

# Hello World実行  
npm run ts:run-hello

# 全ファイルコンパイル
npm run ts:build-all

# ファイル監視コンパイル
npm run ts:watch

# 生成ファイル削除
npm run ts:clean
```

### TypeScript コンパイル

```bash
cd typescript

# 全ファイルコンパイル
npx tsc --build

# 特定ファイルコンパイル
npx tsc 01_basic_types_practice.ts

# エラーチェックのみ（ファイル生成なし）
npx tsc --noEmit

# 監視モード
npx tsc --watch

# 強制再コンパイル
npx tsc --build --force
```

### ローカルサーバー

```bash
# http-server（推奨）
npx http-server typescript -p 3000

# Python 3
python -m http.server 3000

# Python 2
python -m SimpleHTTPServer 3000

# Node.js serve
npx serve typescript -p 3000
```

### ファイル操作

```bash
# ファイル一覧確認
ls -la typescript/
ls -la typescript/modules/

# Windows PowerShell
Get-ChildItem typescript
Get-ChildItem typescript/modules

# 生成ファイル確認
ls typescript/modules/*.js
Get-ChildItem typescript/modules -Filter "*.js"
```

---

## 🚨 トラブルシューティング

### よくあるエラーと解決方法

#### 1. モジュールが見つからないエラー

**エラー**: `Cannot find module './modules/DOMUtils.js'`

**原因**: TypeScriptファイルがコンパイルされていない

**解決方法**:
```bash
cd typescript
npx tsc --build
ls modules/*.js  # JSファイルが生成されているか確認
```

#### 2. CORS エラー

**エラー**: `Access to script blocked by CORS policy`

**原因**: file:// プロトコルでHTMLファイルを開いている

**解決方法**:
```bash
# ローカルサーバーを使用
npx http-server typescript -p 3000
# ブラウザで http://localhost:3000/frontend-demo.html
```

#### 3. DOM型が見つからないエラー

**エラー**: `Cannot find name 'document'`

**原因**: tsconfig.jsonにDOM型が含まれていない

**解決方法**:
```json
// tsconfig.json
{
  "compilerOptions": {
    "lib": ["ES2020", "DOM", "DOM.Iterable"]
  }
}
```

#### 4. 重複識別子エラー

**エラー**: `Duplicate identifier 'Person'`

**原因**: 複数のファイルで同じ型名を使用

**解決方法**: 型名を変更するか、namespaceを使用

#### 5. ポート使用中エラー

**エラー**: `Port 3000 is already in use`

**解決方法**:
```bash
# 別のポートを使用
npx http-server typescript -p 3001

# または使用中のプロセスを終了
# Windows: netstat -ano | findstr :3000
# macOS/Linux: lsof -ti:3000 | xargs kill
```

### デバッグ方法

#### ブラウザ開発者ツール

1. **F12** で開発者ツールを開く
2. **Console** タブでエラーメッセージを確認
3. **Network** タブでリソース読み込みを確認
4. **Sources** タブでJavaScriptをデバッグ

#### コンソールデバッグ

```typescript
// デバッグ用のログ出力
console.log('デバッグ情報:', variable);
console.table(arrayData);
console.error('エラー:', error);

// ブレークポイント
debugger; // この行で実行が停止
```

#### TypeScriptエラー確認

```bash
# 詳細なエラー情報
npx tsc --noEmit --strict

# 特定ファイルのエラー確認
npx tsc modules/DOMUtils.ts --noEmit
```

---

## 📁 ファイル構成

```
learning_public/
├── package.json                    # npm設定・スクリプト
├── package-lock.json              # 依存関係ロック
└── typescript/                    # TypeScriptプロジェクト
    ├── 01_basic_types_practice.ts # メインエントリーポイント
    ├── 01_basic_types_practice.js # コンパイル済み（基本練習）
    ├── hello_world.ts             # Hello Worldサンプル
    ├── hello_world.js             # コンパイル済み（Hello World）
    ├── frontend-demo.html         # ブラウザ実行用HTML
    ├── tsconfig.json              # TypeScript設定
    ├── modules/                   # モジュール群
    │   ├── DOMUtils.ts           # DOM操作ユーティリティ
    │   ├── DOMUtils.js           # コンパイル済み
    │   ├── FormHandler.ts        # フォーム処理
    │   ├── FormHandler.js        # コンパイル済み
    │   ├── UIComponents.ts       # UIコンポーネント
    │   ├── UIComponents.js       # コンパイル済み
    │   ├── FrontendApp.ts        # メインアプリケーション
    │   └── FrontendApp.js        # コンパイル済み
    └── dist/                     # ビルド出力（設定により生成）
```

### 各ファイルの役割

#### メインファイル
- **`01_basic_types_practice.ts`**: TypeScript基本学習 + フロントエンド統合
- **`frontend-demo.html`**: ブラウザ実行用のHTMLファイル
- **`tsconfig.json`**: TypeScriptコンパイラ設定

#### モジュールファイル
- **`DOMUtils.ts`**: 型安全なDOM操作ユーティリティ
- **`FormHandler.ts`**: フォーム処理・バリデーション
- **`UIComponents.ts`**: 再利用可能なUIコンポーネント（Card, Modal, Tooltip）
- **`FrontendApp.ts`**: アプリケーション統合・状態管理

#### 設定・ドキュメント
- **`COMPLETE_SETUP_GUIDE.md`**: 本ファイル（完全セットアップガイド）
- **`DETAILED_USAGE_GUIDE.md`**: 詳細な使用方法
- **`QUICK_REFERENCE.md`**: クイックリファレンス
- **`TYPESCRIPT_PRACTICE_GUIDE.md`**: 学習ガイド

---

## ⚙️ 開発環境設定

### VS Code 設定

#### settings.json
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
  },
  "files.associations": {
    "*.ts": "typescript"
  }
}
```

#### launch.json（デバッグ設定）
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Launch TypeScript",
      "type": "node",
      "request": "launch",
      "program": "${workspaceFolder}/typescript/01_basic_types_practice.js",
      "preLaunchTask": "tsc: build - typescript/tsconfig.json",
      "outFiles": ["${workspaceFolder}/typescript/**/*.js"]
    }
  ]
}
```

#### tasks.json（タスク設定）
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "type": "typescript",
      "tsconfig": "typescript/tsconfig.json",
      "problemMatcher": ["$tsc"],
      "group": "build",
      "label": "tsc: build - typescript/tsconfig.json"
    }
  ]
}
```

### ESLint 設定

#### .eslintrc.json
```json
{
  "extends": [
    "@typescript-eslint/recommended"
  ],
  "parser": "@typescript-eslint/parser",
  "plugins": ["@typescript-eslint"],
  "parserOptions": {
    "ecmaVersion": 2020,
    "sourceType": "module"
  },
  "rules": {
    "@typescript-eslint/no-unused-vars": "error",
    "@typescript-eslint/explicit-function-return-type": "warn",
    "@typescript-eslint/no-explicit-any": "warn"
  }
}
```

### Prettier 設定

#### .prettierrc
```json
{
  "semi": true,
  "trailingComma": "es5",
  "singleQuote": true,
  "printWidth": 80,
  "tabWidth": 2,
  "useTabs": false
}
```

---

## 🚀 デプロイ方法

### 静的サイトホスティング

#### Vercel
```bash
# Vercelにデプロイ
npm install -g vercel
cd typescript
vercel --prod
```

#### Netlify
```bash
# Netlifyにデプロイ
npm install -g netlify-cli
cd typescript
netlify deploy --prod --dir .
```

#### GitHub Pages
```yaml
# .github/workflows/deploy.yml
name: Deploy to GitHub Pages
on:
  push:
    branches: [ main ]
jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - uses: actions/setup-node@v2
        with:
          node-version: '18'
      - run: npm install
      - run: cd typescript && npx tsc --build
      - uses: peaceiris/actions-gh-pages@v3
        with:
          github_token: ${{ secrets.GITHUB_TOKEN }}
          publish_dir: ./typescript
```

### 本番ビルド

#### 最適化設定
```json
// tsconfig.prod.json
{
  "extends": "./tsconfig.json",
  "compilerOptions": {
    "sourceMap": false,
    "removeComments": true,
    "declaration": false
  }
}
```

#### ビルドスクリプト
```bash
# 本番用ビルド
npx tsc --project tsconfig.prod.json

# ファイルサイズ最適化
npx terser dist/*.js --compress --mangle --output dist/app.min.js
```

---

## 📚 学習リソース

### 公式ドキュメント
- [TypeScript公式](https://www.typescriptlang.org/docs/)
- [MDN Web Docs](https://developer.mozilla.org/ja/)
- [Node.js公式](https://nodejs.org/ja/docs/)

### 推奨書籍
- 「プログラミングTypeScript」- Boris Cherny
- 「Effective TypeScript」- Dan Vanderkam
- 「TypeScript実践プログラミング」- 今村謙士

### オンライン学習
- [TypeScript Playground](https://www.typescriptlang.org/play)
- [TypeScript Deep Dive](https://basarat.gitbook.io/typescript/)
- [TypeScript Exercises](https://typescript-exercises.github.io/)

---

## 🎯 次のステップ

### レベルアップ学習
1. **React + TypeScript**: `npx create-react-app my-app --template typescript`
2. **Vue.js + TypeScript**: `npm create vue@latest my-vue-app`
3. **Angular**: `ng new my-angular-app --strict`
4. **Node.js + TypeScript**: Express.js でAPI開発

### 実践プロジェクト
1. **Todoアプリ**: CRUD操作の実装
2. **天気予報アプリ**: API連携の実装
3. **チャットアプリ**: WebSocket通信の実装
4. **Eコマースサイト**: 決済システムの実装

---

## 📞 サポート

### 質問・バグ報告
- **GitHub Issues**: プロジェクトのIssueページ
- **Stack Overflow**: `typescript` タグで質問
- **Discord**: TypeScript Community

### コミュニティ
- **TypeScript Japan User Group**
- **JavaScript Meetup**
- **フロントエンド勉強会**

---

## ✅ チェックリスト

### 環境セットアップ
- [ ] Node.js インストール済み
- [ ] TypeScript インストール済み
- [ ] VS Code + 拡張機能設定済み
- [ ] プロジェクトファイル確認済み

### 基本実行
- [ ] `npm run ts:run-basic` 成功
- [ ] ローカルサーバー起動成功
- [ ] ブラウザでアプリケーション表示成功
- [ ] フォーム機能動作確認済み

### 開発環境
- [ ] ファイル監視モード動作確認
- [ ] デバッグ設定完了
- [ ] リンター・フォーマッター動作確認
- [ ] エラーハンドリング理解済み

---

**🎉 これで完全なTypeScriptフロントエンド開発環境が整いました！**

何か問題が発生した場合は、このガイドのトラブルシューティングセクションを参照してください。
