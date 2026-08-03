"""
SolidPython2 + OpenSCADによるCADサンプル（CadQueryとの比較）。

概要:
    SolidPythonは、CADの実体を自分で計算するのではなく、OpenSCADという別ソフトが
    解釈する「.scadコード」をPythonの構文で組み立てて出力するラッパー。
    実際の3D形状の計算（レンダリング）はOpenSCAD本体が行うため、**OpenSCADが
    別途インストールされている必要がある**。

[重要・信頼性に関する調査結果]
    macOS版のOpenSCAD（Homebrewの`brew install openscad --cask`で入るもの）は、
    このサンプル作成時点（2026-08-03）で実際に次の警告が出た:

        Warning: openscad has been deprecated because it does not pass the
        macOS Gatekeeper check! It will be disabled on 2026-09-01.

    CLIとしての動作自体はこのファイル作成時に確認できたが、Homebrewでの配布が
    2026-09-01に終了する予定であり、将来的な入手性に懸念がある。
    OpenSCAD自体は2009年から続く実績のあるOSSプロジェクトだが、**この macOS配布パッケージ**に
    問題があるという点に注意。3Dプリンター用途で継続的に使うなら、外部アプリ依存が無い
    `cadquery_basics.py`（CadQuery）を優先することを推奨する。本ファイルは
    「SolidPythonという選択肢がどういうものか」を確認する参考用として提供する。

主な仕様:
    - demonstrateScadCodeGeneration(): PythonのコードからOpenSCADコード(.scad)を生成する。
    - demonstrateRenderToStl(): 生成した.scadを、実際にOpenSCAD CLIでSTLへレンダリングする。

実行方法:
    cd python/cad_3d_printing
    pip install solidpython2
    brew install openscad --cask   # macOS。上記の非推奨警告に注意
    python3 solidpython_openscad_basics.py
    # 検証環境: solid2 (SolidPython2), OpenSCAD 2021.01 (Homebrew cask) で実行結果を確認済み。
"""

import os
import subprocess

from solid2 import cube, cylinder, translate


def createBracketScad():
    """
    CadQuery版と対比しやすいよう、穴あき直方体（ブラケット相当）をSolidPythonで組み立てる。

    戻り値:
        solid2のオブジェクト: `.as_scad()`でOpenSCADコード文字列に変換できる。
    """

    # [重要] SolidPythonでは、Cの構造体組み立てのような「宣言的」な書き方ではなく、
    # cube - cylinder のように「引き算(difference)」でくり抜きを表現する
    # （CadQueryの.hole()のような専用メソッドではなく、CSG演算子`-`がOpenSCADの流儀）。
    box = cube([40, 25, 3])
    hole = translate([20, 12.5, -1])(cylinder(r=2, h=5))
    return box - hole


def demonstrateScadCodeGeneration():
    """
    PythonのコードからOpenSCADコード(.scad)を生成する。

    目的:
        SolidPythonは「Pythonのオブジェクトを組み立てる」だけで、実際の幾何計算は
        まだ行っていない。`.as_scad()`を呼んで初めて、OpenSCADが解釈できるテキストへ
        変換される（この時点でもまだ3D形状の計算はされていない。計算はOpenSCAD側の仕事）。

    実行結果（例）:
        generated SCAD code:
        difference() {
            cube(size = [40, 25, 3]);
            translate(v = [20, 12.5, -1]) {
                cylinder(h = 5, r = 2);
            }
        }
    """

    print("=== 1. generate OpenSCAD code from Python ===")

    bracket = createBracketScad()
    scadCode = bracket.as_scad()
    print(f"generated SCAD code:\n{scadCode}")


def demonstrateRenderToStl():
    """
    生成した.scadファイルを、実際にOpenSCAD CLIでSTLへレンダリングする。

    [重要] ここで初めて実際の3D形状計算（CSG演算の実行、メッシュ生成）が行われる。
    `subprocess`でOpenSCADを外部プロセスとして呼び出す必要がある点が、
    単一のPythonパッケージで完結するCadQueryとの大きな違い。

    実行結果（例。openscadが見つからない/バージョン非対応の場合は例外を捕捉して案内する）:
        wrote bracket_solidpython.scad (105 bytes)
        rendered bracket_solidpython.stl (6209 bytes) via OpenSCAD CLI
    """

    print("\n=== 2. render to STL via OpenSCAD CLI (subprocess) ===")

    outputDir = os.path.dirname(__file__) or "."
    scadPath = os.path.join(outputDir, "bracket_solidpython.scad")
    stlPath = os.path.join(outputDir, "bracket_solidpython.stl")

    bracket = createBracketScad()
    scadCode = bracket.as_scad()
    with open(scadPath, "w", encoding="utf-8") as f:
        f.write(scadCode)
    print(f"wrote bracket_solidpython.scad ({os.path.getsize(scadPath)} bytes)")

    try:
        subprocess.run(
            ["openscad", "-o", stlPath, scadPath],
            check=True,
            capture_output=True,
            text=True,
        )
        print(f"rendered bracket_solidpython.stl ({os.path.getsize(stlPath)} bytes) via OpenSCAD CLI")
    except FileNotFoundError:
        print("[案内] 'openscad' コマンドが見つかりません。")
        print("       macOSの場合: brew install openscad --cask")
        print("       （2026-09-01にHomebrewでの配布が終了予定。ファイル冒頭の注意を参照）")
    except subprocess.CalledProcessError as e:
        print(f"[error] OpenSCADでのレンダリングに失敗しました。stderr={e.stderr}")


if __name__ == "__main__":
    demonstrateScadCodeGeneration()
    demonstrateRenderToStl()
