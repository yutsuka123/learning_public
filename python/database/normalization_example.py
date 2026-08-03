"""
DB設計ガイド(NORMALIZATION_GUIDE.md)に対応する実行可能サンプル。

概要:
    NORMALIZATION_GUIDE.mdで説明した「悪い例（1枚の巨大テーブル）」で実際に3種類の異常
    （更新時異常/挿入時異常/削除時異常）が起きることと、正規化した「良い例」では
    それらが起きないことを、実際にSQLiteで再現して確認する。
主な仕様:
    - demonstrateUpdateAnomaly(): 悪い例で顧客の住所変更が一部の行にしか反映されない事故を再現。
    - demonstrateInsertionAnomaly(): 悪い例で「注文の無い新規顧客」を自然に登録できないことを確認。
    - demonstrateDeletionAnomaly(): 悪い例で「唯一の注文」を消すと顧客情報も消えることを確認。
    - demonstrateNormalizedDesignAvoidsAnomalies(): 正規化した良い例では、同じ操作が
      安全に行えることを確認する。

実行方法:
    cd python/database
    python3 normalization_example.py
    # 検証環境: Python 3.14 標準ライブラリの sqlite3 で実行結果を確認済み。
"""

import sqlite3


def _setupBadFlatTable(conn):
    """
    [悪い例] NORMALIZATION_GUIDE.md §1 の1枚の巨大テーブルを構築する。
    顧客"alice"が2回注文した状態（＝顧客情報が2行に重複している状態）を再現する。
    """

    conn.execute(
        """
        CREATE TABLE orders_flat (
            order_id INTEGER,
            customer_name TEXT,
            customer_email TEXT,
            customer_address TEXT,
            product_name TEXT,
            product_price NUMERIC,
            quantity INTEGER
        )
        """
    )
    conn.executemany(
        "INSERT INTO orders_flat VALUES (?, ?, ?, ?, ?, ?, ?)",
        [
            (1, "alice", "alice@example.com", "Tokyo", "pen", 100, 2),
            (2, "alice", "alice@example.com", "Tokyo", "notebook", 300, 1),
        ],
    )
    conn.commit()


def demonstrateUpdateAnomaly():
    """
    [悪い例] 顧客の住所変更が、複数行あるうちの1行にしか反映されない事故を再現する。

    実行結果（例）:
        before: [('Tokyo',), ('Tokyo',)]
        after updating only order_id=1 (a common mistake): [('Osaka',), ('Tokyo',)]
        (注) aliceは同一人物のはずなのに、行によって住所が違う矛盾した状態になった
    """

    print("=== 1. update anomaly (bad design) ===")
    conn = sqlite3.connect(":memory:")
    _setupBadFlatTable(conn)

    before = conn.execute(
        "SELECT customer_address FROM orders_flat WHERE customer_name = 'alice'"
    ).fetchall()
    print(f"before: {before}")

    # ありがちなミス: 全行ではなく特定のorder_idの行だけを更新してしまう
    conn.execute("UPDATE orders_flat SET customer_address = 'Osaka' WHERE order_id = 1")
    conn.commit()

    after = conn.execute(
        "SELECT customer_address FROM orders_flat WHERE customer_name = 'alice'"
    ).fetchall()
    print(f"after updating only order_id=1 (a common mistake): {after}")
    print("(注) aliceは同一人物のはずなのに、行によって住所が違う矛盾した状態になった")

    conn.close()


def demonstrateInsertionAnomaly():
    """
    [悪い例] 「注文がまだ無い新規顧客」を自然に登録する方法が無いことを確認する。

    実行結果（例）:
        inserted a row with NULL product info just to register a customer without any order
        row: (3, 'bob', 'bob@example.com', 'Kyoto', None, None, None)
    """

    print("\n=== 2. insertion anomaly (bad design) ===")
    conn = sqlite3.connect(":memory:")
    _setupBadFlatTable(conn)

    # 「注文なしで顧客だけ登録する」自然な方法が無いため、商品情報をNULLにした
    # 意味の分かりにくい行を挿入するしかない（またはそもそも登録を諦める）。
    conn.execute(
        "INSERT INTO orders_flat VALUES (?, ?, ?, ?, ?, ?, ?)",
        (3, "bob", "bob@example.com", "Kyoto", None, None, None),
    )
    conn.commit()
    print("inserted a row with NULL product info just to register a customer without any order")
    row = conn.execute("SELECT * FROM orders_flat WHERE customer_name = 'bob'").fetchone()
    print(f"row: {row}")

    conn.close()


