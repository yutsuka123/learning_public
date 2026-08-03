"""
pandasによる時系列データ処理の基本操作サンプル。

概要:
    日時をインデックスにしたデータ（センサーの時系列ログ、売上推移等）に対する、
    リサンプリング（集計粒度の変更）、移動平均、前期比較といった典型操作を確認する。
主な仕様:
    - demonstrateDateRangeAndIndexing(): 日時インデックスの作成、日付でのスライス。
    - demonstrateResampling(): 時間粒度の変更（例: 時間別→日別への集約）。
    - demonstrateRollingWindow(): 移動平均（ノイズの多いデータを滑らかにする）。
    - demonstrateShiftAndDiff(): 前期比較（前日比、前週比等）。

実行方法:
    cd python/data_analysis
    pip install pandas numpy
    python3 timeseries_pandas.py
    # 検証環境: pandas 3.0.5 で実行結果を確認済み。
"""

import numpy as np
import pandas as pd


def demonstrateDateRangeAndIndexing():
    """
    日時インデックスの作成と、日付によるスライスを確認する。

    実行結果（例）:
        first 5 rows:
                             value
        2026-01-01 00:00:00     10
        2026-01-01 01:00:00     11
        2026-01-01 02:00:00     12
        2026-01-01 03:00:00     13
        2026-01-01 04:00:00     14
        rows on 2026-01-02: 24
    """

    print("=== 1. date range & datetime indexing ===")

    # 2026-01-01から1時間おきに72時点(3日分)を生成する
    index = pd.date_range(start="2026-01-01", periods=72, freq="h")
    df = pd.DataFrame({"value": range(10, 82)}, index=index)
    print(f"first 5 rows:\n{df.head()}")

    # [重要] 日時インデックスは "YYYY-MM-DD" のような文字列で直接スライスできる
    # （その日付に属する全時刻がまとめて選ばれる）。
    day2 = df.loc["2026-01-02"]
    print(f"rows on 2026-01-02: {len(day2)}")


def demonstrateResampling():
    """
    `resample`で時間粒度を変更する（例: 1時間おき→1日ごとの平均）。

    目的:
        センサーの生ログ（高頻度）を、日次レポート用に集約する典型パターンを確認する。

    実行結果（例）:
        hourly data points: 72
        daily mean:
                    value
        2026-01-01   21.5
        2026-01-02   45.5
        2026-01-03   69.5
    """

    print("\n=== 2. resampling: hourly -> daily ===")

    index = pd.date_range(start="2026-01-01", periods=72, freq="h")
    df = pd.DataFrame({"value": range(10, 82)}, index=index)
    print(f"hourly data points: {len(df)}")

    dailyMean = df.resample("D").mean()
    print(f"daily mean:\n{dailyMean}")


def demonstrateRollingWindow():
    """
    移動平均（rolling window）で、ノイズの多いデータを滑らかにする。

    目的:
        乱数で作ったノイズの多い時系列に対し、3点移動平均を取ると変動が滑らかになることを、
        標準偏差の減少で確認する。

    実行結果（例。乱数シード固定のため再現可能）:
        raw std=8.23
        rolling(3) mean std=5.79 (生データよりばらつきが小さくなる)
    """

    print("\n=== 3. rolling window (moving average) ===")

    rng = np.random.default_rng(seed=0)
    index = pd.date_range(start="2026-01-01", periods=30, freq="D")
    df = pd.DataFrame({"value": rng.normal(loc=50, scale=10, size=30)}, index=index)

    rollingMean = df["value"].rolling(window=3).mean()

    print(f"raw std={df['value'].std():.2f}")
    print(f"rolling(3) mean std={rollingMean.std():.2f} (生データよりばらつきが小さくなる)")


def demonstrateShiftAndDiff():
    """
    `shift`/`diff`で前期比較（前日比等）を確認する。

    目的:
        「前日と比べてどれだけ増減したか」を、ループを書かずにベクトル演算で求める。

    実行結果（例）:
        data:
                    value
        2026-01-01     10
        2026-01-02     15
        2026-01-03     13
        2026-01-04     20
        shifted by 1 day:
                    value
        2026-01-01    NaN
        2026-01-02   10.0
        2026-01-03   15.0
        2026-01-04   13.0
        day-over-day diff:
                    value
        2026-01-01    NaN
        2026-01-02    5.0
        2026-01-03   -2.0
        2026-01-04    7.0
    """

    print("\n=== 4. shift & diff (day-over-day comparison) ===")

    index = pd.date_range(start="2026-01-01", periods=4, freq="D")
    df = pd.DataFrame({"value": [10, 15, 13, 20]}, index=index)
    print(f"data:\n{df}")

    shifted = df.shift(1)  # 1行分（=1日分）後ろへずらす
    print(f"shifted by 1 day:\n{shifted}")

    diff = df.diff()  # df - df.shift(1) と同じ結果を1回で計算する
    print(f"day-over-day diff:\n{diff}")


if __name__ == "__main__":
    demonstrateDateRangeAndIndexing()
    demonstrateResampling()
    demonstrateRollingWindow()
    demonstrateShiftAndDiff()
