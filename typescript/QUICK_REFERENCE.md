# TypeScript フロントエンドアプリケーション クイックリファレンス

## 🚀 今すぐ始める（3分で完了）

### ステップ1: 基本実行
```bash
# プロジェクトルートで実行
npm run ts:run-basic
```

### ステップ2: ブラウザで完全版を体験
```bash
cd typescript
npx tsc --build
npx http-server . -p 3000
# ブラウザで http://localhost:3000/frontend-demo.html を開く
```

---

## 📋 コマンド一覧

### 基本コマンド
```bash
npm run ts:help              # 利用可能なコマンド表示
npm run ts:run-basic         # 基本型システム練習実行
npm run ts:run-hello         # Hello World実行
npm run ts:build-all         # 全TSファイルコンパイル
npm run ts:watch             # ファイル変更監視コンパイル
npm run ts:clean             # 生成JSファイル削除
```

### 手動コンパイル
```bash
cd typescript
npx tsc 01_basic_types_practice.ts    # 単一ファイル
npx tsc --build                       # 全ファイル
npx tsc --watch                       # 監視モード
npx tsc --noEmit                      # エラーチェックのみ
```

### ブラウザ実行
```bash
# ローカルサーバー起動
npx http-server . -p 3000
python -m http.server 3000
# Node.js 18+の場合
node --experimental-modules --input-type=module -e "import('http').then(h=>h.createServer((req,res)=>{const fs=require('fs');const path=require('path');let filePath='.'+req.url;if(filePath==='./')filePath='./index.html';const ext=path.extname(filePath);const contentType={'html':'text/html','js':'text/javascript','css':'text/css','json':'application/json'}[ext.slice(1)]||'text/plain';fs.readFile(filePath,(err,data)=>{if(err){res.writeHead(404);res.end('Not Found');}else{res.writeHead(200,{'Content-Type':contentType});res.end(data);}});}).listen(3000,()=>console.log('Server running on http://localhost:3000')))"
```

---

## 🔧 ファイル構成

```
typescript/
├── 01_basic_types_practice.ts    # メインエントリーポイント
├── modules/                      # モジュール群
│   ├── DOMUtils.ts              # DOM操作ユーティリティ
│   ├── FormHandler.ts           # フォーム処理
│   ├── UIComponents.ts          # UIコンポーネント
│   └── FrontendApp.ts           # メインアプリ
├── frontend-demo.html           # ブラウザ実行用HTML
├── tsconfig.json               # TypeScript設定
└── *.js                        # コンパイル済みファイル
```

---

## 🎯 主な機能

### 1. 基本型システム練習
- プリミティブ型、配列、オブジェクト型
- Union型、Intersection型、リテラル型
- 関数型、ジェネリクス
- エラーハンドリング

### 2. フロントエンド機能
- **フォーム処理**: リアルタイムバリデーション
- **UIコンポーネント**: Card、Modal、Tooltip
- **DOM操作**: 型安全なユーティリティ
- **状態管理**: ユーザー情報の管理

---

## 🚨 トラブルシューティング

### よくあるエラーと解決方法

#### エラー: `Cannot find module './modules/DOMUtils.js'`
```bash
# 解決方法
cd typescript
npx tsc --build
ls -la modules/*.js  # JSファイルが生成されているか確認
```

#### エラー: `Access to script blocked by CORS policy`
```bash
# 解決方法（ローカルサーバーを使用）
npx http-server . -p 3000
# ブラウザで http://localhost:3000/frontend-demo.html
```

#### エラー: `Element not found`
```html
<!-- HTMLに必要な要素があるか確認 -->
<div id="app-container"></div>
<div class="error-container"></div>
```

#### TypeScriptコンパイルエラー
```bash
# 詳細なエラー情報を表示
npx tsc --noEmit --strict
# 設定ファイルの確認
cat tsconfig.json
```

---

## 📝 カスタマイズ例

