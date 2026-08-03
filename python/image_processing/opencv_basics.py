"""
OpenCVによる画像処理の基本操作サンプル（グレースケール化、エッジ検出、輪郭検出）。

概要:
    OpenCVはコンピュータビジョン向けの定番ライブラリ。Pillowが「画像編集」寄りなのに対し、
    OpenCVは「画像から情報を抽出する（エッジ/輪郭/特徴点等）」処理に強い。
主な仕様:
    - demonstrateGrayscaleAndBlur(): グレースケール化とガウシアンぼかし。
    - demonstrateEdgeDetection(): Cannyエッジ検出。
    - demonstrateContourDetection(): 輪郭検出（生成画像の矩形/円を検出する）。
制限事項:
    - `opencv-python-headless`を使用（GUI表示`cv2.imshow`は行わない。ヘッドレス環境向け）。

実行方法:
    cd python/image_processing
    pip install opencv-python-headless numpy Pillow
    python3 opencv_basics.py
    # 検証環境: opencv-python-headless (cv2 5.0.0) で実行結果を確認済み。
"""

import cv2
import numpy as np

from sample_image_generator import createSampleImage


def _pilToOpenCv(pilImage):
    """
    PIL Image(RGB) を OpenCV配列(BGR)へ変換する。

    [重要] PillowはRGB順、OpenCVはBGR順が既定という歴史的な違いがある。変換を忘れると
    色が反転して見える（赤と青が入れ替わる）典型的な落とし穴になる。

    引数:
        pilImage (PIL.Image.Image): 変換元のPillow画像。
    戻り値:
        numpy.ndarray: BGR順のOpenCV画像配列。
    """

    rgbArray = np.array(pilImage)
    return cv2.cvtColor(rgbArray, cv2.COLOR_RGB2BGR)


def demonstrateGrayscaleAndBlur():
    """
    グレースケール化とガウシアンぼかしを確認する。

    実行結果（例）:
        bgr shape=(150, 200, 3)
        gray shape=(150, 200)
        blurred shape=(150, 200, 3)
    """

    print("=== 1. grayscale conversion & Gaussian blur ===")
    bgrImage = _pilToOpenCv(createSampleImage())
    print(f"bgr shape={bgrImage.shape}")

    grayImage = cv2.cvtColor(bgrImage, cv2.COLOR_BGR2GRAY)
    print(f"gray shape={grayImage.shape}")

    blurredImage = cv2.GaussianBlur(bgrImage, (5, 5), sigmaX=0)
    print(f"blurred shape={blurredImage.shape}")


def demonstrateEdgeDetection():
    """
    Cannyアルゴリズムでエッジ検出する。

    目的:
        生成画像には赤い矩形・青い円という「はっきりした境界」があるため、
        エッジ検出で境界線上のピクセルが検出できることを、検出ピクセル数で確認する。

    実行結果（例）:
        edges shape=(150, 200), dtype=uint8
        edge pixel count=428
    """

    print("\n=== 2. edge detection (Canny) ===")
    bgrImage = _pilToOpenCv(createSampleImage())
    grayImage = cv2.cvtColor(bgrImage, cv2.COLOR_BGR2GRAY)

    edges = cv2.Canny(grayImage, threshold1=50, threshold2=150)
    edgePixelCount = int(np.count_nonzero(edges))

    print(f"edges shape={edges.shape}, dtype={edges.dtype}")
    print(f"edge pixel count={edgePixelCount}")


def demonstrateContourDetection():
    """
    輪郭検出で、生成画像に描いた矩形・円の形状を検出する。

    目的:
        `cv2.findContours`で「閉じた輪郭」を検出し、生成画像に描いた2つの図形
        （矩形・円）に対応する輪郭が見つかることを確認する。

    [注意] しきい値は生成画像の実際のグレースケール範囲(min=33, max=94、背景=33〜63程度、
    図形=84〜94程度)を確認したうえで75に設定している。しきい値100だと画像全体がその値を
    下回ってしまい、輪郭が1つも見つからない（＝実際にこのファイル作成時に遭遇した）。

    実行結果（例）:
        found 2 contours
          contour[0]: area=2824.0
          contour[1]: area=3000.0
    """

    print("\n=== 3. contour detection ===")
    bgrImage = _pilToOpenCv(createSampleImage())
    grayImage = cv2.cvtColor(bgrImage, cv2.COLOR_BGR2GRAY)

    _thresholdValue, thresholded = cv2.threshold(grayImage, 75, 255, cv2.THRESH_BINARY)
    contours, _hierarchy = cv2.findContours(thresholded, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    print(f"found {len(contours)} contours")
    for i, contour in enumerate(contours):
        area = cv2.contourArea(contour)
        print(f"  contour[{i}]: area={area:.1f}")


if __name__ == "__main__":
    demonstrateGrayscaleAndBlur()
    demonstrateEdgeDetection()
    demonstrateContourDetection()
