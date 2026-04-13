# TypeScript 開発環境インストールガイド

## 📋 概要
TypeScript開発のための環境セットアップ手順を説明します。型安全なJavaScript開発、最新のECMAScript機能、React/Node.js連携に対応した開発環境を構築します。

C# や C/C++ との対比、Node.js での活用の整理は `LANGUAGE_COMPARISON_AND_NODEJS.md` を参照してください。

## ✅ 前提条件
- Windows 10/11 または macOS、Linux
- 管理者権限でのコマンド実行が可能
- インターネット接続
- 2GB以上の空きディスク容量

## 🎯 インストール対象
- Node.js（JavaScript ランタイム）
- npm または yarn（パッケージマネージャー）
- TypeScript Compiler
- Visual Studio Code + TypeScript 拡張機能
- ESLint、Prettier（コード品質ツール）

---

## 🟢 Node.js 環境のセットアップ

### **1. Node.js のインストール**

#### **Windows の場合**
```powershell
# Chocolateyを使用
choco install nodejs

# Scoop を使用
scoop install nodejs

# 手動インストール
# https://nodejs.org/ からLTS版をダウンロード・インストール
```

#### **macOS の場合**
```bash
# Homebrew を使用
brew install node

# MacPorts を使用
sudo port install nodejs18

# 手動インストール
# https://nodejs.org/ からLTS版をダウンロード・インストール
```

#### **Linux の場合**
```bash
# Ubuntu/Debian（NodeSource リポジトリ使用）
curl -fsSL https://deb.nodesource.com/setup_lts.x | sudo -E bash -
sudo apt-get install -y nodejs

# CentOS/RHEL/Fedora
curl -fsSL https://rpm.nodesource.com/setup_lts.x | sudo bash -
sudo dnf install nodejs npm

# Snap を使用（Ubuntu）
sudo snap install node --classic
```

#### **確認**
```bash
node --version
npm --version
```

### **2. パッケージマネージャーの選択**

#### **npm の更新**
```bash
# npm を最新版に更新
npm install -g npm@latest

# npm の設定確認
npm config list
```

#### **yarn のインストール（オプション）**
```bash
# npm を使用してyarnをインストール
npm install -g yarn

# 確認
yarn --version
```

#### **pnpm のインストール（オプション）**
```bash
# npm を使用してpnpmをインストール
npm install -g pnpm

# 確認
pnpm --version
```

### **3. TypeScript のグローバルインストール**

```bash
# TypeScript コンパイラのインストール
npm install -g typescript

# ts-node（TypeScriptの直接実行）
npm install -g ts-node

# 確認
tsc --version
ts-node --version
```

### **4. Visual Studio Code 拡張機能**

#### **必須拡張機能**
```bash
# TypeScript はVS Codeに標準搭載されているため追加不要

# ESLint（リンター）
code --install-extension dbaeumer.vscode-eslint

# Prettier（フォーマッター）
code --install-extension esbenp.prettier-vscode

# Error Lens（エラー可視化）
code --install-extension usernamehw.errorlens
```

#### **推奨拡張機能**
```bash
# Auto Rename Tag
code --install-extension formulahendry.auto-rename-tag

# Bracket Pair Colorizer
code --install-extension coenraads.bracket-pair-colorizer-2

# Path Intellisense
code --install-extension christian-kohler.path-intellisense

# TypeScript Importer
code --install-extension pmneo.tsimporter

# REST Client（API テスト用）
code --install-extension humao.rest-client
```

---

## 🏗️ プロジェクトの設定

### **5. TypeScript プロジェクトの作成**

#### **新しいプロジェクトの初期化**
```bash
# プロジェクトディレクトリの作成と移動
cd typescript
mkdir my-typescript-project
cd my-typescript-project

# package.json の作成
npm init -y

# TypeScript 設定ファイルの作成
tsc --init
```

