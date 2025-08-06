# Java 学習・復習フォルダ

## 目的
Javaの基礎から最新機能まで、企業レベルのアプリケーション開発に必要なスキルを習得することを目的とします。オブジェクト指向プログラミング、デザインパターン、最新のJava機能を総合的に学習します。

## 学習内容

### 基本的な言語機能
- **基本データ型**: プリミティブ型、ラッパークラス
- **オブジェクト指向**: クラス、継承、ポリモーフィズム、カプセル化
- **インターフェース**: 抽象化、多重実装
- **例外処理**: checked例外、unchecked例外、try-with-resources

### コレクションフレームワーク
- **List**: ArrayList、LinkedList、Vector
- **Set**: HashSet、TreeSet、LinkedHashSet
- **Map**: HashMap、TreeMap、LinkedHashMap
- **Queue**: ArrayDeque、PriorityQueue

### 最新のJava機能（Java 8以降）
- **ラムダ式**: 関数型インターフェース、メソッド参照
- **Stream API**: filter、map、reduce、collect
- **Optional**: null安全性、値の存在チェック
- **日時API**: LocalDate、LocalTime、LocalDateTime
- **モジュールシステム**: Java 9以降のモジュール

### 並行プログラミング
- **スレッド**: Thread、Runnable、ExecutorService
- **同期**: synchronized、volatile、Lock
- **並行コレクション**: ConcurrentHashMap、BlockingQueue
- **CompletableFuture**: 非同期処理、コンビネータ

### デザインパターン
- **生成パターン**: Singleton、Factory、Builder
- **構造パターン**: Adapter、Decorator、Facade
- **振る舞いパターン**: Observer、Strategy、Command

## プロジェクト構成
```
java/
├── basics/            # 基本的なJava機能
├── oop/              # オブジェクト指向プログラミング
├── collections/      # コレクションフレームワーク
├── modern-features/  # 最新のJava機能（Java 8以降）
├── concurrency/      # 並行プログラミング
├── design-patterns/  # デザインパターン
├── exception-handling/ # 例外処理
├── io-nio/           # 入出力処理
└── best-practices/   # ベストプラクティス
```

## 学習方針
- Clean Codeの原則に基づいた実装
- SOLID原則の適用
- 最新のJava機能を活用した現代的なコード
- 包括的な例外処理
- 詳細な日本語JavaDoc