"""
SQLite（標準ライブラリ `sqlite3`）の基本操作サンプル。

概要:
    SQLiteはサーバー不要でファイル1つ（または`:memory:`でメモリ上）に収まる組み込みDB。
    Pythonには標準ライブラリとして`sqlite3`が同梱されており、追加インストール無しで使える。
主な仕様:
    - demonstrateBasicCrud(): テーブル作成、INSERT/SELECT/UPDATE/DELETEの基本。
    - demonstrateParameterizedQueriesVsSqlInjection(): パラメータ化クエリの書き方と、
      文字列結合との違い（詳細な攻撃実演は`sql_injection_demo.py`参照）。
    - demonstrateTransactionRollback(): エラー時のロールバックを確認する。
    - demonstrateContextManagerGotcha(): `with sqlite3.connect(...)`が「コネクションを
      閉じる」のではなく「トランザクションをcommit/rollbackする」だけである点を確認する。
    - demonstrateRowFactory(): `row_factory`で辞書ライクな行アクセスをする。

実行方法:
    cd python/database
    python3 sqlite_basics.py
    # 検証環境: Python 3.14 標準ライブラリの sqlite3 で実行結果を確認済み。
"""

import sqlite3


def demonstrateBasicCrud():
    """
    テーブル作成、INSERT/SELECT/UPDATE/DELETEの基本を確認する。

    実行結果（例）:
        created table sensors
        inserted rows: 2
        select all: [(1, 'temp', 25.5), (2, 'humidity', 60.0)]
        after update: [(1, 'temp', 26.0), (2, 'humidity', 60.0)]
        after delete: [(1, 'temp', 26.0)]
    """

    print("=== 1. basic CRUD ===")

    conn = sqlite3.connect(":memory:")  # ファイルを作らず、プロセス内メモリ上にDBを作る（学習/テスト用）
    cursor = conn.cursor()

    cursor.execute(
        """
        CREATE TABLE sensors (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            value REAL NOT NULL
        )
        """
    )
    print("created table sensors")

    # [重要] 値は "?" プレースホルダで渡す（パラメータ化クエリ）。詳細は次の関数で解説する。
    cursor.executemany(
        "INSERT INTO sensors (name, value) VALUES (?, ?)",
        [("temp", 25.5), ("humidity", 60.0)],
    )
    conn.commit()  # [重要] INSERT/UPDATE/DELETEはcommit()しないと確定しない
    print(f"inserted rows: {cursor.rowcount}")

    cursor.execute("SELECT id, name, value FROM sensors")
    print(f"select all: {cursor.fetchall()}")

    cursor.execute("UPDATE sensors SET value = ? WHERE name = ?", (26.0, "temp"))
    conn.commit()
    cursor.execute("SELECT id, name, value FROM sensors")
    print(f"after update: {cursor.fetchall()}")

    cursor.execute("DELETE FROM sensors WHERE name = ?", ("humidity",))
    conn.commit()
    cursor.execute("SELECT id, name, value FROM sensors")
    print(f"after delete: {cursor.fetchall()}")

    conn.close()


def demonstrateParameterizedQueriesVsSqlInjection():
    """
    パラメータ化クエリ（"?"プレースホルダ）と、文字列結合の違いを確認する。

    [重要] SQL文に値を直接埋め込む（f-stringや`%`演算子での文字列結合）と、
    悪意のある入力によってSQL文の構造そのものを書き換えられる「SQLインジェクション」の
    危険がある。`?`プレースホルダを使えば、渡した値は常に「データ」として扱われ、
    SQL構文の一部として解釈されることはない。実際に攻撃が成立する様子・防げる様子の
    デモは `sql_injection_demo.py` を参照。

    実行結果（例）:
        [悪い例] f-string query = SELECT * FROM users WHERE name = 'admin'
        [良い例] parameterized: query="SELECT * FROM users WHERE name = ?" params=('admin',)
    """

    print("\n=== 2. parameterized queries vs string concatenation ===")

    userInput = "admin"

    # [悪い例] 値を直接SQL文字列へ埋め込む。userInputに '; DROP TABLE ... のような文字列が
    # 来ると、SQL文の意味そのものが変わってしまう危険がある（実演: sql_injection_demo.py）。
    badQuery = f"SELECT * FROM users WHERE name = '{userInput}'"
    print(f"[悪い例] f-string query = {badQuery}")

    # [良い例] "?" にはSQLの値としてしか解釈されない形で安全にバインドされる
    goodQuery = "SELECT * FROM users WHERE name = ?"
    print(f'[良い例] parameterized: query="{goodQuery}" params=({userInput!r},)')


