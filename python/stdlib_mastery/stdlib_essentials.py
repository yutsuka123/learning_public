"""
Python標準ライブラリの「使いこなし」サンプル（os / sys / math / re / pathlib /
collections / itertools / functools / json）。

概要:
    サードパーティライブラリを入れずとも、標準ライブラリだけで多くの実務作業が
    こなせることを、頻出パターンを中心に確認する。
主な仕様:
    - demonstrateOsAndPathlib(): ファイル/ディレクトリ操作（os, os.path, pathlib）。
    - demonstrateSys(): コマンドライン引数、モジュール検索パス、終了コード。
    - demonstrateMath(): 数学関数の代表例。
    - demonstrateRegex(): 正規表現によるパターンマッチ・抽出・置換。
    - demonstrateCollections(): Counter, defaultdict, namedtupleの実務パターン。
    - demonstrateItertools(): 組み合わせ生成、グループ化。
    - demonstrateFunctools(): lru_cache（メモ化）、reduce、partial。
    - demonstrateJson(): JSON文字列/ファイルとの相互変換。

実行方法:
    cd python/stdlib_mastery
    python3 stdlib_essentials.py
    # 検証環境: Python 3.14 標準ライブラリで実行結果を確認済み。
"""

import functools
import itertools
import json
import math
import os
import re
import sys
from collections import Counter, defaultdict, namedtuple
from pathlib import Path


def demonstrateOsAndPathlib():
    """
    ファイル/ディレクトリ操作を`os`系と`pathlib`の両方で確認する。

    [重要] `os.path`は文字列としてパスを扱う古くからのAPI、`pathlib.Path`は
    パスをオブジェクトとして扱う新しめのAPI（Python 3.4+）。新規コードでは
    `pathlib`が推奨されることが多いが、既存コードや一部ライブラリは`os.path`前提のため、
    両方読めることが実務では必要。

    実行結果（例。実行するカレントディレクトリにより一部の値は変わる）:
        os.getcwd()=/Users/.../python/stdlib_mastery
        os.path.join example: data/sensors/2026-01-01.csv
        pathlib example: data/sensors/2026-01-01.csv
        suffix=.csv, stem=2026-01-01, parent=data/sensors
    """

    print("=== 1. os / os.path / pathlib ===")

    print(f"os.getcwd()={os.getcwd()}")

    # [重要] os.path.joinは文字列を組み立てるだけ。OS間のパス区切り文字の違い
    # （Windowsは\、macOS/Linuxは/）を吸収してくれる（決して自分で"/"を連結しないこと）。
    osStylePath = os.path.join("data", "sensors", "2026-01-01.csv")
    print(f"os.path.join example: {osStylePath}")

    # pathlib版: Pathオブジェクトは "/" 演算子でパスを連結できる
    pathlibStylePath = Path("data") / "sensors" / "2026-01-01.csv"
    print(f"pathlib example: {pathlibStylePath}")
    print(f"suffix={pathlibStylePath.suffix}, stem={pathlibStylePath.stem}, parent={pathlibStylePath.parent}")


def demonstrateSys():
    """
    コマンドライン引数・モジュール検索パス・実行環境情報を確認する。

    実行結果（例。実際のPythonバージョン・引数は環境依存）:
        sys.argv=['stdlib_essentials.py']
        python version: 3.14.x
        first sys.path entry: ...
    """

    print("\n=== 2. sys: argv, version, path ===")

    print(f"sys.argv={sys.argv}")
    print(f"python version: {sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}")
    print(f"first sys.path entry: {sys.path[0]!r}")


def demonstrateMath():
    """
    数学関数の代表例を確認する。

    実行結果（例）:
        sqrt(2)=1.4142135623730951
        ceil(4.1)=5, floor(4.9)=4
        gcd(12, 18)=6
        pi=3.141592653589793
    """

    print("\n=== 3. math ===")

    print(f"sqrt(2)={math.sqrt(2)}")
    print(f"ceil(4.1)={math.ceil(4.1)}, floor(4.9)={math.floor(4.9)}")
    print(f"gcd(12, 18)={math.gcd(12, 18)}")
    print(f"pi={math.pi}")


def demonstrateRegex():
    """
    正規表現によるパターンマッチ・抽出・置換を確認する。

    実行結果（例）:
        match found: True
        extracted email: alice@example.com
        redacted log: [REDACTED] logged in from 192.168.1.1
    """

    print("\n=== 4. re (regular expressions) ===")

    text = "Contact: alice@example.com"
    emailPattern = r"[\w.+-]+@[\w-]+\.[\w.-]+"

    match = re.search(emailPattern, text)
    print(f"match found: {match is not None}")
    print(f"extracted email: {match.group() if match else None}")

    # [重要] sub()で「パターンにマッチした部分を置き換える」。ログのマスキング等で頻出。
    logLine = "user alice logged in from 192.168.1.1"
    redacted = re.sub(r"user \w+", "[REDACTED]", logLine)
    print(f"redacted log: {redacted}")


