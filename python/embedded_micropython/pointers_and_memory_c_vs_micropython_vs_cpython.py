"""
「ポインタ的なこと」「メモリ確保・解放」を、C / CPython / MicroPython の3者で比較するサンプル。

概要:
    Cは`malloc`/`free`とポインタで自分でメモリを管理する。CPythonはほぼ完全に
    自動化されておりポインタ相当のものを意識することはまず無い。MicroPythonはその中間で、
    「基本は自動（GC）だが、必要ならuctypes/memoryview/machine.memNのような
    “素のメモリへ手を伸ばす”機能も用意されている」という位置づけになる。
    C側の対応する話は `c/structures/struct_pointer_array_deep_dive.c` を参照
    （構造体・ポインタ・配列・ビットフィールド・パディングを深掘りしたファイル）。
主な仕様:
    - demonstrateMemoryModelComparison(): 3者のメモリモデルの違いを一覧で示す。
    - demonstrateUctypesStruct(): `uctypes`でバイト列にC構造体風のレイアウトを被せる。
    - demonstrateMemoryviewAliasing(): `memoryview`で「コピーせず同じメモリを指す」ことを確認する。
    - demonstrateArrayModule(): `array`モジュールで型付きの連続メモリを扱う。
    - explainMachineMemDirectAccess(): `machine.mem8/16/32`（実メモリ番地への直接アクセス）を
      解説する（実機必須のため実行はしない）。

実行方法:
    cd python/embedded_micropython
    micropython pointers_and_memory_c_vs_micropython_vs_cpython.py
    # 検証環境: micropython 1.28.0 (unix port, Homebrew) で実行結果を確認済み
    # （explainMachineMemDirectAccessを除く。理由は関数のdocstring参照）。
"""

import uctypes


def demonstrateMemoryModelComparison():
    """
    C / CPython / MicroPython のメモリモデルの違いを一覧で示す。
    """

    print("=== 1. memory model: C vs CPython vs MicroPython ===")
    print("[確保/解放]     C: malloc/freeを自分で呼ぶ。忘れる/二重に呼ぶとバグになる")
    print("                CPython: ほぼ全自動（参照カウント+循環GC）。開発者は意識しない")
    print("                MicroPython: 自動（mark-and-sweep GC）だが、RAMが少ないため")
    print("                gc.collect()の明示呼び出しやmicropython.const()で意識することがある")
    print("[ポインタ]      C: 生ポインタで直接アドレスを読み書きできる（危険だが柔軟）")
    print("                CPython: ポインタ相当の概念はユーザーコードに露出しない")
    print("                MicroPython: 通常は不要だが、uctypes/memoryview/machine.memN等の")
    print("                「エスケープハッチ」でポインタに近い操作ができる（下記デモ参照）")
    print("[典型的バグ]    C: use-after-free、二重free、バッファオーバーラン、境界外UB")
    print("                CPython: （メモリ安全性由来のバグはまず起きない。ロジックバグが主）")
    print("                MicroPython: メモリ不足によるMemoryError、断片化。生ポインタ系")
    print("                機能を使った場合はCに近いリスクも自分で背負うことになる")


def demonstrateUctypesStruct():
    """
    `uctypes`で、生のバイト列(bytearray)にC構造体のようなフィールドレイアウトを被せる。

    目的:
        `c/structures/struct_pointer_array_deep_dive.c` で見た「構造体はメモリ上の
        決まったオフセットにフィールドが並んだもの」という考え方そのものを、Pythonから
        直接体験する。`uctypes.struct(addressof(buf), layout, ...)`は、`buf`の実体を
        コピーせず、指定レイアウトの「窓」として扱う（Cで`(struct Foo*)ptr`と
        キャストするのに近い）。

    実行結果（例）:
        buf raw bytes: b'\\x01\\xe8\\x03'
        sensor.flag=1 sensor.count=1000
    """

    print("\n=== 2. uctypes: C-struct-like layout over a bytearray ===")

    # Cでいう `struct { uint8_t flag; uint16_t count; }` 相当のレイアウトを定義する。
    # `| 0` `| 1` はバイトオフセット（c/structures...cのoffsetofに相当する情報を、
    # ここでは自分で指定する）。
    sensorLayout = {
        "flag": uctypes.UINT8 | 0,
        "count": uctypes.UINT16 | 1,
    }

    buf = bytearray(3)
    sensor = uctypes.struct(uctypes.addressof(buf), sensorLayout, uctypes.LITTLE_ENDIAN)
    sensor.flag = 1
    sensor.count = 1000

    print(f"buf raw bytes: {bytes(buf)}")
    print(f"sensor.flag={sensor.flag} sensor.count={sensor.count}")
    print("(注) sensorはbufのメモリをそのまま指しているだけで、コピーは作られない")
    print("     ＝Cで buf を struct Sensor* にキャストして読み書きするのと同じ発想")


