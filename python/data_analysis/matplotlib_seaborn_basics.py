"""
Matplotlib / Seabornによるデータ可視化の基本操作サンプル。

概要:
    Matplotlibは柔軟だが記述量がやや多いグラフ描画ライブラリ、SeabornはMatplotlibの上に
    構築され、統計的なグラフ（分布、相関等）をより少ないコードで描ける。
主な仕様:
    - demonstrateMatplotlibBasics(): 折れ線グラフ・棒グラフの基本、ファイル保存。
    - demonstrateSeabornStatisticalPlots(): 分布(histplot)、相関(heatmap)、
      カテゴリ別分布(boxplot)。
制限事項:
    - このリポジトリの検証環境はGUI無し(ヘッドレス)のため、`plt.show()`は使わず、
      すべて`savefig()`でPNGファイルに保存して確認する（`matplotlib.use("Agg")`で
      画面表示を伴わないバックエンドを明示している）。

実行方法:
    cd python/data_analysis
    pip install matplotlib seaborn pandas numpy
    python3 matplotlib_seaborn_basics.py
    # 検証環境: matplotlib 3.11.1, seaborn 0.13.2 で実行結果を確認済み。
"""

import matplotlib

# [重要] matplotlib.pyplot をimportする"前"に use("Agg") でバックエンドを確定させる必要がある
# （pyplotはimport時点でバックエンドを選択・固定するため、順序を入れ替えると効果が無い）。
# そのためこの2行はモジュール先頭のimport群より前に置く必要があり、
# 一般的な「importは全部ファイル先頭にまとめる」というLintルール(E402)には意図的に反する。
matplotlib.use("Agg")  # GUI無し環境向けの描画バックエンド。ファイル保存のみ行う場合に使う

import os  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402
import seaborn as sns  # noqa: E402


def demonstrateMatplotlibBasics():
    """
    折れ線グラフ・棒グラフの基本と、ファイル保存を確認する。

    実行結果（例）:
        saved line_chart.png (28643 bytes)
        saved bar_chart.png (9974 bytes)
    """

    print("=== 1. matplotlib basics: line chart, bar chart ===")

    outputDir = os.path.dirname(__file__) or "."

    x = np.linspace(0, 10, 100)
    y = np.sin(x)
    fig, ax = plt.subplots()
    ax.plot(x, y, label="sin(x)")
    ax.set_title("Line chart")
    ax.set_xlabel("x")
    ax.set_ylabel("sin(x)")
    ax.legend()
    linePath = os.path.join(outputDir, "line_chart.png")
    fig.savefig(linePath)
    plt.close(fig)  # [重要] figureを明示的に閉じないと、多数生成した際にメモリを消費し続ける
    print(f"saved line_chart.png ({os.path.getsize(linePath)} bytes)")

    categories = ["temp", "humidity", "pressure"]
    values = [25.5, 60.0, 1013.2]
    fig, ax = plt.subplots()
    ax.bar(categories, values)
    ax.set_title("Bar chart")
    barPath = os.path.join(outputDir, "bar_chart.png")
    fig.savefig(barPath)
    plt.close(fig)
    print(f"saved bar_chart.png ({os.path.getsize(barPath)} bytes)")


def demonstrateSeabornStatisticalPlots():
    """
    Seabornで分布・相関・カテゴリ別分布を確認する。

    実行結果（例）:
        saved histogram.png (25457 bytes)
        saved heatmap.png (20777 bytes)
        saved boxplot.png (12602 bytes)
    """

    print("\n=== 2. seaborn statistical plots: histogram, heatmap, boxplot ===")

    outputDir = os.path.dirname(__file__) or "."
    rng = np.random.default_rng(seed=0)  # [重要] シード固定で再現可能な乱数にする

    # ヒストグラム(分布)
    data = rng.normal(loc=50, scale=10, size=1000)
    fig, ax = plt.subplots()
    sns.histplot(data, kde=True, ax=ax)
    ax.set_title("Histogram with KDE")
    histPath = os.path.join(outputDir, "histogram.png")
    fig.savefig(histPath)
    plt.close(fig)
    print(f"saved histogram.png ({os.path.getsize(histPath)} bytes)")

    # ヒートマップ(相関)
    df = pd.DataFrame(rng.normal(size=(100, 4)), columns=["a", "b", "c", "d"])
    fig, ax = plt.subplots()
    sns.heatmap(df.corr(), annot=True, cmap="coolwarm", ax=ax)
    ax.set_title("Correlation heatmap")
    heatmapPath = os.path.join(outputDir, "heatmap.png")
    fig.savefig(heatmapPath)
    plt.close(fig)
    print(f"saved heatmap.png ({os.path.getsize(heatmapPath)} bytes)")

    # 箱ひげ図(カテゴリ別分布)
    boxDf = pd.DataFrame(
        {
            "category": ["A"] * 50 + ["B"] * 50,
            "value": np.concatenate([rng.normal(40, 5, 50), rng.normal(60, 8, 50)]),
        }
    )
    fig, ax = plt.subplots()
    sns.boxplot(data=boxDf, x="category", y="value", ax=ax)
    ax.set_title("Boxplot by category")
    boxPath = os.path.join(outputDir, "boxplot.png")
    fig.savefig(boxPath)
    plt.close(fig)
    print(f"saved boxplot.png ({os.path.getsize(boxPath)} bytes)")


if __name__ == "__main__":
    demonstrateMatplotlibBasics()
    demonstrateSeabornStatisticalPlots()