def demonstrateTransactionRollback():
    """
    トランザクションのロールバックを確認する。

    目的:
        処理の途中で例外が起きた場合、`rollback()`でそれまでの変更を取り消せることを、
        エラー発生前のINSERTが実際に取り消されることで確認する。

    実行結果（例）:
        before error: count=1
        after rollback: count=0 (エラー前のinsertも取り消されている)
    """

    print("\n=== 3. transaction rollback ===")

    conn = sqlite3.connect(":memory:")
    cursor = conn.cursor()
    cursor.execute("CREATE TABLE accounts (id INTEGER PRIMARY KEY, balance INTEGER NOT NULL CHECK (balance >= 0))")
    conn.commit()

    try:
        cursor.execute("INSERT INTO accounts (balance) VALUES (100)")
        cursor.execute("SELECT COUNT(*) FROM accounts")
        print(f"before error: count={cursor.fetchone()[0]}")

        # CHECK制約違反(balance >= 0)を意図的に起こし、後続処理が失敗する状況を再現する
        cursor.execute("INSERT INTO accounts (balance) VALUES (-50)")
        conn.commit()
    except sqlite3.IntegrityError as e:
        conn.rollback()  # [重要] commitしていない変更（さっきのINSERT(100)含む）を全て取り消す
        print(f"caught IntegrityError: {e}")

    cursor.execute("SELECT COUNT(*) FROM accounts")
    print(f"after rollback: count={cursor.fetchone()[0]} (エラー前のinsertも取り消されている)")

    conn.close()


def demonstrateContextManagerGotcha():
    """
    `with sqlite3.connect(...)`は「コネクションを閉じる」のではないことを確認する。

    [重要] 多くのPythonライブラリでは`with obj:`はリソースの後始末（close等）をするが、
    `sqlite3.Connection`の`with`はトランザクションのcommit/rollbackだけを行い、
    **コネクション自体はcloseされない**。閉じ忘れを防ぐには明示的に`conn.close()`を
    呼ぶか、`with`を抜けた後に別途closeする必要がある。

    実行結果（例）:
        after with-block: connection is still usable (not closed)
        after explicit close(): further use raises ProgrammingError
    """

    print("\n=== 4. context manager gotcha: with does NOT close the connection ===")

    conn = sqlite3.connect(":memory:")
    with conn:
        conn.execute("CREATE TABLE t (x INTEGER)")
        conn.execute("INSERT INTO t VALUES (1)")
    # withブロックを抜けた時点で自動commitされるが、connはまだ開いたまま使える
    result = conn.execute("SELECT x FROM t").fetchall()
    print(f"after with-block: connection is still usable (not closed), rows={result}")

    conn.close()
    try:
        conn.execute("SELECT 1")
    except sqlite3.ProgrammingError as e:
        print(f"after explicit close(): further use raises ProgrammingError: {e}")


def demonstrateRowFactory():
    """
    `row_factory`で、タプルではなく辞書ライクな行アクセスをする。

    実行結果（例）:
        row['name']=temp, row['value']=25.5
    """

    print("\n=== 5. row_factory: dict-like row access ===")

    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row  # [重要] これを設定すると、行を列名でも位置番号でも参照できる
    cursor = conn.cursor()
    cursor.execute("CREATE TABLE sensors (name TEXT, value REAL)")
    cursor.execute("INSERT INTO sensors VALUES ('temp', 25.5)")
    conn.commit()

    cursor.execute("SELECT name, value FROM sensors")
    row = cursor.fetchone()
    print(f"row['name']={row['name']}, row['value']={row['value']}")

    conn.close()


if __name__ == "__main__":
    demonstrateBasicCrud()
    demonstrateParameterizedQueriesVsSqlInjection()
    demonstrateTransactionRollback()
    demonstrateContextManagerGotcha()
    demonstrateRowFactory()