#### **package.json の設定**
```json
{
  "name": "my-typescript-project",
  "version": "1.0.0",
  "description": "TypeScript学習プロジェクト",
  "main": "dist/index.js",
  "scripts": {
    "build": "tsc",
    "start": "node dist/index.js",
    "dev": "ts-node src/index.ts",
    "watch": "tsc --watch",
    "clean": "rimraf dist",
    "lint": "eslint src/**/*.ts",
    "lint:fix": "eslint src/**/*.ts --fix",
    "format": "prettier --write src/**/*.ts",
    "test": "jest",
    "test:watch": "jest --watch"
  },
  "keywords": ["typescript", "learning"],
  "author": "Your Name",
  "license": "MIT",
  "devDependencies": {
    "@types/node": "^20.10.0",
    "@typescript-eslint/eslint-plugin": "^6.13.0",
    "@typescript-eslint/parser": "^6.13.0",
    "eslint": "^8.55.0",
    "prettier": "^3.1.0",
    "rimraf": "^5.0.5",
    "ts-node": "^10.9.0",
    "typescript": "^5.3.0"
  },
  "dependencies": {}
}
```

#### **依存関係のインストール**
```bash
# 開発依存関係のインストール
npm install --save-dev @types/node @typescript-eslint/eslint-plugin @typescript-eslint/parser eslint prettier rimraf ts-node typescript

# 実行時依存関係（必要に応じて）
npm install lodash axios express
npm install --save-dev @types/lodash @types/express
```

### **6. TypeScript 設定ファイル**

#### **tsconfig.json**
```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "commonjs",
    "lib": ["ES2022", "DOM"],
    "outDir": "./dist",
    "rootDir": "./src",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
    "moduleResolution": "node",
    "resolveJsonModule": true,
    "declaration": true,
    "declarationMap": true,
    "sourceMap": true,
    "removeComments": false,
    "noImplicitAny": true,
    "noImplicitReturns": true,
    "noImplicitThis": true,
    "noUnusedLocals": true,
    "noUnusedParameters": true,
    "exactOptionalPropertyTypes": true,
    "noImplicitOverride": true,
    "noPropertyAccessFromIndexSignature": true,
    "noUncheckedIndexedAccess": true,
    "allowUnreachableCode": false,
    "allowUnusedLabels": false,
    "experimentalDecorators": true,
    "emitDecoratorMetadata": true
  },
  "include": ["src/**/*"],
  "exclude": ["node_modules", "dist", "**/*.test.ts", "**/*.spec.ts"]
}
```

#### **tsconfig.json（Node.js プロジェクト用）**
```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "commonjs",
    "lib": ["ES2022"],
    "outDir": "./dist",
    "rootDir": "./src",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
    "moduleResolution": "node",
    "resolveJsonModule": true,
    "declaration": true,
    "sourceMap": true,
    "types": ["node"]
  },
  "include": ["src/**/*"],
  "exclude": ["node_modules", "dist"]
}
```

### **7. コード品質ツールの設定**

#### **.eslintrc.js**
```javascript
module.exports = {
  parser: '@typescript-eslint/parser',
  parserOptions: {
    ecmaVersion: 2022,
    sourceType: 'module',
    project: './tsconfig.json'
  },
  plugins: ['@typescript-eslint'],
  extends: [
    'eslint:recommended',
    '@typescript-eslint/recommended',
    '@typescript-eslint/recommended-requiring-type-checking'
  ],
  rules: {
    '@typescript-eslint/no-unused-vars': 'error',
    '@typescript-eslint/explicit-function-return-type': 'warn',
    '@typescript-eslint/no-explicit-any': 'warn',
    '@typescript-eslint/no-non-null-assertion': 'warn',
    '@typescript-eslint/prefer-const': 'error',
    'no-var': 'error',
    'prefer-const': 'error',
    'no-console': 'warn'
  },
  env: {
    node: true,
    es2022: true
  }
};
```

#### **.prettierrc**
```json
{
  "semi": true,
  "trailingComma": "es5",
  "singleQuote": true,
  "printWidth": 80,
  "tabWidth": 2,
  "useTabs": false,
  "bracketSpacing": true,
  "arrowParens": "avoid",
  "endOfLine": "lf"
}
```

