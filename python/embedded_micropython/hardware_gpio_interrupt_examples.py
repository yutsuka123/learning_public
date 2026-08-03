"""
MicroPython の実機ハードウェア操作サンプル（GPIO / PWM / ADC / UART / I2C / SPI / タイマー割り込み）。

概要:
    `machine`モジュールはMicroPythonの「ハードウェアを直接触る」ための中核モジュール。
    Cで直接レジスタを叩く/Arduinoの`digitalWrite()`等に相当する処理を、Pythonの
    オブジェクト指向APIとして提供する。

[重要・実行環境について]
    このファイルは **実機（ESP32, Raspberry Pi Pico等）が必須** で、このリポジトリの
    検証環境（`micropython`コマンド、unixポート）では**実行できない・検証していない**。
    unixポートの`machine`モジュールは中身がほぼ空（`Pin`/`ADC`/`PWM`/`UART`/`I2C`/`SPI`/`Timer`が
    存在しない）ことを実際に確認済み:

        $ micropython -c "import machine; print(dir(machine))"
        ['__class__', '__name__', 'PinBase', 'Signal', '__dict__', 'idle',
         'mem16', 'mem32', 'mem8', 'soft_reset', 'time_pulse_us']

    そのため本ファイルのコードは、MicroPython公式ドキュメント記載のAPIに基づいて
    記述しているが、**このセッションでは実機での動作確認をしていない**。
    実機（本リポジトリでは `IoT/ESP32` でC++/Arduinoフレームワーク経由のESP32-S3を
    既に扱っている）へ書き込んで試すことを前提とする。ピン番号はESP32を例にしており、
    Raspberry Pi Pico等では配置が異なる。

実行方法（実機、mpremote使用時の例）:
    pip install mpremote
    mpremote connect /dev/tty.usbserial-XXXX run hardware_gpio_interrupt_examples.py
"""

import time

from machine import ADC, I2C, PWM, SPI, UART, Pin, Timer

# =============================================================================
# 1. GPIO 基本（デジタル出力・入力）
# =============================================================================


def demonstrateGpioOutput():
    """
    GPIO出力の基本（LED点滅）。

    [重要] `Pin(2, Pin.OUT)` の `2` はESP32devkitのオンボードLEDに割り当てられがちな
    GPIO番号の一例。実機のボード図（ピンアウト）で必ず確認すること。
    """

    led = Pin(2, Pin.OUT)
    for _ in range(5):
        led.value(1)  # led.on() でも同じ
        time.sleep(0.2)
        led.value(0)  # led.off() でも同じ
        time.sleep(0.2)


def demonstrateGpioInput():
    """
    GPIO入力の基本（ボタン読み取り、内蔵プルアップ使用）。

    [重要] `Pin.PULL_UP` を指定すると、ボタンを押していないとき自動的にHigh(1)に
    保たれる（外付けの抵抗が不要になる）。多くのボードでボタンは「押すとLow」になる
    配線（アクティブLow）が一般的なため、`if button.value() == 0:` で押下を判定する。
    """

    button = Pin(0, Pin.IN, Pin.PULL_UP)
    for _ in range(20):
        if button.value() == 0:
            print("button pressed")
        time.sleep(0.1)


# =============================================================================
# 2. GPIO割り込み（IRQ）とタイマー割り込み
# =============================================================================


def demonstrateGpioInterrupt():
    """
    GPIO割り込み(IRQ)によるボタン検知。

    [重要] 割り込みハンドラ（コールバック関数）の中では、原則として「短く、素早く」
    処理を終える必要がある。特にハード割り込みコンテキストでは:
    - メモリ確保を伴う処理（新しいリストや文字列の生成等）を避ける
      （ポートによっては`MemoryError`や不安定動作の原因になる）
    - 重い処理は `micropython.schedule()` でメインループ側へ「予約」して、
      実際の処理はメインループのタイミングで行う（下記`_deferredHandler`参照）
    - チャタリング（機械的なスイッチのバウンドで短時間に何度も発火する現象）対策として、
      前回発火時刻を記録し、一定時間内の再発火は無視する（デバウンス）
    """

    import micropython

    micropython.alloc_emergency_exception_buf(100)  # IRQ内で例外が起きた際の詳細表示用バッファ確保

    button = Pin(0, Pin.IN, Pin.PULL_UP)
    state = {"lastPressMs": 0, "pressCount": 0}
    debounceMs = 200

    def _deferredHandler(pressCount):
        # micropython.schedule()経由で「通常のコンテキスト」で呼ばれるため、
        # printや通信など重い処理をしてもよい。
        print(f"button pressed! (count={pressCount})")

    def _isrHandler(pin):
        # [重要] ここはISR(割り込みサービスルーチン)。最小限の処理に留める。
        now = time.ticks_ms()
        if time.ticks_diff(now, state["lastPressMs"]) < debounceMs:
            return  # チャタリング/連続発火とみなして無視
        state["lastPressMs"] = now
        state["pressCount"] += 1
        micropython.schedule(_deferredHandler, state["pressCount"])

    button.irq(trigger=Pin.IRQ_FALLING, handler=_isrHandler)

    print("waiting for button presses... (Ctrl-C to stop)")
    while True:
        time.sleep(1)  # メインループは他の仕事をしていてよい。押下はirqが検知する


def demonstrateTimerInterrupt():
    """
    タイマー割り込みによる定期実行（センサーの定期ポーリング等に使う）。

    [重要] `Timer(0)`の`0`はタイマーIDで、ボードによって使える番号の範囲が異なる。
    `mode=Timer.PERIODIC`で周期実行、`Timer.ONE_SHOT`で1回だけの実行になる。
    """

    tickCount = {"value": 0}

    def onTick(timer):
        tickCount["value"] += 1
        print(f"tick {tickCount['value']}")

    tim = Timer(0)
    tim.init(period=1000, mode=Timer.PERIODIC, callback=onTick)  # 1000ms=1秒ごと

    time.sleep(5)
    tim.deinit()  # タイマーを止める（irqも同様に `pin.irq(handler=None)` で解除できる）


