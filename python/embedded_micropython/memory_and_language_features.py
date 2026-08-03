"""
MicroPython のメモリ管理・言語機能サンプル（CPythonとの違い中心）。

概要:
    組み込み機器はRAMが数十KB〜数百KB程度と非常に限られるため、MicroPythonには
    「メモリが有限であることを強く意識させる」機能がいくつもある。CPython（PC上の
    通常のPython）ではあまり気にしない`gc`の手動制御や、コンパイル時定数への畳み込みが
    その代表例。
主な仕様:
    - demonstrateGarbageCollection(): `gc.mem_free()`/`gc.collect()`でメモリ使用量を
      直接観測する。
    - demonstrateConstFolding(): `micropython.const()`でRAMを消費しない定数を作る。
    - demonstrateMemoryInfo(): `micropython.mem_info()`でヒープの詳細を見る。
    - demonstrateCPythonDifferences(): その他のCPythonとの代表的な違いを一覧で示す。

実行方法:
    cd python/embedded_micropython
    micropython memory_and_language_features.py
    # 検証環境: micropython 1.28.0 (unix port, Homebrew) で実行結果を確認済み。
    # [注意] 具体的なバイト数はビルド/実行環境によって変わるため、実行結果の数値例は
    # 「傾向」を示すものとして読むこと（このリポジトリの検証環境での実測値を記載している）。
"""

import gc
import micropython


def demonstrateGarbageCollection():
    """
    `gc`モジュールでメモリ使用量を直接観測する。

    目的:
        CPythonでも`gc`モジュールはあるが、日常的に使うことは少ない。MicroPythonでは
        「今どれだけ空きメモリがあるか」を把握し、必要なら明示的に`gc.collect()`を
        呼ぶことが、長時間動作する組み込み機器では現実的な選択になる
        （参照が絡まって自動回収されにくい構造を早めに断ち切る、等）。

    実行結果（例。実際のバイト数は実行のたびに多少変動する。傾向として
    「確保後は減る」「delete+collectで(確保前と同じか、それ以上に)戻る」ことを確認する）:
        mem_free before alloc: 2063872
        mem_free after alloc:  2055648 (delta=8224)
        mem_free after del+collect: 2067424
    """

    print("=== 1. gc: observe free memory directly ===")

    before = gc.mem_free()
    print(f"mem_free before alloc: {before}")

    bigList = [0] * 1000  # ヒープにそれなりのサイズのリストを確保する
    afterAlloc = gc.mem_free()
    print(f"mem_free after alloc:  {afterAlloc} (delta={before - afterAlloc})")

    del bigList
    gc.collect()  # [重要] 参照を消しただけでは即座に回収されるとは限らないため、明示的に回収する
    afterCollect = gc.mem_free()
    print(f"mem_free after del+collect: {afterCollect}")


# [重要] micropython.const()で作った定数は、コンパイル時に「その値そのもの」へ置き換えられる
# （Cのプリプロセッサマクロに近い）。通常のモジュールレベル変数と違い、実行時にRAM上へ
# 変数として保持され続けることがない（＝メモリを食わない）。ハードウェアのレジスタアドレスや
# ピン番号など「実行中に変わらない値」はconst()にするのが定石。
_SENSOR_COUNT = micropython.const(4)
_MAX_RETRY = micropython.const(3)


def demonstrateConstFolding():
    """
    `micropython.const()`によるコンパイル時定数を確認する。

    実行結果（例）:
        _SENSOR_COUNT=4, _MAX_RETRY=3
    """

    print("\n=== 2. micropython.const(): compile-time constant folding ===")
    print(f"_SENSOR_COUNT={_SENSOR_COUNT}, _MAX_RETRY={_MAX_RETRY}")
    print("(参考) 通常のモジュール変数と違い、const()の値は実行時にRAM上の変数として")
    print("保持され続けない。ピン番号やレジスタアドレス等の不変値に使うのが定石。")


def demonstrateMemoryInfo():
    """
    `micropython.mem_info()`でヒープの詳細情報を出力する。

    [注意] 出力は標準出力に直接書かれる形式（戻り値はNone）で、環境によって書式が異なる。
    ここでは「呼べること」「何が分かるか」を確認する目的で使う。
    """

    print("\n=== 3. micropython.mem_info(): heap details ===")
    micropython.mem_info()


def demonstrateCPythonDifferences():
    """
    MicroPythonとCPythonの代表的な違いを一覧で示す。
    """

    print("\n=== 4. MicroPython vs CPython: key differences ===")
    print("[標準ライブラリ] CPython: pandas等の大規模ライブラリも豊富")
    print("                 MicroPython: os/sys/socket等の薄いサブセット + machine(HW制御)")
    print("[メモリ管理]     CPython: GCはほぼ意識しない")
    print("                 MicroPython: gc.collect()を明示的に呼ぶことがある、")
    print("                 micropython.const()でRAM消費を避ける")
    print("[整数の扱い]     CPython: intは常に多倍長（メモリの許す限り無限精度）")
    print("                 MicroPython: 多くのポートで小さい整数は特別扱い(高速)だが、")
    print("                 大きい整数も多倍長に対応（ビルド設定に依存する場合がある）")
    print("[実行形態]       CPython: .pyファイルを毎回コンパイルするか.pycキャッシュ")
    print("                 MicroPython: 事前コンパイルした.mpy(フローズンバイトコード)を")
    print("                 フラッシュに書き込み、起動を高速化できる")
    print("[移植性]         CPython: OS上でほぼ同じ標準ライブラリが使える")
    print("                 MicroPython: 「ポート」(unix/esp32/rp2等)ごとに使えるモジュールが")
    print("                 異なる。本ファイルはunixポートで検証しているが、`machine`モジュールは")
    print("                 unixポートではほぼ空（実機ポートでのみPin等が実装される）")


if __name__ == "__main__":
    demonstrateGarbageCollection()
    demonstrateConstFolding()
    demonstrateMemoryInfo()
    demonstrateCPythonDifferences()
