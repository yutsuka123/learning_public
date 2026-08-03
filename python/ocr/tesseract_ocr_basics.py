"""
Tesseract OCR (pytesseract) による文字認識の基本操作サンプル。

概要:
    TesseractはGoogleがメンテナンスする、実績のあるオープンソースOCRエンジン。
    `pytesseract`はそのPythonラッパー。外部の画像ファイルを用意しなくても再現できるよう、
    Pillowで文字を描画した画像をその場で生成し、それをOCRにかけて確認する。
主な仕様:
    - demonstrateBasicOcr(): 英数字の基本的な文字認識。
    - demonstrateFontSizeEffect(): フォントサイズ（画像の解像度）がOCR精度に
      どれだけ影響するかを、実際に小さい/大きいフォントで比較する。
    - demonstrateJapaneseOcr(): 日本語の文字認識（`jpn`言語データを使用）。
制限事項:
    - [重要] Tesseract本体（OS側のバイナリ）が別途インストールされている必要がある
      （`brew install tesseract tesseract-lang`）。`pytesseract`はあくまでラッパーであり、
      OCR自体はTesseract本体が行う。

実行方法:
    cd python/ocr
    brew install tesseract tesseract-lang   # macOS。日本語を使うにはtesseract-langも必要
    pip install pytesseract Pillow
    python3 tesseract_ocr_basics.py
    # 検証環境: Tesseract 5.5.3 (Homebrew), pytesseract 5.5.3 で実行結果を確認済み。
"""

from PIL import Image, ImageDraw, ImageFont

import pytesseract


def _renderTextImage(text, fontSize, canvasSize, fontPath=None):
    """
    指定テキストを描画した画像を生成する（OCRの入力に使う）。

    引数:
        text (str): 描画する文字列。
        fontSize (int): フォントサイズ(pt)。
        canvasSize (tuple[int, int]): 画像サイズ(幅, 高さ)。
        fontPath (str | None): TrueTypeフォントのパス。Noneなら簡易な既定フォントを使う。
    戻り値:
        PIL.Image.Image: 白背景に黒文字を描画した画像。
    """

    image = Image.new("RGB", canvasSize, color="white")
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(fontPath, fontSize) if fontPath else ImageFont.load_default(size=fontSize)
    draw.text((10, canvasSize[1] // 4), text, fill="black", font=font)
    return image


def demonstrateBasicOcr():
    """
    英数字の基本的な文字認識を確認する。

    実行結果（例）:
        recognized text: 'Hello OCR 123\\n'
    """

    print("=== 1. basic OCR (English) ===")

    image = _renderTextImage("Hello OCR 123", fontSize=28, canvasSize=(400, 100))
    recognizedText = pytesseract.image_to_string(image)
    print(f"recognized text: {recognizedText!r}")


def demonstrateFontSizeEffect():
    """
    フォントサイズ（画像の解像度）がOCR精度にどれだけ影響するかを実際に比較する。

    [重要] このファイル作成時、Pillowの既定フォントで小さいサイズ(10pt相当)のまま
    OCRにかけたところ、"Hello OCR 123" が "Helloocr 123"（"OCR"が小文字化され、
    単語間のスペースも1つ失われる）に誤認識される実例に実際に遭遇した。
    フォントサイズを28ptまで上げると正しく認識された。これは「OCR対象の画像は、
    文字が十分な解像度で写っていることが精度に直結する」という、スキャン書類等を
    実務でOCRする際にも重要な教訓を示している。

    実行結果（例。小さいフォントでの誤認識は、Tesseractのバージョンや
    フォントレンダリング環境により多少結果が変わりうる）:
        small font (10pt) recognized: 'Helloocr 123\\n' (誤認識の例: OCRが小文字化、スペース消失)
        large font (28pt) recognized: 'Hello OCR 123\\n' (正しく認識)
    """

    print("\n=== 2. font size (resolution) effect on OCR accuracy ===")

    smallFontImage = _renderTextImage("Hello OCR 123", fontSize=10, canvasSize=(300, 80))
    smallResult = pytesseract.image_to_string(smallFontImage)
    print(f"small font (10pt) recognized: {smallResult!r} (誤認識の例)")

    largeFontImage = _renderTextImage("Hello OCR 123", fontSize=28, canvasSize=(400, 100))
    largeResult = pytesseract.image_to_string(largeFontImage)
    print(f"large font (28pt) recognized: {largeResult!r} (正しく認識)")


def demonstrateJapaneseOcr():
    """
    日本語の文字認識を確認する（`lang="jpn"`を指定する）。

    [重要] 日本語のようなCJK文字は、フォントによって字形の描画品質が変わり、
    OCR精度に直結する。このファイル作成時、標準的な"Arial Unicode.ttf"を使い、
    十分なフォントサイズ(40pt)・キャンバスサイズで描画することで正しく認識できることを
    確認した（小さめのフォント/キャンバスでは一部の文字が誤認識されることも実際に確認した）。

    実行結果（例。使用フォント・環境により結果が変わりうる）:
        recognized (jpn): 'こんにちは世界\\n'
    """

    print("\n=== 3. Japanese OCR (lang='jpn') ===")

    fontPath = "/Library/Fonts/Arial Unicode.ttf"  # macOS標準搭載のCJK対応フォント
    try:
        image = _renderTextImage("こんにちは世界", fontSize=40, canvasSize=(500, 120), fontPath=fontPath)
    except OSError:
        print(f"[案内] フォントが見つかりません: {fontPath}")
        print("       日本語が描画できるTrueTypeフォントのパスに書き換えてください。")
        return

    recognizedText = pytesseract.image_to_string(image, lang="jpn")
    print(f"recognized (jpn): {recognizedText!r}")


if __name__ == "__main__":
    demonstrateBasicOcr()
    demonstrateFontSizeEffect()
    demonstrateJapaneseOcr()
