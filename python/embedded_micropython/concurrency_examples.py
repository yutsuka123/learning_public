"""
MicroPython の並列処理・並行処理サンプル（_thread と asyncio の比較）。

概要:
    - `_thread`: OSレベルの本物のスレッド。ESP32のようなマルチコアチップでは
      「第2コアで動かす」ことにも使われる（本リポジトリの `IoT/ESP32` プロジェクトが
      C++/Arduinoフレームワーク経由で内部的に使っているFreeRTOSタスクに近い概念）。
    - `asyncio`: 1つのスレッド内で「待っている間に他の処理へ切り替える」協調的並行処理。
      センサー読み取りや通信待ちなど「I/O待ちが多い」処理に向く。MicroPythonでは歴史的に
      `uasyncio`という名前だったが、現行版では`asyncio`が正式名（`uasyncio`は互換用の別名）。
主な仕様:
    - demonstrateThreading(): _threadで2つのワーカーを同時に走らせ、ロックで
      共有カウンタを保護する例。
    - demonstrateAsyncio(): asyncio.gatherで2つの非同期タスクを協調実行する例。
制限事項:
    - `_thread`はunixポートでは`sys.implementation._thread == 'unsafe'`
      （公式に安全性が保証されていない、の意）。実機ではESP32等の対応ポートを使うこと。
    - MicroPythonの`_thread`にはCPythonの`Thread.join()`に相当する標準APIが無いポートが多い。
      このサンプルでは「完了フラグをポーリングする」簡易な方法で代用している。

実行方法:
    cd python/embedded_micropython
    micropython concurrency_examples.py
    # 検証環境: micropython 1.28.0 (unix port, Homebrew) で実行結果を確認済み。
"""

import _thread
import asyncio
import time


def demonstrateThreading():
    """
    `_thread` による「本物の並列処理」を確認する。

    目的:
        2つのワーカーを別スレッドで同時に走らせ、共有カウンタをロックで保護しながら
        更新する。ロックが無いと、複数スレッドが同時に `count += 1` を実行して
        更新が失われる（レースコンディション）可能性がある。

    実行結果（例。2スレッドの実行順は毎回入れ替わりうるため、tickの出現順は変わりうる）:
        thread 1 tick 0 count=1
        thread 2 tick 0 count=2
        thread 1 tick 1 count=3
        thread 2 tick 1 count=4
        thread 1 tick 2 count=5
        thread 2 tick 2 count=6
        final count=6 (expected 6)
    """

    print("=== 1. _thread: true parallelism (2 OS threads) ===")

    count = 0
    lock = _thread.allocate_lock()
    finishedCount = 0
    doneLock = _thread.allocate_lock()

    def worker(workerId, delaySeconds):
        nonlocal count, finishedCount
        for i in range(3):
            time.sleep(delaySeconds)
            with lock:  # [重要] ロックなしで複数スレッドから同時に count += 1 すると更新が失われうる
                count += 1
                print(f"thread {workerId} tick {i} count={count}")
        with doneLock:
            finishedCount += 1

    _thread.start_new_thread(worker, (1, 0.05))
    _thread.start_new_thread(worker, (2, 0.07))

    # [注意] join()相当が無いため、両方のワーカーが完了フラグを立てるまでポーリングして待つ。
    while finishedCount < 2:
        time.sleep(0.02)

    print(f"final count={count} (expected 6)")


async def _asyncioWorker(name, delaySeconds, times):
    """
    asyncio用のワーカー。指定回数、指定間隔で待って表示する。

    引数:
        name (str): ログ表示用の名前。
        delaySeconds (float): 1回あたりの待ち時間（秒）。
        times (int): 繰り返し回数。
    """

    for i in range(times):
        # [重要] time.sleep()と違い、await asyncio.sleep()は「待っている間、
        # イベントループが他のタスク（この場合はもう一方のworker）に制御を譲る」。
        # そのため1スレッドしかなくても複数のタスクが「並行して」進んでいるように見える。
        await asyncio.sleep(delaySeconds)
        print(f"{name} tick {i}")


async def _runAsyncioDemo():
    await asyncio.gather(_asyncioWorker("A", 0.05, 3), _asyncioWorker("B", 0.07, 3))


def demonstrateAsyncio():
    """
    `asyncio` による「協調的並行処理」を確認する。

    目的:
        2つの非同期ワーカーを`asyncio.gather`で同時に開始し、待ち時間中はお互いに
        制御を譲り合いながら並行して進むことを確認する。

    実行結果（例。A/Bのtickが交互に近い順で出る。実際の間隔0.05秒と0.07秒の
    タイミング次第で若干前後することがある）:
        A tick 0
        B tick 0
        A tick 1
        B tick 1
        A tick 2
        B tick 2
    """

    print("\n=== 2. asyncio: cooperative concurrency (1 thread) ===")
    asyncio.run(_runAsyncioDemo())


def demonstrateThreadVsAsyncioSummary():
    """
    `_thread` と `asyncio` の使い分けの指針を表示する。
    """

    print("\n=== 3. _thread vs asyncio: how to choose ===")
    print("- _thread: CPUを使う重い処理を本当に並列で走らせたい（ESP32の第2コア活用等）")
    print("- asyncio: I/O待ち(通信・センサー読み取り等)が多い処理を、1コアで賢く切り替えたい")
    print("- 迷ったらasyncioから検討する: 組み込みは共有メモリの扱いがシビアで、")
    print("  ロック忘れによるレースコンディションのバグは原因調査が難しい。")
    print("  _threadは「本当に並列でなければ間に合わない」ときだけ使う。")


if __name__ == "__main__":
    demonstrateThreading()
    demonstrateAsyncio()
    demonstrateThreadVsAsyncioSummary()
