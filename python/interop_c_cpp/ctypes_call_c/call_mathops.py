"""
ctypesでCの共有ライブラリ(mathops)を呼び出すサンプル。

概要:
    `c/structures/struct_pointer_array_deep_dive.c` で学んだポインタ・配列・構造体が、
    Pythonの`ctypes`からどう見えるかを実際に確認する。
主な仕様:
    - demonstrateSimpleFunctionCall(): int引数・int戻り値の単純な呼び出し。
    - demonstrateArrayPointerPassing(): Pythonのlistを、Cの`const double*`として渡す。
    - demonstrateInPlaceMutation(): Cの関数にバッファを渡し、その場で書き換えてもらう
      （＝Cのポインタ経由の副作用が、Python側にもそのまま見える）。
    - demonstrateStructMarshaling(): `ctypes.Structure`でCのstructをPython側から扱う。

実行方法:
    cd python/interop_c_cpp/ctypes_call_c
    cc -shared -fPIC -O2 -Wall -Wextra mathops.c -o libmathops.dylib   # macOS
    python3 call_mathops.py
    # Linuxの場合は -o libmathops.so とし、下記_libPathの拡張子も合わせて読み替える。
"""

import ctypes
import os

_libPath = os.path.join(os.path.dirname(__file__), "libmathops.dylib")
_lib = ctypes.CDLL(_libPath)


# [重要] ctypesはCの関数シグネチャを自動では知らないため、引数型(argtypes)と戻り値型(restype)を
# 明示的に教える必要がある。これを省略すると既定でint扱いされ、doubleやポインタを渡すと
# メモリ破壊のような深刻なバグにつながることがある（Cの「プロトタイプ宣言忘れ」に近い危険さ）。
_lib.addInts.argtypes = [ctypes.c_int, ctypes.c_int]
_lib.addInts.restype = ctypes.c_int

_lib.computeAverage.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.c_int]
_lib.computeAverage.restype = ctypes.c_double

_lib.scaleArrayInPlace.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.c_int, ctypes.c_double]
_lib.scaleArrayInPlace.restype = None


class SensorReading(ctypes.Structure):
    """
    Cの `typedef struct { int id; double value; } SensorReading;` に対応するctypes構造体。

    [重要] `_fields_`はCの構造体のメンバ順序・型と厳密に一致させる必要がある。順序や型が
    違うとオフセットがずれ、誤った値を読むことになる
    （`c/structures/struct_pointer_array_deep_dive.c` §10のパディングの話がここでも関わる。
    このSensorReadingは int(4byte) の直後に double(8byte) が続くため、環境によっては
    intの後に4byteのパディングが入る。ctypesはCコンパイラと同じアライメント規則に
    従ってメモリレイアウトを組み立てるため、通常はこれを意識しなくても正しく動く）。
    """

    _fields_ = [("id", ctypes.c_int), ("value", ctypes.c_double)]


_lib.makeSensorReading.argtypes = [ctypes.c_int, ctypes.c_double]
_lib.makeSensorReading.restype = SensorReading

_lib.sensorReadingValue.argtypes = [SensorReading]
_lib.sensorReadingValue.restype = ctypes.c_double


def demonstrateSimpleFunctionCall():
    """
    int引数・int戻り値の、最も単純なC関数呼び出しを確認する。

    実行結果（例）:
        addInts(2, 3) = 5
    """

    print("=== 1. simple function call: int addInts(int, int) ===")
    result = _lib.addInts(2, 3)
    print(f"addInts(2, 3) = {result}")


def demonstrateArrayPointerPassing():
    """
    Pythonのlistを、Cの`const double*`として渡す。

    目的:
        `(ctypes.c_double * len(pyValues))(*pyValues)` という書き方で、Cの`double[4]`に
        相当する連続メモリ領域をPython側で確保し、そのポインタをCへ渡す。

    実行結果（例）:
        computeAverage([1.0, 2.0, 3.0, 4.0]) = 2.5
    """

    print("\n=== 2. array/pointer passing: double computeAverage(const double*, int) ===")
    pyValues = [1.0, 2.0, 3.0, 4.0]
    cArray = (ctypes.c_double * len(pyValues))(*pyValues)  # Cのdouble[4]に相当する領域を確保
    average = _lib.computeAverage(cArray, len(pyValues))
    print(f"computeAverage({pyValues}) = {average}")


def demonstrateInPlaceMutation():
    """
    Cの関数にバッファを渡し、その場で書き換えてもらう（ポインタ経由の副作用）。

    目的:
        Cの`scaleArrayInPlace`はポインタ越しに配列を直接書き換える。ctypes配列はPython側の
        バッファをそのまま指しているため、C関数呼び出し後、Python側から見ても値が
        変わっていることを確認する（構造体ファイル§3の「ポインタ経由の書き換えは呼び出し元にも
        反映される」という話が、Python-C境界を越えても成り立つことを示す）。

    実行結果（例）:
        before: [1.0, 2.0, 3.0]
        after scaleArrayInPlace(..., factor=10.0): [10.0, 20.0, 30.0]
    """

    print("\n=== 3. in-place mutation via pointer: void scaleArrayInPlace(double*, int, double) ===")
    pyValues = [1.0, 2.0, 3.0]
    cArray = (ctypes.c_double * len(pyValues))(*pyValues)
    print(f"before: {list(cArray)}")
    _lib.scaleArrayInPlace(cArray, len(pyValues), 10.0)
    print(f"after scaleArrayInPlace(..., factor=10.0): {list(cArray)}")


def demonstrateStructMarshaling():
    """
    `ctypes.Structure`でCの構造体をPython側から扱う。

    実行結果（例）:
        reading.id=7, reading.value=25.5
        sensorReadingValue(reading) = 25.5
    """

    print("\n=== 4. struct marshaling: SensorReading (ctypes.Structure) ===")
    reading = _lib.makeSensorReading(7, 25.5)
    print(f"reading.id={reading.id}, reading.value={reading.value}")

    valueViaFunction = _lib.sensorReadingValue(reading)
    print(f"sensorReadingValue(reading) = {valueViaFunction}")


if __name__ == "__main__":
    demonstrateSimpleFunctionCall()
    demonstrateArrayPointerPassing()
    demonstrateInPlaceMutation()
    demonstrateStructMarshaling()