def demonstrateDeletionAnomaly():
    """
    [悪い例] 顧客の「唯一の注文」を削除すると、顧客情報そのものが失われることを確認する。

    実行結果（例）:
        before delete: [('carol', 'carol@example.com', 'Fukuoka')]
        after deleting carol's only order, customer info is GONE: []
    """

    print("\n=== 3. deletion anomaly (bad design) ===")
    conn = sqlite3.connect(":memory:")
    _setupBadFlatTable(conn)
    conn.execute(
        "INSERT INTO orders_flat VALUES (?, ?, ?, ?, ?, ?, ?)",
        (4, "carol", "carol@example.com", "Fukuoka", "eraser", 50, 1),
    )
    conn.commit()

    before = conn.execute(
        "SELECT DISTINCT customer_name, customer_email, customer_address "
        "FROM orders_flat WHERE customer_name = 'carol'"
    ).fetchall()
    print(f"before delete: {before}")

    conn.execute("DELETE FROM orders_flat WHERE order_id = 4")  # carolの唯一の注文を取り消す
    conn.commit()

    after = conn.execute(
        "SELECT DISTINCT customer_name, customer_email, customer_address "
        "FROM orders_flat WHERE customer_name = 'carol'"
    ).fetchall()
    print(f"after deleting carol's only order, customer info is GONE: {after}")

    conn.close()


def _setupNormalizedSchema(conn):
    """
    [良い例] NORMALIZATION_GUIDE.md §2〜3 の正規化済みスキーマ（4テーブル）を構築する。
    """

    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute(
        "CREATE TABLE customers ("
        "customer_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
        "email TEXT UNIQUE NOT NULL, address TEXT)"
    )
    conn.execute(
        "CREATE TABLE products (product_id INTEGER PRIMARY KEY, name TEXT NOT NULL, price NUMERIC NOT NULL)"
    )
    conn.execute(
        "CREATE TABLE orders (order_id INTEGER PRIMARY KEY, "
        "customer_id INTEGER NOT NULL REFERENCES customers(customer_id))"
    )
    conn.execute(
        """
        CREATE TABLE order_items (
            order_id INTEGER NOT NULL REFERENCES orders(order_id),
            product_id INTEGER NOT NULL REFERENCES products(product_id),
            quantity INTEGER NOT NULL CHECK (quantity > 0),
            PRIMARY KEY (order_id, product_id)
        )
        """
    )

    conn.execute("INSERT INTO customers VALUES (1, 'alice', 'alice@example.com', 'Tokyo')")
    conn.execute("INSERT INTO products VALUES (1, 'pen', 100), (2, 'notebook', 300)")
    conn.execute("INSERT INTO orders VALUES (1, 1), (2, 1)")  # aliceの2回の注文
    conn.execute("INSERT INTO order_items VALUES (1, 1, 2), (2, 2, 1)")
    conn.commit()


def demonstrateNormalizedDesignAvoidsAnomalies():
    """
    [良い例] 正規化済みスキーマでは、同じ3つの操作が異常を起こさず安全に行えることを確認する。

    実行結果（例。最後の行のaddressが'Osaka'なのは、この関数内で直前に更新した結果が
    反映されているため。削除しても顧客の行自体は消えない、という点がポイント）:
        [更新時異常なし] alice's address updated once, reflected via 2 orders: [('Osaka',), ('Osaka',)]
        [挿入時異常なし] registered a new customer with zero orders: (2, 'bob', 'bob@example.com', 'Kyoto')
        [削除時異常なし] deleted alice's order 1, but customer info remains: (1, 'alice', 'alice@example.com', 'Osaka')
    """

    print("\n=== 4. normalized design (good) avoids all three anomalies ===")

    conn = sqlite3.connect(":memory:")
    _setupNormalizedSchema(conn)

    # 更新時異常なし: customersテーブルの1行を更新するだけで、JOIN経由で全注文に反映される
    conn.execute("UPDATE customers SET address = 'Osaka' WHERE customer_id = 1")
    conn.commit()
    addresses = conn.execute(
        """
        SELECT c.address FROM orders o
        JOIN customers c ON o.customer_id = c.customer_id
        WHERE c.customer_id = 1
        """
    ).fetchall()
    print(f"[更新時異常なし] alice's address updated once, reflected via {len(addresses)} orders: {addresses}")

    # 挿入時異常なし: 注文が無くても顧客を単独で登録できる
    conn.execute(
        "INSERT INTO customers (customer_id, name, email, address) VALUES (2, 'bob', 'bob@example.com', 'Kyoto')"
    )
    conn.commit()
    bob = conn.execute("SELECT * FROM customers WHERE customer_id = 2").fetchone()
    print(f"[挿入時異常なし] registered a new customer with zero orders: {bob}")

    # 削除時異常なし: 注文(とその明細)を削除しても、顧客情報はcustomersに残ったまま
    conn.execute("DELETE FROM order_items WHERE order_id = 1")
    conn.execute("DELETE FROM orders WHERE order_id = 1")
    conn.commit()
    alice = conn.execute("SELECT * FROM customers WHERE customer_id = 1").fetchone()
    print(f"[削除時異常なし] deleted alice's order 1, but customer info remains: {alice}")

    conn.close()


if __name__ == "__main__":
    demonstrateUpdateAnomaly()
    demonstrateInsertionAnomaly()
    demonstrateDeletionAnomaly()
    demonstrateNormalizedDesignAvoidsAnomalies()
