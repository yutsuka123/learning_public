"""
SQLインジェクションの実演: 文字列結合の危険性とパラメータ化クエリでの防御。

概要:
    [重要] これは防御目的の学習用デモである。このファイル内で使い捨てのSQLite DB(`:memory:`)
    を作り、自分自身のコードに対して典型的な攻撃パターンを実演する。ローカルの許可された
    実演以外の目的で、他者のシステムに対しこの種の入力を試すことは不正アクセスであり、
    絶対に行わないこと。
主な仕様:
    - demonstrateVulnerableLogin(): 文字列結合で作ったクエリが、パスワードを知らなくても
      認証をバイパスされる様子を実演する。
    - demonstrateSafeLogin(): 同じ攻撃入力をパラメータ化クエリに通すと、
      バイパスされないことを確認する。

実行方法:
    cd python/database
    python3 sql_injection_demo.py
"""

import sqlite3


def _setupUsersTable(conn):
    """
    デモ用のusersテーブルを作成し、2件のユーザー（一般ユーザー1件、管理者1件）を投入する。
    """

    conn.execute(
        """
        CREATE TABLE users (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            password TEXT NOT NULL,
            is_admin INTEGER NOT NULL DEFAULT 0
        )
        """
    )
    conn.executemany(
        "INSERT INTO users (name, password, is_admin) VALUES (?, ?, ?)",
        [
            ("alice", "alice_password", 0),
            ("admin", "super_secret_admin_password", 1),
        ],
    )
    conn.commit()


def _vulnerableLogin(conn, name, password):
    """
    [悪い例] 文字列結合でクエリを組み立てる、脆弱なログイン処理。

    引数:
        conn (sqlite3.Connection): DB接続。
        name (str): ユーザー名（未検証の外部入力を想定）。
        password (str): パスワード（未検証の外部入力を想定）。
    戻り値:
        tuple: (fetchoneの結果, 実際に実行されたSQL文字列)
    """

    query = f"SELECT id, name, is_admin FROM users WHERE name = '{name}' AND password = '{password}'"
    cursor = conn.execute(query)
    return cursor.fetchone(), query


def _safeLogin(conn, name, password):
    """
    [良い例] パラメータ化クエリを使う、安全なログイン処理。

    引数:
        conn (sqlite3.Connection): DB接続。
        name (str): ユーザー名（未検証の外部入力を想定）。
        password (str): パスワード（未検証の外部入力を想定）。
    戻り値:
        tuple: (fetchoneの結果, 実行したSQL文字列テンプレート)
    """

    query = "SELECT id, name, is_admin FROM users WHERE name = ? AND password = ?"
    cursor = conn.execute(query, (name, password))
    return cursor.fetchone(), query


def demonstrateVulnerableLogin():
    """
    文字列結合で作ったクエリが、パスワードを知らなくても認証をバイパスされる様子を実演する。

    目的:
        典型的な「認証バイパス」攻撃パターン（ユーザー名欄に`admin' -- `を入れる）を、
        このファイル内の使い捨てDBに対して実際に実行し、パスワード無しでadminとして
        ログインできてしまうことを確認する。

    実行結果（例）:
        normal login (alice, correct password): (1, 'alice', 0)
        attack input: name=admin' -- , password=anything
        executed query: SELECT id, name, is_admin FROM users WHERE name = 'admin' -- ' AND password = 'anything'
        [重大] login result WITHOUT knowing the password: (2, 'admin', 1)
    """

    print("=== 1. vulnerable login: string concatenation ===")

    conn = sqlite3.connect(":memory:")
    _setupUsersTable(conn)

    normalResult, _ = _vulnerableLogin(conn, "alice", "alice_password")
    print(f"normal login (alice, correct password): {normalResult}")

    # 攻撃入力: name欄に `admin' -- ` を入れると、`--`以降がSQLの行コメントとして無視され、
    # 実質的に `WHERE name = 'admin'` だけが有効になり、パスワード条件そのものが消える。
    attackName = "admin' -- "
    attackPassword = "anything"
    print(f"attack input: name={attackName}, password={attackPassword}")

    attackResult, executedQuery = _vulnerableLogin(conn, attackName, attackPassword)
    print(f"executed query: {executedQuery}")
    print(f"[重大] login result WITHOUT knowing the password: {attackResult}")

    conn.close()


def demonstrateSafeLogin():
    """
    同じ攻撃入力をパラメータ化クエリに通すと、バイパスされないことを確認する。

    実行結果（例）:
        attack input via parameterized query: name=admin' -- , password=anything
        login result (None means the attack failed)=None
    """

    print("\n=== 2. safe login: parameterized query ===")

    conn = sqlite3.connect(":memory:")
    _setupUsersTable(conn)

    attackName = "admin' -- "
    attackPassword = "anything"
    print(f"attack input via parameterized query: name={attackName}, password={attackPassword}")

    result, _ = _safeLogin(conn, attackName, attackPassword)
    print(f"login result (None means the attack failed)={result}")
    print("(注) '?' に渡した文字列は、'や--を含んでいてもあくまで「1個のデータ値」として")
    print("     扱われ、SQL構文の一部としては解釈されない。これがパラメータ化クエリが")
    print("     安全な理由（値とSQL構文が明確に分離されているため）。")

    conn.close()


if __name__ == "__main__":
    demonstrateVulnerableLogin()
    demonstrateSafeLogin()
