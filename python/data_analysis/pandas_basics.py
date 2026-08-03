"""
pandasによるデータ分析の基本操作サンプル。

概要:
    表形式データ（DataFrame）の作成、選択/フィルタ、集計(groupby)、結合(merge)、
    欠損値処理、基本統計といった、pandasの代表的な操作を確認する。
主な仕様:
    - demonstrateDataFrameBasics(): DataFrame作成、列選択、行フィルタ。
    - demonstrateGroupByAggregation(): groupbyによるグループ集計。
    - demonstrateMerge(): 2つのDataFrameの結合（SQLのJOINに相当）。
    - demonstrateMissingData(): 欠損値(NaN)の検出・穴埋め・除去。
    - demonstrateDescriptiveStatistics(): 基本統計量の確認。

実行方法:
    cd python/data_analysis
    pip install pandas
    python3 pandas_basics.py
    # 検証環境: pandas 3.0.5 で実行結果を確認済み。
"""

import numpy as np
import pandas as pd


def demonstrateDataFrameBasics():
    """
    DataFrameの作成、列選択、行フィルタの基本を確認する。

    実行結果（例。pandas 3.0以降、文字列列の既定dtypeは`object`ではなく新しい`str`型になった。
    バージョンにより`dtype: object`と表示される場合もある）:
        DataFrame:
            name  age     city
        0  alice   30    Tokyo
        1    bob   25    Osaka
        2  carol   35  Fukuoka
        names column:
        0    alice
        1      bob
        2    carol
        Name: name, dtype: str
        rows where age >= 30:
            name  age     city
        0  alice   30    Tokyo
        2  carol   35  Fukuoka
    """

    print("=== 1. DataFrame basics: creation, column selection, row filtering ===")

    df = pd.DataFrame(
        {
            "name": ["alice", "bob", "carol"],
            "age": [30, 25, 35],
            "city": ["Tokyo", "Osaka", "Fukuoka"],
        }
    )
    print(f"DataFrame:\n{df}")

    print(f"names column:\n{df['name']}")

    # [重要] 列選択は df['col']（1列ならSeries、リストで複数列ならDataFrame）。
    # 行フィルタは「条件式が返すbool配列」でインデックスする（numpyのブールインデックスと同じ発想。
    # python/image_processing/numpy_pixel_manipulation.py のdemonstrateThresholdingを参照）。
    filtered = df[df["age"] >= 30]
    print(f"rows where age >= 30:\n{filtered}")


def demonstrateGroupByAggregation():
    """
    `groupby`によるグループ集計を確認する。SQLの`GROUP BY`に相当する。

    実行結果（例）:
        sales data:
          region product  amount
        0  east    pen      10
        1  east    pen      20
        2  west    pen      15
        3  east    book     30
        4  west    book     25
        total amount by region:
        region
        east    60
        west    40
        Name: amount, dtype: int64
        total amount by region and product:
                       amount
        region product
        east   book        30
               pen         30
        west   book        25
               pen         15
    """

    print("\n=== 2. groupby aggregation (like SQL GROUP BY) ===")

    df = pd.DataFrame(
        {
            "region": ["east", "east", "west", "east", "west"],
            "product": ["pen", "pen", "pen", "book", "book"],
            "amount": [10, 20, 15, 30, 25],
        }
    )
    print(f"sales data:\n{df}")

    byRegion = df.groupby("region")["amount"].sum()
    print(f"total amount by region:\n{byRegion}")

    byRegionAndProduct = df.groupby(["region", "product"])[["amount"]].sum()
    print(f"total amount by region and product:\n{byRegionAndProduct}")


def demonstrateMerge():
    """
    2つのDataFrameを結合する。`python/database/`のJOINと同じ発想。

    目的:
        `orders`と`customers`をSQLのJOINのように結合する例
        （`python/database/NORMALIZATION_GUIDE.md`の正規化されたテーブル構成を、
        pandas側で再現・結合する）。

    実行結果（例）:
        merged (inner join on customer_id):
           order_id  customer_id product   name
        0         1            1     pen  alice
        1         2            1    book  alice
        2         3            2     pen    bob
    """

    print("\n=== 3. merge (like SQL JOIN) ===")

    orders = pd.DataFrame(
        {
            "order_id": [1, 2, 3],
            "customer_id": [1, 1, 2],
            "product": ["pen", "book", "pen"],
        }
    )
    customers = pd.DataFrame({"customer_id": [1, 2], "name": ["alice", "bob"]})

    merged = orders.merge(customers, on="customer_id", how="inner")
    print(f"merged (inner join on customer_id):\n{merged}")


def demonstrateMissingData():
    """
    欠損値(NaN)の検出・穴埋め・除去を確認する。

    実行結果（例）:
        data with missing values:
           value
        0   10.0
        1    NaN
        2   30.0
        3    NaN
        isna:
        0    False
        1     True
        2    False
        3     True
        Name: value, dtype: bool
        filled with mean: [10. 20. 30. 20.]
        dropped NaN rows:
           value
        0   10.0
        2   30.0
    """

    print("\n=== 4. missing data: detect / fill / drop ===")

    df = pd.DataFrame({"value": [10.0, np.nan, 30.0, np.nan]})
    print(f"data with missing values:\n{df}")

    print(f"isna:\n{df['value'].isna()}")

    filled = df["value"].fillna(df["value"].mean())
    print(f"filled with mean: {filled.to_numpy()}")

    dropped = df.dropna()
    print(f"dropped NaN rows:\n{dropped}")


def demonstrateDescriptiveStatistics():
    """
    `describe()`等による基本統計量の確認。

    実行結果（例。describe()の出力書式はpandasのバージョンにより多少変わりうる）:
        count     5.000000
        mean     30.000000
        std      15.811388
        min      10.000000
        25%      20.000000
        50%      30.000000
        75%      40.000000
        max      50.000000
        Name: value, dtype: float64
        correlation:
                  x    value
        x      1.00     1.00
        value  1.00     1.00
    """

    print("\n=== 5. descriptive statistics ===")

    df = pd.DataFrame({"x": [1, 2, 3, 4, 5], "value": [10, 20, 30, 40, 50]})
    print(df["value"].describe())

    print(f"correlation:\n{df.corr().round(2)}")


if __name__ == "__main__":
    demonstrateDataFrameBasics()
    demonstrateGroupByAggregation()
    demonstrateMerge()
    demonstrateMissingData()
    demonstrateDescriptiveStatistics()