#### **.prettierignore**
```
node_modules/
dist/
*.min.js
*.bundle.js
```

### **8. VS Code の設定ファイル**

#### **.vscode/settings.json**
```json
{
  "typescript.preferences.quoteStyle": "single",
  "typescript.preferences.includePackageJsonAutoImports": "auto",
  "typescript.suggest.autoImports": true,
  "typescript.updateImportsOnFileMove.enabled": "always",
  "editor.formatOnSave": true,
  "editor.defaultFormatter": "esbenp.prettier-vscode",
  "editor.codeActionsOnSave": {
    "source.fixAll.eslint": true,
    "source.organizeImports": true
  },
  "[typescript]": {
    "editor.defaultFormatter": "esbenp.prettier-vscode"
  },
  "[javascript]": {
    "editor.defaultFormatter": "esbenp.prettier-vscode"
  },
  "eslint.validate": ["typescript", "javascript"],
  "files.exclude": {
    "**/node_modules": true,
    "**/dist": true
  }
}
```

#### **.vscode/launch.json**
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "TypeScript: Current File",
      "type": "node",
      "request": "launch",
      "program": "${file}",
      "runtimeArgs": ["-r", "ts-node/register"],
      "env": {
        "NODE_ENV": "development"
      }
    },
    {
      "name": "TypeScript: Debug",
      "type": "node",
      "request": "launch",
      "program": "${workspaceFolder}/src/index.ts",
      "runtimeArgs": ["-r", "ts-node/register"],
      "env": {
        "NODE_ENV": "development"
      }
    },
    {
      "name": "Node: Launch Program",
      "type": "node",
      "request": "launch",
      "program": "${workspaceFolder}/dist/index.js",
      "preLaunchTask": "npm: build"
    }
  ]
}
```

#### **.vscode/tasks.json**
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "type": "npm",
      "script": "build",
      "group": {
        "kind": "build",
        "isDefault": true
      }
    },
    {
      "type": "npm",
      "script": "watch",
      "group": "build",
      "isBackground": true
    },
    {
      "type": "npm",
      "script": "lint",
      "group": "test"
    },
    {
      "type": "npm",
      "script": "format",
      "group": "build"
    }
  ]
}
```

---

## 🧪 動作確認

### **9. サンプルプロジェクトのビルドと実行**

#### **プロジェクト構造の作成**
```bash
# ディレクトリ構造の作成
mkdir -p src
mkdir -p dist

# メインファイルの作成（既存のhello_world.tsを移動）
mv hello_world.ts src/index.ts
```

#### **ビルドと実行**
```bash
# TypeScript のコンパイル
npm run build

# 実行
npm start

# 開発モード（直接実行）
npm run dev

# ウォッチモード（ファイル変更の監視）
npm run watch
```

#### **コード品質チェック**
```bash
# ESLint によるリンティング
npm run lint

# ESLint による自動修正
npm run lint:fix

# Prettier によるフォーマット
npm run format
```

### **10. React プロジェクトの設定（オプション）**

#### **React + TypeScript プロジェクトの作成**
```bash
# Create React App with TypeScript
npx create-react-app my-react-app --template typescript
cd my-react-app

# または Vite を使用
npm create vite@latest my-react-app -- --template react-ts
cd my-react-app
npm install
```

### **11. Express.js プロジェクトの設定（オプション）**

#### **Express + TypeScript の設定**
```bash
# Express関連の依存関係
npm install express
npm install --save-dev @types/express nodemon

# package.json のスクリプト追加
"scripts": {
  "dev:server": "nodemon --exec ts-node src/server.ts",
  "build:server": "tsc",
  "start:server": "node dist/server.js"
}
```