def demonstrateMemoryviewAliasing():
    """
    `memoryview`で「コピーせず、同じメモリの一部を指す別名」を作る（ポインタ+オフセットに近い）。

    目的:
        Cで`int *p = &arr[1];`とすると、`*p`を書き換えれば`arr[1]`も変わる
        （同じメモリを指しているため）。Pythonの通常のスライス`buf[1:4]`は**コピー**を
        作るが、`memoryview(buf)[1:4]`は**コピーしない**「窓」になる。これを実際に
        書き換えて、元のbufが変化することを確認する。

    実行結果（例）:
        before: [10, 20, 30, 40, 50]
        after via view[0]=99: [10, 99, 30, 40, 50]
    """

    print("\n=== 3. memoryview: alias into existing memory (no copy) ===")

    buf = bytearray([10, 20, 30, 40, 50])
    view = memoryview(buf)[1:4]  # bufをコピーせず、その一部を指す

    print(f"before: {list(buf)}")
    # [重要] view=memoryview(buf)[1:4] は「bufのインデックス1」から数え始める窓。
    # そのため view[0] は buf[0] ではなく buf[1] を指す（Cで int *p=&arr[1]; とした時、
    # p[0]がarr[1]を指すのと同じ理屈）。だから書き換わるのは「2番目の要素」になる。
    view[0] = 99  # viewを書き換えると、bufも変わる（同じメモリを指しているため）
    print(f"after via view[0]=99: {list(buf)}")
    print("(注) 通常のスライス buf[1:4] はコピーを作るため、書き換えても元のbufは変わらない。")
    print("     memoryviewは『コピーしない』点がポインタ的な性質そのもの。")


def demonstrateArrayModule():
    """
    `array`モジュールで型付きの連続メモリ（Cの配列に近いもの）を扱う。

    目的:
        Pythonの`list`は「何でも入る代わりに1要素ごとにオブジェクトへの参照を持つ」ため、
        メモリオーバーヘッドが大きい。`array.array('i', [...])`は、Cの`int[]`のように
        「型が固定された値そのもの」を連続領域に詰めるため、メモリ効率が良い
        （センサーの生データを大量に貯める用途等で有効）。

    実行結果（例）:
        arr=[1, 2, 3]
    """

    print("\n=== 4. array module: C-like contiguous typed storage ===")

    import array

    arr = array.array("i", [1, 2, 3])  # 'i' = signed int。Cの int[] に近い連続メモリ
    print(f"arr={list(arr)}")
    print("(注) list([1,2,3])と違い、各要素は『intそのもの』が連続して並ぶ。")
    print("     大量のセンサー値を貯めるときlistよりメモリを節約できる。")


def explainMachineMemDirectAccess():
    """
    `machine.mem8`/`mem16`/`mem32`（実メモリ番地への直接アクセス）を解説する。

    [重要・未実行] これはCの `*(volatile uint32_t*)0x3FF44004 = value;` のような
    「絶対アドレスへの直接読み書き」に相当する、最も生ポインタに近い機能。
    ESP32等ではペリフェラルのレジスタを直接叩く際に使われることがある
    （通常は`machine.Pin`等の高レベルAPIで十分で、meme8/16/32が必要になる場面は稀）。

    このセッションでは実機が無いため実行していない。unixポートで任意アドレスへ
    アクセスすると、実在しないメモリ領域に触れてクラッシュする恐れがあるため、
    安全のためコードとしても実行せず、解説のみに留める。

    参考コード（実機専用。実際のレジスタアドレスは使用チップのデータシート次第）:

        from machine import mem32
        REGISTER_ADDRESS = 0x3FF44004  # 例: あるチップのGPIO関連レジスタのアドレス
        value = mem32[REGISTER_ADDRESS]        # 読み取り（Cの *(uint32_t*)addr に相当）
        mem32[REGISTER_ADDRESS] = value | 0x1  # 書き込み（ビットを立てる）

    Cとの対比:
        - C:            *(volatile uint32_t*)addr = value;
        - MicroPython:  machine.mem32[addr] = value
        どちらも「型もサイズもチェックされない、正しいアドレスを自分で保証する責任がある」
        という点で危険度は同じ。C以上に「これを使わなければならない場面は稀」という
        位置づけがMicroPythonでの特徴（大抵は`machine.Pin`等の高レベルAPIで足りる）。
    """

    print("\n=== 5. machine.mem8/16/32: raw address access (hardware only, not executed) ===")
    print("実機が必要なため未実行。関数のdocstringに参考コードと解説を記載。")
    print("help(explainMachineMemDirectAccess) で確認できる。")


if __name__ == "__main__":
    demonstrateMemoryModelComparison()
    demonstrateUctypesStruct()
    demonstrateMemoryviewAliasing()
    demonstrateArrayModule()
    explainMachineMemDirectAccess()
