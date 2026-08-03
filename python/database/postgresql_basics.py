"""
PostgreSQL（`psycopg` 3系）の基本操作サンプル。SQLiteとの違いを中心に確認する。

概要:
    PostgreSQLはサーバーとして常駐するクライアント/サーバー型RDBMS。SQLiteと違い、
    複数プロセスからの同時書き込みや、高度な型・拡張機能に強い。
主な仕様:
    - demonstrateBasicCrud(): 接続、INSERT/SELECT/UPDATE/DELETEの基本。
    - demonstrateReturningClause(): PostgreSQL独自の`RETURNING`句
      （INSERT直後に生成された値をSELECTなしで受け取れる）。
    - demonstrateContextManagerDifferenceFromSqlite(): psycopgの`with`はSQLiteと違い
      接続もクローズする点を確認する。
    - demonstrateTransactionRollback(): エラー時のロールバック。
制限事項:
    - ローカルにPostgreSQLサーバーが起動していることが前提（README.md参照）。

実行方法:
    cd python/database
    pip install "psycopg[binary]"
    # 事前にPostgreSQLサーバーを起動し、DBを作成しておく（README.md参照）
    #   createdb learning_public_sample
    python3 postgresql_basics.py
    # 検証環境: PostgreSQL 16.14 (Homebrew), psycopg 3.3.4 で実行結果を確認済み。
"""

import psycopg

DSN = "dbname=learning_public_sample"  # 接続文字列。ホスト/ユーザー省略時はOS既定値が使われる


def demonstrateBasicCrud():
    """
    接続、テーブル作成、INSERT/SELECT/UPDATE/DELETEの基本を確認する。

    実行結果（例）:
        created table sensors
        select all: [(1, 'temp', 25.5), (2, 'humidity', 60.0)]
        after update: [(1, 'temp', 26.0), (2, 'humidity', 60.0)]
        after delete: [(1, 'temp', 26.0)]
    """

    print("=== 1. basic CRUD ===")

    with psycopg.connect(DSN) as conn:
        with conn.cursor() as cursor:
            cursor.execute("DROP TABLE IF EXISTS sensors")
            cursor.execute(
                """
                CREATE TABLE sensors (
                    id SERIAL PRIMARY KEY,
                    name TEXT NOT NULL,
                    value REAL NOT NULL
                )
                """
            )
            print("created table sensors")

            # [重要] psycopgは "%s" プレースホルダを使う（SQLiteの"?"とは異なる）
            cursor.executemany(
                "INSERT INTO sensors (name, value) VALUES (%s, %s)",
                [("temp", 25.5), ("humidity", 60.0)],
            )

            cursor.execute("SELECT id, name, value FROM sensors ORDER BY id")
            print(f"select all: {cursor.fetchall()}")

            cursor.execute("UPDATE sensors SET value = %s WHERE name = %s", (26.0, "temp"))
            cursor.execute("SELECT id, name, value FROM sensors ORDER BY id")
            print(f"after update: {cursor.fetchall()}")

            cursor.execute("DELETE FROM sensors WHERE name = %s", ("humidity",))
            cursor.execute("SELECT id, name, value FROM sensors ORDER BY id")
            print(f"after delete: {cursor.fetchall()}")
        # withブロックを抜けると自動的にcommitされる（例外が出た場合は自動rollback）


def demonstrateReturningClause():
    """
    PostgreSQL独自の`RETURNING`句で、INSERT直後に生成された値を受け取る。

    目的:
        SQLiteでは「INSERT後に別途SELECTし直す」か`lastrowid`を使うが、PostgreSQLは
        `RETURNING`句でINSERT/UPDATE/DELETE文自体から結果行を直接受け取れる
        （SERIALで自動採番されたidを、追加のクエリ無しでその場で取得できる）。

    実行結果（例。idの値は実行順・既存データに依存）:
        new sensor id (via RETURNING, no extra SELECT needed)=3
    """

    print("\n=== 2. RETURNING clause (PostgreSQL-specific) ===")

    with psycopg.connect(DSN) as conn:
        with conn.cursor() as cursor:
            cursor.execute(
                "INSERT INTO sensors (name, value) VALUES (%s, %s) RETURNING id",
                ("pressure", 101.3),
            )
            newId = cursor.fetchone()[0]
            print(f"new sensor id (via RETURNING, no extra SELECT needed)={newId}")


def demonstrateContextManagerDifferenceFromSqlite():
    """
    psycopgの`with`は、SQLiteと違い**接続自体もクローズする**ことを確認する。

    [重要] `sqlite_basics.py`のdemonstrateContextManagerGotchaと対比。SQLiteの
    `with conn:`はcommit/rollbackだけでcloseはしないが、psycopgの`with psycopg.connect(...):`は
    ブロックを抜けるとcommit（正常時）またはrollback（例外時）を行った上で、
    **接続もクローズする**。同じ「with」という構文でもライブラリによって意味が違う点に注意。

    実行結果（例）:
        after with-block: connection is closed
        using a closed connection raises: ...
    """

    print("\n=== 3. context manager difference from sqlite3 ===")

    conn = psycopg.connect(DSN)
    with conn:
        conn.execute("SELECT 1")
    print(f"after with-block: connection is closed={conn.closed == 1}")

    try:
        conn.execute("SELECT 1")
    except psycopg.OperationalError as e:
        print(f"using a closed connection raises: {e}")


def demonstrateTransactionRollback():
    """
    エラー時のロールバックを確認する（`with`ブロックで自動的に行われる）。

    実行結果（例。countの値はここまでのdemonstrateXxx()実行順に依存する）:
        before error: count=2
        caught IntegrityError (or similar): null value in column "value" of relation "sensors" violates not-null constraint
        after rollback: count=2 (エラー前の変更も取り消されている)
    """

    print("\n=== 4. transaction rollback (automatic via with-block) ===")

    with psycopg.connect(DSN) as conn:
        with conn.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM sensors")
            print(f"before error: count={cursor.fetchone()[0]}")

    try:
        with psycopg.connect(DSN) as conn:
            with conn.cursor() as cursor:
                cursor.execute("INSERT INTO sensors (name, value) VALUES (%s, %s)", ("extra", 1.0))
                # NOT NULL制約違反を意図的に起こす（valueにNoneを渡す）
                cursor.execute("INSERT INTO sensors (name, value) VALUES (%s, %s)", ("broken", None))
    except psycopg.errors.NotNullViolation as e:
        print(f"caught IntegrityError (or similar): {e}")

    with psycopg.connect(DSN) as conn:
        with conn.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM sensors")
            print(f"after rollback: count={cursor.fetchone()[0]} (エラー前の変更も取り消されている)")


if __name__ == "__main__":
    demonstrateBasicCrud()
    demonstrateReturningClause()
    demonstrateContextManagerDifferenceFromSqlite()
    demonstrateTransactionRollback()
