"""
DB設計の良い例・悪い例（型選択・制約・インデックス）を、実測で比較するサンプル。

概要:
    「動くだけのスキーマ」と「プロレベルのスキーマ」の違いを、実際にクエリを実行して
    比較する。特にインデックスの有無による検索速度の違いは、件数が少ないと体感しにくいため、
    十分な件数（10,000行）を投入したうえで実測する。
主な仕様:
    - demonstrateTypeAndConstraintChoices(): 型選択・NOT NULL・CHECK制約の要否。
    - demonstrateIndexImpact(): インデックス無し/有りでの検索速度の実測比較。
    - demonstrateForeignKeyEnforcement(): 外部キー制約が「無効な参照」を防ぐことを確認する
      （SQLiteは既定でFOREIGN KEY制約のチェックが無効になっている点に注意）。

実行方法:
    cd python/database
    python3 schema_design_good_vs_bad.py
"""

import sqlite3
import time


def demonstrateTypeAndConstraintChoices():
    """
    型選択・NOT NULL・CHECK制約の要否を、悪い例/良い例で対比する。

    目的:
        [悪い例] 何でも`TEXT`型で受け、制約も付けない設計は、一見柔軟に見えるが
        「価格に文字列が入る」「必須項目が空のまま登録される」といった不正データを
        DB側で防げない。[良い例] 適切な型・制約を付けると、不正データの混入を
        アプリケーションコードのミス（バリデーション漏れ）に依存せずDB側で防げる。

    実行結果（例）:
        [悪い例] inserted invalid price 'not-a-number' without any error
        [良い例] rejected invalid price: ...
        [良い例] rejected NULL name: ...
    """

    print("=== 1. type & constraint choices: bad vs good ===")

    badConn = sqlite3.connect(":memory:")
    # [悪い例] 全部TEXT、制約なし。「動きはする」が、不正な値を止められない。
    badConn.execute("CREATE TABLE products_bad (name TEXT, price TEXT)")
    badConn.execute("INSERT INTO products_bad (name, price) VALUES (?, ?)", ("pen", "not-a-number"))
    badConn.commit()
    row = badConn.execute("SELECT name, price FROM products_bad").fetchone()
    print(f"[悪い例] inserted invalid price {row[1]!r} without any error")
    badConn.close()

    goodConn = sqlite3.connect(":memory:")
    # [良い例] 型を適切にし(価格はNUMERIC)、NOT NULL・CHECK制約で「不正な状態」を
    # そもそもDBに保存できないようにする。
    goodConn.execute(
        """
        CREATE TABLE products_good (
            name TEXT NOT NULL,
            price NUMERIC NOT NULL CHECK (price >= 0)
        )
        """
    )
    try:
        goodConn.execute("INSERT INTO products_good (name, price) VALUES (?, ?)", ("pen", -100))
    except sqlite3.IntegrityError as e:
        print(f"[良い例] rejected invalid price: {e}")

    try:
        goodConn.execute("INSERT INTO products_good (name, price) VALUES (?, ?)", (None, 100))
    except sqlite3.IntegrityError as e:
        print(f"[良い例] rejected NULL name: {e}")
    goodConn.close()


