"""
NumPyで画像を配列として直接操作するサンプル。

概要:
    画像は本質的に「高さ×幅×チャンネル数」の数値配列でしかない。PillowのImageとnumpy配列を
    相互変換し、スライス/ブールインデックス等のNumPy操作で画像を直接いじる。
主な仕様:
    - demonstrateImageAsArray(): PIL Image <-> numpy配列の変換、shape/dtypeの確認。
    - demonstrateChannelSplit(): RGB各チャンネルの分離。
    - demonstrateThresholding(): 閾値処理（2値化）をブールインデックスで行う。
    - demonstratePixelStatistics(): 平均・最大・最小等の統計。

実行方法:
    cd python/image_processing
    pip install Pillow numpy
    python3 numpy_pixel_manipulation.py
    # 検証環境: numpy 2.5.1 で実行結果を確認済み。
"""

import numpy as np

from sample_image_generator import createSampleImage


def demonstrateImageAsArray():
    """
    PIL Imageをnumpy配列に変換し、shape/dtypeを確認する。

    [重要] numpy配列のshapeは(height, width, channels)の順（Pillowのsize=(width, height)とは
    順序が逆になる点に注意）。

    実行結果（例）:
        array shape=(150, 200, 3), dtype=uint8
        pixel at (0,0)=[30 30 60]
    """

    print("=== 1. image as a numpy array ===")
    image = createSampleImage()
    array = np.array(image)

    print(f"array shape={array.shape}, dtype={array.dtype}")
    print(f"pixel at (0,0)={array[0, 0]}")


def demonstrateChannelSplit():
    """
    RGB各チャンネルをnumpyのスライスで分離する。

    実行結果（例）:
        red channel shape=(150, 200), mean=90.4
        green channel shape=(150, 200), mean=35.9
        blue channel shape=(150, 200), mean=73.4
    """

    print("\n=== 2. channel split via slicing ===")
    array = np.array(createSampleImage())

    redChannel = array[:, :, 0]
    greenChannel = array[:, :, 1]
    blueChannel = array[:, :, 2]

    print(f"red channel shape={redChannel.shape}, mean={redChannel.mean():.1f}")
    print(f"green channel shape={greenChannel.shape}, mean={greenChannel.mean():.1f}")
    print(f"blue channel shape={blueChannel.shape}, mean={blueChannel.mean():.1f}")


def demonstrateThresholding():
    """
    閾値処理（2値化）をブールインデックスで行う。

    目的:
        「明るいピクセルだけ白、それ以外は黒」という2値化を、forループ無しで
        ベクトル演算（ブールインデックス）により一括で行う。

    実行結果（例）:
        grayscale shape=(150, 200), min=33, max=94
        bright pixel ratio (threshold=60)=0.291
    """

    print("\n=== 3. thresholding (binarization) via boolean indexing ===")
    array = np.array(createSampleImage().convert("L"))  # グレースケール化（1チャンネル）

    threshold = 60
    brightMask = array > threshold  # dtype=bool の配列
    brightRatio = brightMask.mean()  # Trueの割合（True=1, False=0として平均を取ると割合になる）

    print(f"grayscale shape={array.shape}, min={array.min()}, max={array.max()}")
    print(f"bright pixel ratio (threshold={threshold})={brightRatio:.3f}")


def demonstratePixelStatistics():
    """
    画像全体の平均・最大・最小等の統計を確認する。

    実行結果（例）:
        mean=66.56, max=220, min=30
    """

    print("\n=== 4. pixel statistics ===")
    array = np.array(createSampleImage())

    print(f"mean={array.mean():.2f}, max={array.max()}, min={array.min()}")


if __name__ == "__main__":
    demonstrateImageAsArray()
    demonstrateChannelSplit()
    demonstrateThresholding()
    demonstratePixelStatistics()
