# 組み込み Python (MicroPython) サンプル

[重要] このフォルダは「組み込み機器でPythonを使う」ことを、**MicroPython**を題材に学ぶためのものです。
CPython（このリポジトリの他のPythonサンプルが動いている、PC上の通常のPython処理系）とは別の、
組み込み向けに再実装された軽量なPython処理系です。

## MicroPythonの特徴（CPythonとの違い）

- **超軽量**: フラッシュ数百KB〜数MB、RAM数十KB〜数百KB程度のマイコン（ESP32, Raspberry Pi Pico等）で動く。
- **標準ライブラリはサブセット**: `os`/`sys`/`socket`等はあるが、`pandas`のような大規模ライブラリは無い。
  ハードウェア制御専用の`machine`モジュールが追加されている。
- **明示的なメモリ管理の余地**: `gc.collect()`を手動で呼べる、`micropython.const()`でRAMを使わない
  定数を作れる等、「メモリが有限であること」を強く意識した機能がある
  （詳細: `memory_and_language_features.py`, `pointers_and_memory_c_vs_micropython_vs_cpython.py`）。
- **ポインタに近い操作もできる**: 通常は不要だが、`uctypes`/`memoryview`/`machine.mem8,16,32`で
  C言語のポインタ操作に近いこともできる（詳細: `pointers_and_memory_c_vs_micropython_vs_cpython.py`）。
- **移植先(ポート)ごとに使えるモジュールが違う**: 本フォルダの動作確認は「unixポート」
  （PC上で動くMicroPython。`brew install micropython`で導入）で行っており、`machine`モジュールの
  中身はほぼ空（`Pin`/`ADC`/`PWM`等の実装が無い）。実機（ESP32/Raspberry Pi Pico等）でなければ
  `machine.Pin`等は使えない。

## このフォルダのファイル構成

| ファイル | 内容 | 実行可否（このリポジトリの検証環境=unixポート） |
|---|---|---|
| `concurrency_examples.py` | `_thread`（真の並列） vs `asyncio`（協調的並行） | ✅ 実行して動作確認済み |
| `communication_examples.py` | `socket`によるTCP通信、`struct`によるバイナリ組み立て | ✅ 実行して動作確認済み（デッドロックバグを実際に踏んで修正済み） |
| `memory_and_language_features.py` | `gc`/`micropython.const`/CPythonとの違い | ✅ 実行して動作確認済み |
| `pointers_and_memory_c_vs_micropython_vs_cpython.py` | C/CPython/MicroPythonのメモリモデル比較、`uctypes`/`memoryview`/`array` | ✅ 実行して動作確認済み（`machine.mem32`部分のみ実機必須のため未実行） |
| `hardware_gpio_interrupt_examples.py` | `machine.Pin`(GPIO)/PWM/ADC/UART/I2C/SPI/Timer/割り込み | ⚠️ **実機必須**。unixポートでは`machine`モジュールが空のため動かせない（未検証） |

## セットアップ（unixポート、macOS）

```sh
brew install micropython
micropython --version
```

## 実行方法

```sh
cd python/embedded_micropython
micropython concurrency_examples.py
micropython communication_examples.py
micropython memory_and_language_features.py
micropython pointers_and_memory_c_vs_micropython_vs_cpython.py
```

`hardware_gpio_interrupt_examples.py` は実機（ESP32等）へ転送して実行する。
このリポジトリでは `IoT/ESP32`（C++/PlatformIO/ESP32-S3）で実機を扱っているので、
「同じESP32で、C++ではなくMicroPythonから触るとどう書けるか」の比較として読むとよい。

実機への書き込み例（[mpremote](https://docs.micropython.org/en/latest/reference/mpremote.html)を使う場合）:

```sh
pip install mpremote
mpremote connect /dev/tty.usbserial-XXXX run hardware_gpio_interrupt_examples.py
```

## 検証時に実際に踏んだバグ（学習メモ）

`communication_examples.py`の作成時、サーバ役スレッドが`bind()`失敗
（`OSError: EADDRINUSE`、直前実行のTIME_WAIT状態が原因）で異常終了すると、
「listen完了を待つロック」が解放されずメインスレッドが**永久にブロックする**
デッドロックを実際に発生させてしまった。`SO_REUSEADDR`の設定と、
`try/finally`でロック解放を保証する修正で解決した。組み込み・マルチスレッド
コードでは「異常系でも後始末（ロック解放等）を必ず行う」ことの重要性を示す実例。

## 関連ファイル

- C言語での構造体・ポインタ・配列の深掘り: `../../c/structures/struct_pointer_array_deep_dive.c`
- C++基礎（クラス/メモリ/所有権比較）: `../../cpp_m/cpp_basics_class_memory_ownership.cpp`
- Rust所有権・借用: `../../rust/basics_ownership_borrowing/`
- 実機ESP32-S3プロジェクト（C++/PlatformIO）: `../../IoT/ESP32/`
