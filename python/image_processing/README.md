# 画像処理 Python サンプル

[重要] このフォルダは、画像処理の代表的なライブラリ・手法を実際に動かして確認するためのものです。
外部の画像ファイルを用意しなくても再現できるよう、`sample_image_generator.py`が合成テスト画像
（赤い矩形・青い円・グラデーション背景を含む200x150画像）をその場で生成し、他のファイルはそれを使う。

## ファイル構成

| ファイル | 内容 | ライブラリ |
|---|---|---|
| `sample_image_generator.py` | 他ファイル共通の合成テスト画像を生成 | Pillow |
| `pillow_basics.py` | リサイズ/クロップ/回転/フィルタ/描画/フォーマット変換 | Pillow |
| `numpy_pixel_manipulation.py` | 画像をnumpy配列として直接操作（チャンネル分離/2値化/統計） | numpy, Pillow |
| `opencv_basics.py` | グレースケール化/ぼかし/エッジ検出(Canny)/輪郭検出 | opencv-python-headless |
| `kmeans_color_segmentation.py` | KMeansによる色ベース画像分割（教師なしセグメンテーション）、エルボー法 | scikit-learn |
| `semantic_segmentation_pytorch.py` | 学習済みモデルによるセマンティックセグメンテーション | torch, torchvision |

## 「セグメンテーション」2種類の違い

- **`kmeans_color_segmentation.py`**（教師なし・古典的）: ピクセルの色が近いものをグループ分けする
  だけで、「何が写っているか」は一切理解しない。ラベル付きデータが無くても使える。
- **`semantic_segmentation_pytorch.py`**（教師あり・ディープラーニング）: 大量のラベル付き画像で
  事前学習されたニューラルネットワークが、ピクセル単位で「これは人」「これは車」等のクラスを
  予測する。本フォルダの合成画像はPascal VOCの実物体と似ていないため、意味のある分類結果には
  ならない（ほぼ全ピクセルが"background"）が、推論の一連の流れ（前処理→推論→クラス取り出し）を
  確認する目的では十分。

## セットアップ

```sh
cd python/image_processing
python3 -m venv .venv   # 未作成なら（python/.venv-examples を共用してもよい）
source .venv/bin/activate
pip install -r requirements.txt
```

## 実行方法

```sh
python3 sample_image_generator.py   # sample_input.png を生成（他ファイルは内部で毎回生成するため必須ではない）
python3 pillow_basics.py
python3 numpy_pixel_manipulation.py
python3 opencv_basics.py
python3 kmeans_color_segmentation.py
python3 semantic_segmentation_pytorch.py   # 初回のみ学習済み重み(約12MB)をダウンロード
```

## 検証時に実際に踏んだ問題（学習メモ）

- **しきい値の決め打ちは危険**: `opencv_basics.py`の輪郭検出で、当初しきい値100で二値化していたが、
  生成画像のグレースケール値は最大94までしかなく、画像全体が黒になり輪郭が0件になった。
  `array.min()`/`array.max()`で実際の値域を確認してから閾値を決める習慣の重要性を、
  実際にハマって確認した（`numpy_pixel_manipulation.py`のしきい値も同じ理由で60に調整済み）。
- **前処理でサイズが変わる**: `semantic_segmentation_pytorch.py`で`weights.transforms()`を使うと、
  元画像(200x150)が短辺520pxへリサイズ（縦横比維持、693x520）されてからモデルに入る。
  モデルの前処理は「元画像のサイズをそのまま使う」とは限らない点に注意が必要。

## 検証環境

- macOS, Python 3.14, Pillow 12.3.0, numpy 2.5.1, opencv-python-headless (cv2 5.0.0),
  scikit-learn 1.9.0, torch 2.13.0, torchvision 0.28.0
- 全ファイル、実行して出力を確認済み（各ファイルのdocstring内「実行結果（例）」は実測値）。

## 関連ファイル

- 既存のPyTorchサンプル: `../examples/pytorch_practice.py`, `../pytorch_sample.py`