# =============================================================================
# 3. アナログ入出力（ADC / PWM）
# =============================================================================


def demonstrateAdc():
    """
    ADC（アナログ→デジタル変換）で可変抵抗やアナログセンサーの値を読む。

    [注意] ESP32のADCは0〜3.3V程度の入力を想定するが、内部の非線形性のため
    実測値の較正（キャリブレーション）が必要になることがある。`atten`（減衰設定）は
    読みたい電圧レンジに応じて調整する。
    """

    adc = ADC(Pin(34))  # ESP32はGPIO32-39がADC1（Wi-Fi使用中でも安定して使える）
    adc.atten(ADC.ATTN_11DB)  # 入力レンジをおおよそ0〜3.3Vまで拡張
    for _ in range(5):
        rawValue = adc.read()  # 0〜4095（12bit）程度の生値（ポート/構成により範囲は変わる）
        print(f"adc raw={rawValue}")
        time.sleep(0.5)


def demonstratePwm():
    """
    PWM（パルス幅変調）でLEDの明るさやサーボ・ブザーを制御する。

    [重要] `duty()`は0〜1023（10bit、ポートにより範囲が異なる）で「オン時間の割合」を表す。
    サーボモータの角度制御にはduty_ns()でパルス幅を直接ナノ秒指定する方法もよく使われる。
    """

    led = Pin(2, Pin.OUT)
    pwm = PWM(led, freq=1000)  # 1kHzで駆動
    for duty in (0, 256, 512, 768, 1023):  # 0%, 25%, 50%, 75%, 100%相当
        pwm.duty(duty)
        print(f"pwm duty={duty}")
        time.sleep(0.5)
    pwm.deinit()


# =============================================================================
# 4. シリアル通信（UART / I2C / SPI）
# =============================================================================


def demonstrateUart():
    """
    UART（調歩同期式シリアル通信）でセンサーモジュールや別マイコンと通信する。

    [重要] GPSモジュールやBluetoothモジュール等、多くの外部モジュールがUARTで
    通信する。`baudrate`は接続先の仕様書に合わせる（合わないと文字化けする）。
    """

    uart = UART(1, baudrate=9600, tx=Pin(17), rx=Pin(16))
    uart.write("hello from esp32\n")

    time.sleep(0.5)
    if uart.any():  # 受信バッファに未読データがあるバイト数（0なら無し）
        received = uart.read()
        print(f"uart received: {received}")


def demonstrateI2c():
    """
    I2C（2線式シリアルバス）でセンサー等と通信する。

    [重要] I2Cは1本のバスに複数デバイスをアドレスで区別してぶら下げられる。
    `scan()`で接続中のデバイスの7bitアドレス一覧を取得できる（配線・電源トラブルの
    切り分けに便利）。
    """

    i2c = I2C(0, scl=Pin(22), sda=Pin(21), freq=400000)
    devices = i2c.scan()
    print(f"i2c devices found: {[hex(addr) for addr in devices]}")

    if devices:
        targetAddr = devices[0]
        # レジスタ0x00から2バイト読む、という典型パターン（デバイスのデータシート次第）
        data = i2c.readfrom_mem(targetAddr, 0x00, 2)
        print(f"i2c read from 0x{targetAddr:02x}: {data}")


def demonstrateSpi():
    """
    SPI（高速な同期式シリアル通信）でディスプレイやSDカード等と通信する。

    [重要] SPIはI2Cより高速だが、デバイスごとに専用のCS(チップセレクト)線が必要
    （バス自体は共有できるが、通信相手はCSで選ぶ）。
    """

    spi = SPI(1, baudrate=1_000_000, polarity=0, phase=0, sck=Pin(18), mosi=Pin(23), miso=Pin(19))
    csPin = Pin(5, Pin.OUT)

    csPin.value(0)  # 通信相手を選択（Low選択が一般的）
    spi.write(b"\x9f")  # 例: 多くのSPI флеш/センサーでよく使われる「デバイスID読み取り」系コマンド
    response = spi.read(3)
    csPin.value(1)  # 選択解除

    print(f"spi response: {response}")


# =============================================================================
# 5. 電源管理（組み込みならではの関心事）
# =============================================================================


def demonstrateDeepSleep():
    """
    ディープスリープによる省電力運用。

    [重要] 電池駆動のセンサーノード等では「普段は寝ていて、一定時間ごとに起きて
    測定・送信し、また寝る」という運用が消費電力を大きく左右する。ディープスリープからの
    復帰はリセットに近い扱いになる（変数はリセットされる）ため、保持したい値は
    RTCメモリ等の専用領域に置く必要がある（ポートにより方法が異なる）。
    """

    import machine

    print("going to deep sleep for 10 seconds...")
    machine.deepsleep(10_000)  # ミリ秒単位。呼び出すとここで処理が止まり、起床後は最初から再実行される


if __name__ == "__main__":
    # [注意] 実機で全部を続けて実行すると長くなるため、試したいものだけコメントアウトを外す運用を想定。
    demonstrateGpioOutput()
    # demonstrateGpioInput()
    # demonstrateGpioInterrupt()
    # demonstrateTimerInterrupt()
    # demonstrateAdc()
    # demonstratePwm()
    # demonstrateUart()
    # demonstrateI2c()
    # demonstrateSpi()
    # demonstrateDeepSleep()
