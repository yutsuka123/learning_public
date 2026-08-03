# データベース Python サンプル（SQLite / PostgreSQL）

[重要] このフォルダは、PythonからSQLite/PostgreSQLを扱う際の基本・作法・注意点・
DB設計の良い例悪い例・プロレベルになるための勘所を、実際に動くコードで確認するためのものです。

## ファイル構成

| ファイル | 内容 | 必要なもの |
|---|---|---|
| `sqlite_basics.py` | 接続/CRUD/パラメータ化クエリ/トランザクション/`with`の落とし穴/`row_factory` | 標準ライブラリのみ |
| `sql_injection_demo.py` | SQLインジェクションの実演（脆弱な例が実際に突破される様子と、防御の確認） | 標準ライブラリのみ |
| `schema_design_good_vs_bad.py` | 型/制約の要否、インデックスの効果を実測、外部キー制約の落とし穴 | 標準ライブラリのみ |
| `postgresql_basics.py` | 接続/CRUD/`RETURNING`句/`with`の意味の違い/トランザクション | PostgreSQLサーバー、`psycopg` |
| [`NORMALIZATION_GUIDE.md`](NORMALIZATION_GUIDE.md) | **テーブルをどう分けるか**（正規化）をEC注文管理を題材に、ER図・表形式のスキーマ・キー設計まで解説 | — （読み物） |
| `normalization_example.py` | 上記ガイドに対応する実行可能コード。悪い例で3種の異常（更新/挿入/削除）を実際に再現し、良い例で解消されることを確認 | 標準ライブラリのみ |

## 1. 基本（SQLite vs PostgreSQL、いつどちらを使うか）

| 観点 | SQLite | PostgreSQL |
|---|---|---|
| 形態 | ファイル1つ（または`:memory:`）に収まる組み込みDB | サーバーとして常駐するクライアント/サーバー型DB |
| セットアップ | 不要（Python標準ライブラリの`sqlite3`のみ） | サーバーのインストール・起動・DB作成が必要 |
| 同時書き込み | 弱い（基本的に1プロセスの書き込みを直列化） | 強い（複数プロセス/コネクションからの同時書き込みに対応） |
| 向いている用途 | ローカルアプリの内部DB、テスト、プロトタイプ、組み込み機器 | Webサービスのバックエンド、複数クライアントが同時アクセスする本番システム |
| このリポジトリでの関連 | — | `python/examples/django_minimal/`（DjangoのORM経由でSQLiteを使用中） |

[目安] 「1台のアプリが自分のためだけにデータを持つ」ならSQLite、「複数のクライアント/サーバーが
同時にアクセスする」ならPostgreSQL、という判断が実務でも一般的。

## 2. 作法（守るべき基本ルール）

1. **必ずパラメータ化クエリを使う**: SQL文に値を直接埋め込まない（`sql_injection_demo.py`で
   なぜ危険かを実演）。SQLiteは`?`、psycopgは`%s`とプレースホルダの書式が異なる点に注意。
2. **`commit()`/トランザクションの境界を意識する**: SQLiteは明示的な`commit()`が必要
   （`with conn:`ブロックなら自動）。psycopgも同様に`with`ブロックの終わりで自動commit/rollback。
3. **`with`の意味はライブラリごとに違うと知る**: SQLiteの`with conn:`はcommit/rollbackのみ
   （接続はcloseされない）。psycopgの`with psycopg.connect(...):`は接続自体もcloseする
   （`sqlite_basics.py`の`demonstrateContextManagerGotcha`と`postgresql_basics.py`の
   `demonstrateContextManagerDifferenceFromSqlite`で対比）。
4. **例外処理でロールバックを意識する**: 複数の更新を1つの処理としてまとめたい場合、
   途中で失敗したら全体を取り消す（ロールバック）。

## 3. 注意点（実務でハマりやすい落とし穴）

- **SQLインジェクション**: 文字列結合でSQLを組み立てると、入力次第で認証をバイパスされる
  （`sql_injection_demo.py`で実際に突破される様子を確認できる）。
- **SQLiteは既定で外部キー制約が無効**: `PRAGMA foreign_keys = ON`を明示しないと、
  `REFERENCES`を書いても無視される（`schema_design_good_vs_bad.py`で実演）。
- **N+1問題**: ループの中で1件ずつ関連データを取得すると、件数分の追加クエリが飛ぶ。
  詳細と実測（クエリ数7→1）は `python/examples/django_minimal/books/views.py` を参照
  （このフォルダの範囲外だが、DB設計・アクセスパターンの重要な注意点として直結する）。
- **型を緩くしすぎない**: SQLiteは列の型を緩く扱う（`TEXT`列に数値以外を入れても既定では
  エラーにならない）。PostgreSQLはより厳格。「動くから良い」ではなく、不正なデータを
  DB側で弾けるように型・制約を設計する（`schema_design_good_vs_bad.py`§1）。
