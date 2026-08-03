# OCR（文字認識）Python サンプル

[重要] このフォルダは、Tesseract OCR（Googleがメンテナンスする、実績のあるオープンソース
OCRエンジン）をPythonから使うサンプルです。

## セットアップ

```sh
brew install tesseract tesseract-lang   # macOS。日本語("jpn")等の言語データも入る
cd python/ocr
pip install -r requirements.txt
```

## 実行方法

```sh
python3 tesseract_ocr_basics.py
```

## 検証時に実際に確認した重要な知見

- **フォントサイズ(解像度)がOCR精度に直結する**: 小さいフォント(10pt)で描画した
  "Hello OCR 123" は "Helloocr 123" に誤認識されたが、同じテキストを28ptで描画すると
  正しく認識された。スキャン書類等を実務でOCRする際も、**解像度を上げる/文字を
  大きく写す**ことが精度改善の基本であることを、実際の誤認識で確認した。
- **日本語のような複雑な文字体系ではフォントの描画品質も影響する**: `lang="jpn"`を
  指定すれば日本語も認識できるが、フォント・サイズ・キャンバスサイズが不十分だと
  一部の文字を誤認識することがある。本サンプルは十分なサイズ(40pt)で正しく
  認識できることを確認済み。

## 検証環境

- macOS, Tesseract 5.5.3 (Homebrew, 通常のCLIフォーミュラでGatekeeper関連の問題は無い),
  pytesseract 5.5.3
- 全関数、実行して認識結果を確認済み（英数字・日本語とも）。

## 関連ファイル

- 画像処理全般（Pillow/OpenCV/numpy）: `../image_processing/`
