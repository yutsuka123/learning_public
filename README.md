# 🚀 TypeScript Learning Project

プロフェッショナルなTypeScript開発の学習プロジェクト - 基本型システムからフロントエンドアプリケーション開発まで

## 📋 クイックスタート

### 1. 環境確認
```bash
node --version  # v18.0.0+ 必要
npm --version   # v9.0.0+ 必要
```

### 2. セットアップ
```bash
# 依存関係のインストール & 初回ビルド
npm run setup
```

### 3. 実行

#### コンソール版（基本学習）
```bash
npm run ts:run-basic
```

#### ブラウザ版（フル機能）
```bash
npm start
# または
npm run demo
```
ブラウザで `http://localhost:3000/frontend-demo.html` を開く

## 🎯 主な機能

- ✅ **TypeScript基本型システム** - プリミティブ型、配列、オブジェクト型
- ✅ **フロントエンドアプリケーション** - ユーザー管理システム
- ✅ **フォーム処理** - リアルタイムバリデーション
- ✅ **UIコンポーネント** - Card、Modal、Tooltip
- ✅ **DOM操作** - 型安全なユーティリティ
- ✅ **エラーハンドリング** - 堅牢なエラー処理

## 📝 コマンド一覧

```bash
npm run ts:help        # 全コマンド表示
npm start              # ブラウザ版起動
npm run dev            # 開発モード（ファイル監視）
npm run build          # プロダクションビルド
npm run clean          # クリーンアップ
```

## 📁 プロジェクト構成

```
├── typescript/                    # TypeScriptプロジェクト
│   ├── 01_basic_types_practice.ts # メインエントリーポイント
│   ├── frontend-demo.html         # ブラウザ実行用HTML
│   ├── modules/                   # モジュール群
│   │   ├── DOMUtils.ts           # DOM操作ユーティリティ
│   │   ├── FormHandler.ts        # フォーム処理
│   │   ├── UIComponents.ts       # UIコンポーネント
│   │   └── FrontendApp.ts        # メインアプリケーション
│   └── *.md                      # 詳細ドキュメント
└── package.json                  # npm設定
```

## 📚 ドキュメント

- **[完全セットアップガイド](typescript/COMPLETE_SETUP_GUIDE.md)** - 詳細なセットアップ手順
- **[詳細使用方法](typescript/DETAILED_USAGE_GUIDE.md)** - 各機能の詳細な使い方
- **[クイックリファレンス](typescript/QUICK_REFERENCE.md)** - コマンド・機能早見表
- **[学習ガイド](typescript/TYPESCRIPT_PRACTICE_GUIDE.md)** - TypeScript学習ロードマップ

## 🛠️ 環境要件

- **Node.js**: 18.0.0+
- **npm**: 9.0.0+
- **ブラウザ**: Chrome, Firefox, Safari, Edge の最新版

## 🎓 学習内容

### レベル1: 基礎
- プリミティブ型、配列型、オブジェクト型
- Union型、Intersection型、リテラル型
- 関数型、ジェネリクス

### レベル2: 応用
- DOM操作、イベントハンドリング
- フォーム処理、バリデーション
- UIコンポーネント設計

### レベル3: 実践
- モジュール分割アーキテクチャ
- 状態管理、エラーハンドリング
- 型安全なアプリケーション開発

## 🚨 トラブルシューティング

### よくある問題

#### モジュールが見つからない
```bash
npm run ts:build-all  # 再コンパイル
```

#### CORSエラー
```bash
npm run ts:serve      # ローカルサーバー使用
```

#### 詳細は [完全セットアップガイド](typescript/COMPLETE_SETUP_GUIDE.md) を参照

## 🤝 コントリビューション

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📄 ライセンス

MIT License - 詳細は [LICENSE](LICENSE) ファイルを参照

## 🙏 謝辞

このプロジェクトは TypeScript と現代的なフロントエンド開発の学習を目的として作成されました。

---

**🚀 Happy Learning with TypeScript!**