- **`with`ブロックの意味の違い**（上記「作法」参照）。

## 4. DB設計の良い例・悪い例

[相互参照] 「テーブルをどう分けるか」（正規化・ER図・キー設計）は、より詳しく
[`NORMALIZATION_GUIDE.md`](NORMALIZATION_GUIDE.md) にまとめている。本節は列単位の設計
（`schema_design_good_vs_bad.py`が実測している内容）の要約。

`schema_design_good_vs_bad.py`で実測している内容の要約:

- **型・制約**: 何でも`TEXT`・制約なしで受けると、不正なデータ（文字列の価格、NULLの必須項目、
  負の価格等）がそのまま入ってしまう。適切な型 + `NOT NULL` + `CHECK`制約で、
  DB自身が「あり得ない状態」を拒否できるようにする。
- **インデックス**: 検索条件に使う列にインデックスが無いと、件数が増えるほど検索が遅くなる
  （全件走査）。10,000行での実測では、インデックス無し/有りで実行時間に約20倍の差が出た
  （具体的な倍率は環境依存）。`EXPLAIN QUERY PLAN`で「SCAN（全件走査）」か
  「SEARCH...USING INDEX（インデックス探索）」かを確認する習慣が重要。
- **外部キー制約**: 「存在しない親レコードを参照する子レコード」を防ぐ。SQLiteでは
  明示的な有効化が必要な点を含め、意図通りに機能しているかを必ず確認する。

## 5. プロレベルになるための勘所

- **`EXPLAIN` / `EXPLAIN ANALYZE`を読めるようになる**: 「なぜ遅いか」をSQL側から診断する
  基本技術。本フォルダでは`EXPLAIN QUERY PLAN`（SQLite）を使用。PostgreSQLでは
  `EXPLAIN ANALYZE`で実際の実行時間・行数見積もりの精度まで確認できる。
- **N+1を疑う癖をつける**: ループ内でクエリを発行していないか、`python/examples/django_minimal/`
  の`select_related`のような「まとめて取る」手段が無いかを常に確認する。
- **マイグレーション（スキーマのバージョン管理）**: 手動でのDDL実行に頼らず、Djangoの
  `makemigrations`/`migrate`（`python/examples/django_minimal/`で既に実演済み）のような
  仕組みでスキーマ変更を追跡・再現可能にする。
- **接続の扱い**: 本番のWebアプリでは、リクエストのたびに新規接続するのではなく
  コネクションプール（例: `psycopg_pool`、Djangoなら`CONN_MAX_AGE`設定）を使い、
  接続確立のオーバーヘッドを避ける。
- **トランザクション分離レベルを理解する**: 複数のトランザクションが同時に動くとき、
  互いにどこまで影響し合うかを決める設定（PostgreSQLの既定は`READ COMMITTED`）。
  「同時実行で稀にしか起きないバグ」の多くはここに起因する。
- **バックアップ/リストアの手順を持っておく**: `pg_dump`/`pg_restore`（PostgreSQL）、
  ファイルコピー（SQLite）。本番運用ではデータ消失時の手順を事前に確立しておく。

## セットアップ

### SQLite（`sqlite_basics.py`, `sql_injection_demo.py`, `schema_design_good_vs_bad.py`）

追加インストール不要（Python標準ライブラリの`sqlite3`を使用）。

```sh
cd python/database
python3 sqlite_basics.py
python3 sql_injection_demo.py
python3 schema_design_good_vs_bad.py
```

### PostgreSQL（`postgresql_basics.py`）

```sh
# インストール（macOS/Homebrew）
brew install postgresql@16
export PATH="/opt/homebrew/opt/postgresql@16/bin:$PATH"

# サーバー起動（バックグラウンドサービスにせず、都度手動で起動する場合）
pg_ctl -D /opt/homebrew/var/postgresql@16 -l /tmp/pg_log.txt start

# テスト用DB作成（初回のみ）
createdb learning_public_sample

# Python側の依存関係
cd python/database
pip install -r requirements.txt

python3 postgresql_basics.py

# 使い終わったら停止
pg_ctl -D /opt/homebrew/var/postgresql@16 stop
```

[参考] ログイン時に自動起動する常駐サービスにしたい場合は `brew services start postgresql@16`
（本サンプルの検証では、必要なときだけ起動する上記の`pg_ctl`方式を使用した）。

## 検証環境

- macOS, Python 3.14, sqlite3（標準ライブラリ, SQLite同梱バージョン）
- PostgreSQL 16.14 (Homebrew), psycopg 3.3.4
- 全ファイル、実行して出力を確認済み。`sql_injection_demo.py`は実際に攻撃が成立する様子と
  防がれる様子の両方を実行して確認した。

## 関連ファイル

- N+1問題の実演（本フォルダの範囲外だが密接に関連）:
  `../examples/django_minimal/books/views.py`
- Djangoのマイグレーション（スキーマのバージョン管理の実例）:
  `../examples/django_minimal/books/migrations/`
