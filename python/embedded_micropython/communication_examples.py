"""
MicroPython の通信サンプル（TCPソケット通信、バイナリプロトコルの組み立て）。

概要:
    組み込み機器の通信は「Wi-Fi/イーサネット経由のTCP・UDP」「UART/I2C/SPIのような
    シリアル通信」に大別できる。前者（ネットワーク）はこのファイルでunixポート上でも
    実際に動作確認できる。後者（シリアル通信、`machine.UART`等）は実機が必要なため
    `hardware_gpio_interrupt_examples.py` 側にまとめている。
主な仕様:
    - demonstrateTcpSocket(): _threadでサーバを立て、クライアントから接続して
      1往復のデータ送受信を行う。
    - demonstrateBinaryProtocolWithStruct(): `struct`モジュールでC言語の構造体に近い
      固定長バイナリを組み立て/分解する（センサー値をコンパクトに送る典型パターン）。
制限事項:
    - CPythonの`socket.bind(('127.0.0.1', port))`のように素のタプルを渡す書き方は
      MicroPythonでは`TypeError`になる（実際に本ファイル作成時に確認した挙動）。
      `socket.getaddrinfo()`で得たアドレスを使う必要がある。

実行方法:
    cd python/embedded_micropython
    micropython communication_examples.py
    # 検証環境: micropython 1.28.0 (unix port, Homebrew) で実行結果を確認済み。
"""

import _thread
import socket
import struct
import time

HOST = "127.0.0.1"
PORT = 8766


def _runEchoServer(readyLock):
    """
    1回だけ接続を受け付け、受信したバイト列に `ack:` を付けて返すだけの簡易サーバ。

    [重要] `readyLock.release()` は必ず`finally`で呼ぶ。理由: bind等で例外が起きた場合に
    releaseされないと、メインスレッド側の`with readyLock:`が**永久にブロックしたまま
    デッドロックする**（実際に、前回起動のTIME_WAIT状態が残った状態で立て続けに
    このファイルを実行し、`EADDRINUSE`例外でハングすることを確認したうえでの修正）。

    引数:
        readyLock (_thread.lock): サーバが`listen`（または起動失敗）を終えたことを
            メインスレッドへ伝えるためのロック。
    """

    try:
        addr = socket.getaddrinfo(HOST, PORT)[0][-1]
        serverSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # [重要] SO_REUSEADDRを付けないと、直前の実行のソケットがTIME_WAIT状態のうちは
        # 同じポートへのbindが`OSError: [Errno 48] EADDRINUSE`で失敗することがある。
        serverSocket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        serverSocket.bind(addr)
        serverSocket.listen(1)
    finally:
        # 成功・失敗どちらでもここでメインスレッドの待ちを必ず解除する。
        # bind/listenで例外が起きた場合、このfinally実行後に例外がそのまま再送出され、
        # 以降の行（accept以降）には到達しない（＝メインスレッドが待ち続けるデッドロックを防ぐのが目的）。
        readyLock.release()

    conn, _clientAddr = serverSocket.accept()
    data = conn.recv(64)
    print(f"server: received {data}")
    conn.send(b"ack:" + data)
    conn.close()
    serverSocket.close()


def demonstrateTcpSocket():
    """
    `socket`によるTCP通信を、同一プロセス内のサーバ役・クライアント役で確認する。

    目的:
        実機でも（Wi-Fi接続後は）ほぼ同じAPIでセンサーゲートウェイやMQTTブローカー等と
        通信できることを、まずTCPの生ソケットレベルで確認する。
    [注意] `socket.getaddrinfo(host, port)` は解決したアドレス情報のリストを返す。
        末尾の要素 `[-1]` が `bind`/`connect` にそのまま渡せるアドレス形式になっている
        （CPythonのような `(host, port)` タプルではなく、実装依存の内部形式である点に注意）。

    実行結果（例）:
        server: received b'hello'
        client: got b'ack:hello'
    """

    print("=== 1. TCP socket: client/server round trip ===")

    readyLock = _thread.allocate_lock()
    readyLock.acquire()  # サーバがlisten完了するまでロックしておく
    _thread.start_new_thread(_runEchoServer, (readyLock,))

    with readyLock:  # サーバ側がreleaseするまでここでブロックする（=listen完了を待つ）
        pass

    addr = socket.getaddrinfo(HOST, PORT)[0][-1]
    clientSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    clientSocket.connect(addr)
    clientSocket.send(b"hello")
    response = clientSocket.recv(64)
    print(f"client: got {response}")
    clientSocket.close()

    time.sleep(0.1)  # サーバスレッドの後片付け(close)が終わるのを軽く待つ


def demonstrateBinaryProtocolWithStruct():
    """
    `struct`モジュールで固定長バイナリを組み立て・分解する。

    目的:
        帯域やメモリが限られる組み込み通信（LoRa/BLE/独自シリアルプロトコル等）では、
        JSONのようなテキスト形式ではなく、C言語の構造体に近い「固定バイト数」の
        バイナリでやり取りすることが多い。`struct.pack`/`unpack`で、Cの構造体定義に近い
        書式文字列を使ってエンコード/デコードできることを確認する
        （`c/structures/struct_pointer_array_deep_dive.c` の構造体パディングの話とも関連: `struct`の
        書式文字列は基本的にパディング無しでパックされる点がCの構造体と異なる）。

    実行結果（例。float32のバイト表現・丸め誤差は環境依存の場合がある）:
        packed=b'\\x01\\xe8\\x03ff\\x1aA' (7 bytes)
        unpacked=(1, 1000, 9.649999618530274)
    """

    print("\n=== 2. binary protocol with struct (compact sensor payload) ===")

    # フォーマット文字列の意味: '<' リトルエンディアン, 'B' uint8, 'H' uint16, 'f' float32
    # 例: センサーID(1byte) + 生の測定値(2byte) + 変換後の物理値(4byte) を1パケットにまとめる
    fmt = "<BHf"
    sensorId = 1
    rawValue = 1000
    physicalValue = 9.65

    packed = struct.pack(fmt, sensorId, rawValue, physicalValue)
    print(f"packed={packed} ({len(packed)} bytes)")

    unpacked = struct.unpack(fmt, packed)
    print(f"unpacked={unpacked}")


if __name__ == "__main__":
    demonstrateTcpSocket()
    demonstrateBinaryProtocolWithStruct()