#### **Express サーバーのサンプル**
```typescript
// src/server.ts
import express, { Request, Response } from 'express';

const app = express();
const port = process.env.PORT || 3000;

app.use(express.json());

app.get('/', (req: Request, res: Response) => {
  res.json({ message: 'Hello TypeScript + Express!' });
});

app.get('/api/users/:id', (req: Request, res: Response) => {
  const { id } = req.params;
  res.json({ id, name: `User ${id}` });
});

app.listen(port, () => {
  console.log(`Server running at http://localhost:${port}`);
});
```

---

## 🔍 トラブルシューティング

### **よくある問題と解決方法**

#### **TypeScript コンパイルエラー**
```bash
# 型定義ファイルの確認
npm list @types/node

# 型定義ファイルの再インストール
npm install --save-dev @types/node

# TypeScript バージョンの確認
tsc --version
npm list typescript
```

#### **ESLint エラー**
```bash
# ESLint 設定の確認
npx eslint --print-config src/index.ts

# ESLint キャッシュのクリア
npx eslint --cache --cache-location .eslintcache src/**/*.ts
rm .eslintcache
```

#### **VS Code で TypeScript IntelliSense が動作しない**
1. Ctrl+Shift+P でコマンドパレット
2. "TypeScript: Restart TS Server" を実行
3. "Developer: Reload Window" を実行

#### **Node.js バージョンの問題**
```bash
# Node.js バージョンの確認
node --version

# nvm を使用したバージョン管理（Linux/macOS）
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.0/install.sh | bash
nvm install --lts
nvm use --lts

# nvm-windows を使用（Windows）
# https://github.com/coreybutler/nvm-windows からダウンロード
```

#### **パッケージの依存関係エラー**
```bash
# node_modules の削除と再インストール
rm -rf node_modules package-lock.json
npm install

# npm キャッシュのクリア
npm cache clean --force

# パッケージの脆弱性チェック
npm audit
npm audit fix
```

---

## 🚀 高度な機能

### **12. テスト環境の設定**

#### **Jest + TypeScript の設定**
```bash
# Jest関連パッケージのインストール
npm install --save-dev jest @types/jest ts-jest

# Jest 設定ファイルの作成
npx ts-jest config:init
```

#### **jest.config.js**
```javascript
module.exports = {
  preset: 'ts-jest',
  testEnvironment: 'node',
  roots: ['<rootDir>/src'],
  testMatch: ['**/__tests__/**/*.ts', '**/?(*.)+(spec|test).ts'],
  transform: {
    '^.+\\.ts$': 'ts-jest',
  },
  collectCoverageFrom: [
    'src/**/*.ts',
    '!src/**/*.d.ts',
  ],
};
```

### **13. デプロイメント設定**

#### **Docker の設定**
```dockerfile
# Dockerfile
FROM node:18-alpine

WORKDIR /app

COPY package*.json ./
RUN npm ci --only=production

COPY dist ./dist

EXPOSE 3000

CMD ["node", "dist/index.js"]
```

#### **.dockerignore**
```
node_modules/
src/
*.ts
tsconfig.json
.eslintrc.js
.prettierrc
```

---

## 📚 参考リンク

- [TypeScript 公式ドキュメント](https://www.typescriptlang.org/docs/)
- [Node.js 公式ドキュメント](https://nodejs.org/docs/)
- [ESLint 公式ドキュメント](https://eslint.org/docs/)
- [Prettier 公式ドキュメント](https://prettier.io/docs/)
- [VS Code TypeScript サポート](https://code.visualstudio.com/docs/languages/typescript)

---

## 📝 インストール完了チェックリスト

- [ ] Node.js LTS版がインストール済み
- [ ] npm が最新版に更新済み
- [ ] TypeScript コンパイラがインストール済み
- [ ] VS Code + TypeScript 拡張機能がインストール済み
- [ ] ESLint、Prettier がインストール・設定済み
- [ ] hello_world.ts が正常にコンパイル・実行できる
- [ ] TypeScript プロジェクトが作成・ビルドできる
- [ ] リンティングとフォーマットが動作する
- [ ] VS Code でのデバッグが設定済み

**✅ すべて完了したら、型安全な現代的なJavaScript/TypeScript開発を開始できます！**