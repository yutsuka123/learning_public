"""
Pillow(PIL)による画像処理の基本操作サンプル。

概要:
    画像の生成・リサイズ・クロップ・回転・フィルタ・図形/テキスト描画・フォーマット変換
    といった、Pillowの代表的な操作を確認する。
主な仕様:
    - demonstrateResizeAndCrop(): resize/cropの基本。
    - demonstrateRotateAndFlip(): 回転・反転。
    - demonstrateFilters(): ぼかし/シャープ化等のフィルタ。
    - demonstrateDrawing(): 図形・テキストの描画。
    - demonstrateFormatConversion(): PNG⇔JPEGのフォーマット変換（RGBA→RGB変換の必要性）。

実行方法:
    cd python/image_processing
    pip install Pillow
    python3 pillow_basics.py
    # 検証環境: Pillow 12.3.0 で実行結果を確認済み。
"""

from PIL import Image, ImageDraw, ImageFilter

from sample_image_generator import createSampleImage


def demonstrateResizeAndCrop():
    """
    `resize()`と`crop()`の基本を確認する。

    実行結果（例）:
        original size=(200, 150)
        resized size=(100, 75)
        cropped size=(60, 50)
    """

    print("=== 1. resize / crop ===")
    image = createSampleImage()
    print(f"original size={image.size}")

    resized = image.resize((100, 75))  # 幅高さを直接指定（アスペクト比は自動調整されない）
    print(f"resized size={resized.size}")

    cropped = image.crop((20, 20, 80, 70))  # (left, upper, right, lower)
    print(f"cropped size={cropped.size}")


def demonstrateRotateAndFlip():
    """
    `rotate()`と`transpose()`（反転）の基本を確認する。

    実行結果（例）:
        rotated(45, expand=True) size=(248, 248)
        flipped size=(200, 150)
    """

    print("\n=== 2. rotate / flip ===")
    image = createSampleImage()

    # expand=True: 回転後の四隅がはみ出さないよう、キャンバス自体を広げる
    rotated = image.rotate(45, expand=True)
    print(f"rotated(45, expand=True) size={rotated.size}")

    flipped = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    print(f"flipped size={flipped.size}")


def demonstrateFilters():
    """
    `ImageFilter`によるぼかし/シャープ化を確認する。

    実行結果（例。数値は生成画像に依存）:
        blurred size=(200, 150), sharpened size=(200, 150)
    """

    print("\n=== 3. filters: blur / sharpen ===")
    image = createSampleImage()

    blurred = image.filter(ImageFilter.GaussianBlur(radius=3))
    sharpened = image.filter(ImageFilter.SHARPEN)

    print(f"blurred size={blurred.size}, sharpened size={sharpened.size}")
    print("(注) ぼかし/シャープ化は画素値の平均はあまり変えず、隣接画素との差（輪郭）を変える")


def demonstrateDrawing():
    """
    `ImageDraw`で図形・テキストを描画する。

    実行結果（例）:
        drew rectangle, ellipse, line, text onto a 200x150 canvas
    """

    print("\n=== 4. drawing shapes and text ===")
    image = Image.new("RGB", (200, 150), color=(255, 255, 255))
    draw = ImageDraw.Draw(image)

    draw.rectangle([10, 10, 60, 60], outline=(0, 0, 0), width=2)
    draw.ellipse([80, 10, 130, 60], fill=(0, 200, 0))
    draw.line([10, 100, 190, 100], fill=(200, 0, 0), width=3)
    draw.text((10, 120), "Pillow sample", fill=(0, 0, 0))  # フォント未指定時は組み込みの既定フォントを使う

    print(f"drew rectangle, ellipse, line, text onto a {image.size[0]}x{image.size[1]} canvas")


def demonstrateFormatConversion():
    """
    画像フォーマットの変換（PNG⇔JPEG等）を確認する。

    [重要] JPEGはアルファチャンネル(透過)を持てないため、RGBAのままJPEG保存しようとすると
    `OSError: cannot write mode RGBA as JPEG` になる。保存前に`convert("RGB")`する必要がある。

    実行結果（例）:
        rgba image mode=RGBA
        converted to RGB for JPEG: mode=RGB
    """

    print("\n=== 5. format conversion (PNG <-> JPEG) ===")
    rgbaImage = Image.new("RGBA", (50, 50), color=(255, 0, 0, 128))
    print(f"rgba image mode={rgbaImage.mode}")

    rgbImage = rgbaImage.convert("RGB")  # JPEG保存前に必須
    print(f"converted to RGB for JPEG: mode={rgbImage.mode}")


if __name__ == "__main__":
    demonstrateResizeAndCrop()
    demonstrateRotateAndFlip()
    demonstrateFilters()
    demonstrateDrawing()
    demonstrateFormatConversion()
