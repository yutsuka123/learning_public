"""
画像処理サンプル群で共通して使う、合成テスト画像を生成するモジュール。

概要:
    外部の画像ファイルを用意しなくても再現できるように、PillowのImageDrawで
    図形を描いた200x150のテスト画像をその場で生成する。赤い矩形・青い円・
    グラデーション背景を含み、以降のサンプル（クロップ/エッジ検出/輪郭検出等）の
    題材として使う。

実行方法:
    cd python/image_processing
    pip install Pillow
    python3 sample_image_generator.py   # sample_input.png を生成
"""

from PIL import Image, ImageDraw


def createSampleImage(width=200, height=150):
    """
    テスト用の合成画像を生成する。

    引数:
        width (int): 画像の幅（px）。
        height (int): 画像の高さ（px）。
    戻り値:
        PIL.Image.Image: 生成した画像（モード"RGB"）。
    """

    image = Image.new("RGB", (width, height), color=(30, 30, 60))

    # 簡易グラデーション（左から右へ明るくなる）
    pixels = image.load()
    for x in range(width):
        brightness = int(30 + (x / width) * 100)
        for y in range(height):
            _r, g, b = pixels[x, y]
            pixels[x, y] = (brightness, g, b)

    draw = ImageDraw.Draw(image)
    draw.rectangle([20, 20, 80, 70], fill=(220, 40, 40))  # 赤い矩形
    draw.ellipse([120, 60, 180, 120], fill=(40, 80, 220))  # 青い円

    return image


if __name__ == "__main__":
    sample = createSampleImage()
    sample.save("sample_input.png")
    print(f"generated sample_input.png ({sample.size[0]}x{sample.size[1]})")
