# Kotlin 学習・復習フォルダ

## 目的
Kotlinの現代的な言語機能を習得し、Javaとの相互運用性を活かしたアプリケーション開発スキルを身につけることを目的とします。関数型プログラミング要素、null安全性、簡潔な記述方法を重点的に学習します。

## 学習内容

### 基本的な言語機能
- **変数と型**: val、var、型推論、null安全性
- **関数**: 関数定義、デフォルト引数、名前付き引数
- **クラスとオブジェクト**: プライマリコンストラクタ、セカンダリコンストラクタ
- **プロパティ**: getter、setter、カスタムアクセサ

### Kotlin特有の機能
- **null安全性**: ?演算子、?:演算子、!!演算子
- **拡張関数**: 既存クラスの機能拡張
- **データクラス**: data class、分解宣言
- **シールドクラス**: sealed class、when式での網羅性
- **オブジェクト宣言**: object、companion object

### 関数型プログラミング
- **高階関数**: 関数を引数として受け取る関数
- **ラムダ式**: { }記法、it、明示的パラメータ
- **スコープ関数**: let、run、with、apply、also
- **コレクション操作**: map、filter、reduce、fold

### コルーチン（非同期プログラミング）
- **基本概念**: suspend関数、コルーチンスコープ
- **コルーチンビルダー**: launch、async、runBlocking
- **チャネル**: Channel、Flow
- **構造化された並行性**: SupervisorJob、CoroutineExceptionHandler

### Java相互運用性
- **Javaからの呼び出し**: @JvmStatic、@JvmOverloads
- **Kotlinからの呼び出し**: Javaライブラリの活用
- **型マッピング**: プリミティブ型、コレクション型
- **プラットフォーム型**: null許容性の扱い

## プロジェクト構成
```
kotlin/
├── basics/            # 基本的なKotlin機能
├── oop/              # オブジェクト指向プログラミング
├── functional/       # 関数型プログラミング
├── null-safety/      # null安全性
├── coroutines/       # コルーチンと非同期処理
├── collections/      # コレクション操作
├── java-interop/     # Java相互運用性
├── dsl/              # ドメイン固有言語（DSL）
└── best-practices/   # ベストプラクティス
```

## 学習方針
- Kotlinらしい簡潔で表現力豊かなコード
- null安全性を最大限に活用
- 関数型プログラミングパラダイムの理解
- Java資産の効果的な活用
- 詳細な日本語コメント