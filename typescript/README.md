# TypeScript 学習・復習フォルダ

## 目的
TypeScriptの基礎から最新機能まで、型安全なJavaScript開発のスキルを習得することを目的とします。静的型付け、最新のECMAScript機能、React/Node.js連携など、現代的なWeb開発に必要な技術を総合的に学習します。

## 学習内容

### 基本的な型システム
- **基本型**: string、number、boolean、null、undefined
- **配列と tuple**: Array<T>、[string, number]
- **オブジェクト型**: interface、type alias、optional properties
- **関数型**: 引数の型、戻り値の型、関数オーバーロード
- **Union型とIntersection型**: |、&演算子

### 高度な型システム
- **ジェネリクス**: <T>、制約、条件付き型
- **ユーティリティ型**: Partial<T>、Required<T>、Pick<T>、Omit<T>
- **マップ型**: keyof、in演算子、テンプレートリテラル型
- **型ガード**: typeof、instanceof、カスタム型ガード
- **リテラル型**: 文字列リテラル、数値リテラル、真偽値リテラル

### 最新のTypeScript機能（TS 4.0以降）
- **テンプレートリテラル型**: バックティック記法での型定義
- **Variadic Tuple Types**: 可変長タプル型
- **Recursive Conditional Types**: 再帰的条件型
- **Template Literal Pattern Matching**: パターンマッチング
- **satisfies演算子**: 型制約の確認

### ECMAScript最新機能
- **ES2020+**: Optional Chaining、Nullish Coalescing
- **ES2021+**: Logical Assignment Operators
- **ES2022+**: Top-level await、Private Fields
- **ES2023+**: Array.findLast()、Array.toReversed()
- **モジュール**: import/export、dynamic import

### オブジェクト指向プログラミング
- **クラス**: constructor、メソッド、プロパティ
- **継承**: extends、super、抽象クラス
- **アクセス修飾子**: public、private、protected、readonly
- **デコレータ**: クラスデコレータ、メソッドデコレータ

### 関数型プログラミング
- **高階関数**: map、filter、reduce、forEach
- **関数合成**: pipe、compose
- **カリー化**: 部分適用、クロージャ
- **不変性**: readonly、const assertions

### 実践的な開発環境
- **コンパイラ設定**: tsconfig.json、strict mode
- **リンター**: ESLint、Prettier
- **バンドラー**: Webpack、Vite、esbuild
- **テスト**: Jest、Vitest、Testing Library

### フレームワーク連携
- **React**: JSX、Hooks、型安全なコンポーネント
- **Node.js**: Express、型安全なAPI開発
- **Vue.js**: Composition API、型安全なテンプレート
- **Angular**: TypeScript-first フレームワーク

## プロジェクト構成
```
typescript/
├── basics/              # 基本的な型システム
├── advanced-types/      # 高度な型機能
├── modern-features/     # 最新のTypeScript機能
├── oop/                # オブジェクト指向プログラミング
├── functional/         # 関数型プログラミング
├── ecmascript/         # 最新のECMAScript機能
├── tooling/            # 開発ツールと設定
├── react-integration/  # React連携
├── node-integration/   # Node.js連携
├── testing/            # テスト駆動開発
└── projects/           # 実践プロジェクト
```

## 学習方針
- 型安全性を最大限に活用したコード
- 最新のECMAScript機能の積極的な採用
- 実践的なWeb開発プロジェクトの実装
- 詳細な型注釈と日本語コメント
- モダンな開発ツールチェーンの活用

## 開発環境セットアップ
```bash
# Node.js と npm のインストール後
npm init -y
npm install -D typescript @types/node
npm install -D eslint @typescript-eslint/parser @typescript-eslint/eslint-plugin
npm install -D prettier
npx tsc --init
```

## 推奨VS Code拡張機能
- TypeScript Importer
- ESLint
- Prettier
- Error Lens
- Auto Rename Tag
- Bracket Pair Colorizer