def demonstrateCollections():
    """
    `Counter`/`defaultdict`/`namedtuple`の実務パターンを確認する。

    実行結果（例）:
        word counts: Counter({'apple': 3, 'banana': 2, 'cherry': 1})
        most common 2: [('apple', 3), ('banana', 2)]
        grouped by first letter: {'a': ['apple', 'avocado'], 'b': ['banana']}
        point: Point(x=1, y=2)
    """

    print("\n=== 5. collections: Counter, defaultdict, namedtuple ===")

    words = ["apple", "banana", "apple", "cherry", "apple", "banana"]
    counts = Counter(words)
    print(f"word counts: {counts}")
    print(f"most common 2: {counts.most_common(2)}")

    # [重要] defaultdictは「キーが無い時の初期値」を自動生成してくれる。
    # 通常のdictだと `if key not in d: d[key] = []` を毎回書く必要があるのを省略できる。
    groupedByFirstLetter = defaultdict(list)
    for word in ["apple", "banana", "avocado"]:
        groupedByFirstLetter[word[0]].append(word)
    print(f"grouped by first letter: {dict(groupedByFirstLetter)}")

    Point = namedtuple("Point", ["x", "y"])  # フィールド名でアクセスできる軽量なタプル
    point = Point(x=1, y=2)
    print(f"point: {point}")


def demonstrateItertools():
    """
    組み合わせ生成・グループ化の代表例を確認する。

    実行結果（例）:
        combinations of [1,2,3] taken 2: [(1, 2), (1, 3), (2, 3)]
        product of [1,2] and ['a','b']: [(1, 'a'), (1, 'b'), (2, 'a'), (2, 'b')]
        grouped consecutive: [(1, [1, 1]), (2, [2]), (1, [1])]
    """

    print("\n=== 6. itertools: combinations, product, groupby ===")

    combos = list(itertools.combinations([1, 2, 3], 2))
    print(f"combinations of [1,2,3] taken 2: {combos}")

    productResult = list(itertools.product([1, 2], ["a", "b"]))
    print(f"product of [1,2] and ['a','b']: {productResult}")

    # [重要] itertools.groupbyは「連続して同じ値が並んでいる区間」でグループ化する
    # （事前にソートされていないと、離れた同じ値は別グループとして扱われる点に注意）。
    data = [1, 1, 2, 1]
    grouped = [(key, list(group)) for key, group in itertools.groupby(data)]
    print(f"grouped consecutive: {grouped}")


def demonstrateFunctools():
    """
    `lru_cache`（メモ化）・`reduce`・`partial`の代表例を確認する。

    実行結果（例）:
        fibonacci(30)=832040 (lru_cacheで高速化)
        cache info: CacheInfo(hits=28, misses=31, maxsize=None, currsize=31)
        reduce sum [1,2,3,4]=10
        double(5)=10 (partialで引数を1つ固定した関数)
    """

    print("\n=== 7. functools: lru_cache, reduce, partial ===")

    @functools.lru_cache(maxsize=None)
    def fibonacci(n):
        # [重要] lru_cacheが無いと、再帰呼び出しの重複計算で指数関数的に遅くなる。
        # 同じ引数での呼び出し結果をキャッシュすることで、事実上の動的計画法になる。
        if n < 2:
            return n
        return fibonacci(n - 1) + fibonacci(n - 2)

    print(f"fibonacci(30)={fibonacci(30)} (lru_cacheで高速化)")
    print(f"cache info: {fibonacci.cache_info()}")

    total = functools.reduce(lambda acc, x: acc + x, [1, 2, 3, 4])
    print(f"reduce sum [1,2,3,4]={total}")

    def multiply(a, b):
        return a * b

    double = functools.partial(multiply, 2)  # 第1引数を2に固定した新しい関数を作る
    print(f"double(5)={double(5)} (partialで引数を1つ固定した関数)")


def demonstrateJson():
    """
    JSON文字列との相互変換を確認する。

    実行結果（例）:
        json string: {"name": "sensor1", "value": 25.5, "tags": ["temp", "indoor"]}
        parsed back: {'name': 'sensor1', 'value': 25.5, 'tags': ['temp', 'indoor']}
    """

    print("\n=== 8. json ===")

    data = {"name": "sensor1", "value": 25.5, "tags": ["temp", "indoor"]}
    jsonString = json.dumps(data)
    print(f"json string: {jsonString}")

    parsed = json.loads(jsonString)
    print(f"parsed back: {parsed}")


if __name__ == "__main__":
    demonstrateOsAndPathlib()
    demonstrateSys()
    demonstrateMath()
    demonstrateRegex()
    demonstrateCollections()
    demonstrateItertools()
    demonstrateFunctools()
    demonstrateJson()
