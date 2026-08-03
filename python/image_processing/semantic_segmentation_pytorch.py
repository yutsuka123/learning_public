"""
学習済みモデルによるセマンティックセグメンテーション（torchvision）のサンプル。

概要:
    「セグメンテーション」には大きく2種類ある。
    - 古典的画像分割（`kmeans_color_segmentation.py`参照）: ラベルの意味を知らず、
      似た色/特徴でピクセルをグループ分けするだけ（教師なし）。
    - セマンティックセグメンテーション（本ファイル）: 学習済みニューラルネットワークが
      「このピクセルは"人"、このピクセルは"車"」のように、ピクセル単位でクラスラベルを
      予測する（あらかじめ大量のラベル付き画像で教師あり学習されたモデルを使う）。
主な仕様:
    - demonstrateSemanticSegmentation(): 学習済みLR-ASPP(MobileNetV3)モデルで、
      サンプル画像の各ピクセルにPascal VOCの21クラスのいずれかを割り当てる。
制限事項:
    - 学習済みモデルはPascal VOCの実物体（人・車・猫等）で訓練されているため、
      本ファイルの合成テスト画像（幾何学図形）に対しては意味のある分類結果にはならず、
      ほとんど（または全て）のピクセルが"background"と判定される。それでも
      「前処理→推論→ピクセル単位クラスの取り出し→集計」という一連の流れを確認する
      目的では十分。実物体が写った写真を使えば、より意味のある分布が得られる。
    - 初回実行時、学習済み重み（約12MB）を https://download.pytorch.org からダウンロードする
      （`~/.cache/torch/hub/checkpoints/` にキャッシュされ、2回目以降は再ダウンロードしない）。

実行方法:
    cd python/image_processing
    pip install torch torchvision Pillow numpy
    python3 semantic_segmentation_pytorch.py
    # 検証環境: torch 2.13.0, torchvision 0.28.0 で実行結果を確認済み。
"""

import numpy as np
import torch
from torchvision.models.segmentation import (
    LRASPP_MobileNet_V3_Large_Weights,
    lraspp_mobilenet_v3_large,
)

from sample_image_generator import createSampleImage


def demonstrateSemanticSegmentation():
    """
    学習済みモデルで、画像の各ピクセルにクラスラベルを割り当てる。

    目的:
        「モデルへの入力前処理→推論→ピクセル単位クラスの取り出し」という
        セマンティックセグメンテーションの一連の流れを確認する。

    実行結果（例。前処理で短辺520pxへリサイズされる(縦横比は維持)ため入力サイズは元画像と異なる。
    合成画像のため、全ピクセルがbackgroundと判定されている）:
        model categories: 21 classes (Pascal VOC, incl. background)
        input tensor shape=torch.Size([1, 3, 520, 693])
        output logits shape=torch.Size([1, 21, 520, 693])
        predicted class map shape=(520, 693)
        class distribution: {'__background__': 360360}
    """

    print("=== semantic segmentation (pretrained LR-ASPP MobileNetV3, Pascal VOC) ===")

    weights = LRASPP_MobileNet_V3_Large_Weights.DEFAULT
    model = lraspp_mobilenet_v3_large(weights=weights)
    model.eval()  # [重要] 推論時はdropout/batchnorm等の挙動を訓練時と変えるため、必ずeval()を呼ぶ

    categories = weights.meta["categories"]
    print(f"model categories: {len(categories)} classes (Pascal VOC, incl. background)")

    image = createSampleImage()
    preprocess = weights.transforms()  # モデルに合わせた前処理（正規化等）を取得
    inputTensor = preprocess(image).unsqueeze(0)  # バッチ次元を追加: (C,H,W) -> (1,C,H,W)
    print(f"input tensor shape={inputTensor.shape}")

    with torch.no_grad():  # [重要] 推論のみなら勾配計算は不要。無効化してメモリ/速度を節約する
        output = model(inputTensor)["out"]
    print(f"output logits shape={output.shape}")

    # 各ピクセルについて、21クラスのうち最もスコアが高いクラスIDを選ぶ(argmax)
    predictedClassMap = output.argmax(dim=1).squeeze(0).numpy()
    print(f"predicted class map shape={predictedClassMap.shape}")

    uniqueClasses, counts = np.unique(predictedClassMap, return_counts=True)
    distribution = {categories[int(c)]: int(n) for c, n in zip(uniqueClasses, counts)}
    print(f"class distribution: {distribution}")


if __name__ == "__main__":
    demonstrateSemanticSegmentation()