def demonstrateIndexImpact():
    """
    インデックス無し/有りでの検索速度の違いを、10,000行に対する検索で実測する。

    目的:
        「インデックスを張ると速くなる」を体感として理解するため、`EXPLAIN QUERY PLAN`で
        実際のスキャン方式（全件走査 vs インデックス探索）の違いを確認したうえで、
        実行時間も比較する。

    実行結果（例。実行時間は環境依存のため目安。EXPLAIN QUERY PLANの結果は環境非依存）:
        rows inserted: 10000
        [インデックス無し] plan: SCAN sensors_noindex
        [インデックス無し] elapsed for 100 lookups: 0.011843s
        [インデックス有り] plan: SEARCH sensors_indexed USING INDEX idx_sensor_id (sensor_id=?)
        [インデックス有り] elapsed for 100 lookups: 0.000582s
        speedup: ~20.3x
    """

    print("\n=== 2. index impact: full scan vs index search ===")

    conn = sqlite3.connect(":memory:")
    conn.execute("CREATE TABLE sensors_noindex (sensor_id INTEGER, value REAL)")
    conn.execute("CREATE TABLE sensors_indexed (sensor_id INTEGER, value REAL)")
    conn.execute("CREATE INDEX idx_sensor_id ON sensors_indexed (sensor_id)")

    rowCount = 10000
    rows = [(i % 500, float(i)) for i in range(rowCount)]  # sensor_idは0-499を繰り返す(重複あり)
    conn.executemany("INSERT INTO sensors_noindex VALUES (?, ?)", rows)
    conn.executemany("INSERT INTO sensors_indexed VALUES (?, ?)", rows)
    conn.commit()
    print(f"rows inserted: {rowCount}")

    lookupCount = 100

    # EXPLAIN QUERY PLAN で「どういう手段で検索するか」を確認する
    noIndexPlan = conn.execute("EXPLAIN QUERY PLAN SELECT * FROM sensors_noindex WHERE sensor_id = ?", (1,)).fetchall()
    print(f"[インデックス無し] plan: {noIndexPlan[0][3]}")

    start = time.perf_counter()
    for sensorId in range(lookupCount):
        conn.execute("SELECT * FROM sensors_noindex WHERE sensor_id = ?", (sensorId,)).fetchall()
    noIndexElapsed = time.perf_counter() - start
    print(f"[インデックス無し] elapsed for {lookupCount} lookups: {noIndexElapsed:.6f}s")

    indexedPlan = conn.execute("EXPLAIN QUERY PLAN SELECT * FROM sensors_indexed WHERE sensor_id = ?", (1,)).fetchall()
    print(f"[インデックス有り] plan: {indexedPlan[0][3]}")

    start = time.perf_counter()
    for sensorId in range(lookupCount):
        conn.execute("SELECT * FROM sensors_indexed WHERE sensor_id = ?", (sensorId,)).fetchall()
    indexedElapsed = time.perf_counter() - start
    print(f"[インデックス有り] elapsed for {lookupCount} lookups: {indexedElapsed:.6f}s")

    if indexedElapsed > 0:
        print(f"speedup: ~{noIndexElapsed / indexedElapsed:.1f}x")

    conn.close()


def demonstrateForeignKeyEnforcement():
    """
    外部キー制約が「無効な参照」を防ぐことを確認する。

    [重要] SQLiteは後方互換性のため、既定では`FOREIGN KEY`制約のチェックが**無効**になっている。
    `PRAGMA foreign_keys = ON`を明示的に実行しないと、外部キー制約を書いても無視される
    （実際にこのファイル作成時、この設定を忘れて「制約があるのに弾かれない」状態に一度なった）。

    実行結果（例）:
        [PRAGMA未設定] inserted order with non-existent customer_id (制約が効いていない)
        [PRAGMA ON] rejected order with non-existent customer_id: ...
    """

    print("\n=== 3. foreign key enforcement (SQLite requires PRAGMA foreign_keys=ON) ===")

    conn = sqlite3.connect(":memory:")
    # PRAGMAを設定しない場合の挙動を確認する
    conn.execute("CREATE TABLE customers (id INTEGER PRIMARY KEY)")
    conn.execute(
        "CREATE TABLE orders (id INTEGER PRIMARY KEY, customer_id INTEGER REFERENCES customers(id))"
    )
    conn.execute("INSERT INTO orders (customer_id) VALUES (999)")  # 存在しないcustomer_id
    conn.commit()
    print("[PRAGMA未設定] inserted order with non-existent customer_id (制約が効いていない)")
    conn.close()

    conn = sqlite3.connect(":memory:")
    conn.execute("PRAGMA foreign_keys = ON")  # [重要] これを明示しないと外部キー制約は無効
    conn.execute("CREATE TABLE customers (id INTEGER PRIMARY KEY)")
    conn.execute(
        "CREATE TABLE orders (id INTEGER PRIMARY KEY, customer_id INTEGER REFERENCES customers(id))"
    )
    try:
        conn.execute("INSERT INTO orders (customer_id) VALUES (999)")
        conn.commit()
    except sqlite3.IntegrityError as e:
        print(f"[PRAGMA ON] rejected order with non-existent customer_id: {e}")
    conn.close()


if __name__ == "__main__":
    demonstrateTypeAndConstraintChoices()
    demonstrateIndexImpact()
    demonstrateForeignKeyEnforcement()
