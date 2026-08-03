"""
scikit-learnのKMeansによる色ベース画像分割（教師なしセグメンテーション）のサンプル。

概要:
    画像の各ピクセルを「(R,G,B)という3次元の点」とみなし、KMeansクラスタリングで
    似た色同士をグループ分けする。学習済みモデルによる意味理解
    （`semantic_segmentation_pytorch.py`）とは異なり、ラベルの意味（"これは人"等）は
    一切知らない、純粋に色の近さだけで領域を分ける「教師なし」の古典的画像分割手法。
主な仕様:
    - demonstrateKMeansColorSegmentation(): サンプル画像のピクセルをKMeansでK色に
      量子化し、分割結果を確認する。
    - demonstrateElbowMethodForK(): クラスタ数Kをいくつにすべきかの目安
      （エルボー法、慣性(inertia)の減り方を見る）を確認する。

実行方法:
    cd python/image_processing
    pip install scikit-learn numpy Pillow
    python3 kmeans_color_segmentation.py
    # 検証環境: scikit-learn 1.9.0 で実行結果を確認済み。
"""

import numpy as np
from sklearn.cluster import KMeans

from sample_image_generator import createSampleImage


def demonstrateKMeansColorSegmentation():
    """
    KMeansで画像のピクセルを色ベースにK個のグループへ分割する。

    目的:
        生成画像には「背景のグラデーション」「赤い矩形」「青い円」という大まかに
        3つの色領域があるため、K=3のKMeansでおおむねこの3領域に分かれることを、
        クラスタごとの平均色とピクセル数で確認する。

    実行結果（例。KMeansの初期値次第でクラスタの番号(0,1,2)は入れ替わりうるが、
    3クラスタの内訳（背景相当の大きな集団+赤相当+青相当）は変わらない）:
        pixels shape=(30000, 3)
        cluster 0: count=23980, mean_color=[79.6 30.  60. ] (背景のグラデーション相当)
        cluster 1: count=2909, mean_color=[ 40.  80. 220.] (青い円相当)
        cluster 2: count=3111, mean_color=[220.  40.  40.] (赤い矩形相当)
    """

    print("=== 1. KMeans color segmentation (K=3) ===")

    image = createSampleImage()
    array = np.array(image)
    height, width, channels = array.shape

    # KMeansは「N個のサンプル x 特徴量」という2次元配列を期待するため、
    # (height, width, 3) の画像を (height*width, 3) の「ピクセルのリスト」に変形する
    pixels = array.reshape(-1, channels).astype(np.float64)
    print(f"pixels shape={pixels.shape}")

    kmeans = KMeans(n_clusters=3, random_state=0, n_init=10)
    labels = kmeans.fit_predict(pixels)  # 各ピクセルがどのクラスタに属すかのラベル(0,1,2)

    for clusterId in range(3):
        count = int(np.sum(labels == clusterId))
        meanColor = kmeans.cluster_centers_[clusterId]
        print(f"cluster {clusterId}: count={count}, mean_color={np.round(meanColor, 1)}")


def demonstrateElbowMethodForK():
    """
    エルボー法で「クラスタ数Kをいくつにすべきか」の目安を確認する。

    目的:
        Kを1から6まで変えながら、KMeansの`inertia_`（各点とその所属クラスタ中心との
        距離の二乗和。小さいほど「うまく分けられている」）の減り方を見る。改善が急に
        小さくなる「肘(elbow)」のK付近が良い選択とされる（本サンプルは3色構成の
        画像なので、K=3以降で改善が緩やかになる見込み）。

    実行結果（例。値は生成画像・乱数シード依存）:
        K=1: inertia=160081920.5
        K=2: inertia=76450077.1
        K=3: inertia=20827512.2
        K=4: inertia=5840903.6
        K=5: inertia=2144358.2
        K=6: inertia=1231748.9
    """

    print("\n=== 2. elbow method: how to choose K ===")

    array = np.array(createSampleImage())
    pixels = array.reshape(-1, 3).astype(np.float64)

    for k in range(1, 7):
        kmeans = KMeans(n_clusters=k, random_state=0, n_init=10)
        kmeans.fit(pixels)
        print(f"K={k}: inertia={kmeans.inertia_:.1f}")


if __name__ == "__main__":
    demonstrateKMeansColorSegmentation()
    demonstrateElbowMethodForK()