### 新しいバリデーションルール追加
```typescript
// FormHandler.ts に追加
static phoneNumber(fieldName: string): ValidationRule {
    return (value: string): ValidationResult => {
        const phoneRegex = /^0\d{1,4}-\d{1,4}-\d{4}$/;
        return {
            isValid: phoneRegex.test(value),
            message: phoneRegex.test(value) ? '' : `${fieldName}は電話番号形式で入力してください`
        };
    };
}
```

### 新しいUIコンポーネント追加
```typescript
// UIComponents.ts に追加
export class AlertBox extends BaseComponent {
    constructor(message: string, type: 'success' | 'error' | 'warning' = 'info') {
        const alertElement = document.createElement('div');
        alertElement.className = `alert alert-${type}`;
        alertElement.textContent = message;
        super(alertElement);
    }
}
```

### スタイルのカスタマイズ
```css
/* 独自のテーマカラー */
:root {
    --primary-color: #your-color;
    --secondary-color: #your-secondary-color;
}

/* カスタムボタンスタイル */
.btn-custom {
    background: var(--primary-color);
    color: white;
    border: none;
    padding: 0.75rem 1.5rem;
    border-radius: 4px;
}
```

---

## 🔍 デバッグ方法

### ブラウザ開発者ツール
1. **F12** で開発者ツールを開く
2. **Console** タブでエラーメッセージを確認
3. **Network** タブでリソース読み込みを確認
4. **Elements** タブでDOM構造を確認

### TypeScriptデバッグ
```typescript
// デバッグ用のログ出力
console.log('デバッグ情報:', { variable, state });
console.table(arrayData);
console.error('エラー情報:', error);

// ブレークポイントの設定
debugger; // この行で実行が停止する
```

### パフォーマンス測定
```typescript
// 処理時間の測定
console.time('処理名');
// 処理内容
console.timeEnd('処理名');

// メモリ使用量の確認（Chrome）
console.log(performance.memory);
```

---

## 📚 学習リソース

### 公式ドキュメント
- [TypeScript公式](https://www.typescriptlang.org/docs/)
- [MDN Web Docs](https://developer.mozilla.org/ja/)

### 推奨書籍
- 「プログラミングTypeScript」
- 「Effective TypeScript」
- 「TypeScript実践プログラミング」

### オンライン学習
- [TypeScript Playground](https://www.typescriptlang.org/play)
- [TypeScript Deep Dive](https://basarat.gitbook.io/typescript/)

---

## 🎓 次のステップ

### レベル1完了後
1. **React + TypeScript** - コンポーネントベース開発
2. **Vue.js + TypeScript** - プログレッシブフレームワーク
3. **Angular** - TypeScriptファーストフレームワーク

### レベル2完了後
1. **Node.js + TypeScript** - サーバーサイド開発
2. **Express + TypeScript** - Web API開発
3. **GraphQL + TypeScript** - モダンAPI開発

### レベル3完了後
1. **テスト駆動開発** - Jest/Vitest
2. **CI/CD** - GitHub Actions
3. **デプロイメント** - Vercel/Netlify

---

## 📞 サポート

### 質問・バグ報告
- GitHub Issues
- Stack Overflow
- TypeScript Discord

### コミュニティ
- TypeScript Japan User Group
- JavaScript Meetup
- フロントエンド勉強会

---

## 🎉 完了チェックリスト

### 基本機能
- [ ] TypeScript基本型システムの理解
- [ ] モジュール分割の理解
- [ ] DOM操作の実装
- [ ] フォーム処理の実装
- [ ] UIコンポーネントの作成

### 応用機能
- [ ] エラーハンドリングの実装
- [ ] 状態管理の理解
- [ ] API連携の実装
- [ ] テストの作成
- [ ] パフォーマンス最適化

### 実践スキル
- [ ] デバッグ技術の習得
- [ ] コードレビュー能力
- [ ] ドキュメント作成能力
- [ ] チーム開発への参加
- [ ] 継続的な学習習慣

---

**🚀 頑張って学習を進めてください！このリファレンスはいつでも参照できます。**
