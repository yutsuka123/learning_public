# Python ⇔ C/C++ 相互呼び出しサンプル

[重要] このフォルダは、PythonとC/C++を組み合わせる代表的な3方向を、それぞれ実際に
ビルド・実行して確認するためのものです。

## 3つの方向

| ディレクトリ | 方向 | 使う技術 | 用途の例 |
|---|---|---|---|
| `ctypes_call_c/` | Python → C | `ctypes`（標準ライブラリ、追加インストール不要） | 既存のCライブラリをPythonから素早く叩きたい |
| `pybind11_call_cpp/` | Python → C++ | `pybind11`（`pip install`が必要） | C++のクラス設計をそのままPython APIとして公開したい |
| `embed_python_in_c/` | C/C++ → Python | Python C API（`Python.h`, `python3-config --embed`） | C/C++の母体アプリに、設定や一部ロジックをPythonスクリプトで差し替えられるようにしたい |

`ctypes`は「Cの共有ライブラリの関数シグネチャをPython側で手動宣言して呼ぶ」低レベルな方法、
`pybind11`は「C++のクラス定義からPythonバインディングを自動生成する」高レベルな方法、という
違いがある。CライブラリだけならまずCtypes、C++のクラスをきちんと公開したいならpybind11が
実務でも定石。

## セットアップ

```sh
cd python
python3 -m venv .venv-examples   # 未作成なら
source .venv-examples/bin/activate
pip install pybind11
```

## 1. ctypes_call_c（Python → C）

```sh
cd python/interop_c_cpp/ctypes_call_c
cc -shared -fPIC -O2 -Wall -Wextra mathops.c -o libmathops.dylib   # macOS
# Linux: cc -shared -fPIC -O2 -Wall -Wextra mathops.c -o libmathops.so
#        （call_mathops.py 内の _libPath 拡張子も .so に読み替える）
python3 call_mathops.py
```

内容: 単純な関数呼び出し、配列/ポインタの受け渡し、ポインタ経由のin-place書き換え、
`ctypes.Structure`によるC構造体のマーシャリング。`c/structures/struct_pointer_array_deep_dive.c`
で学んだ内容がPython側からどう見えるかの実演。

## 2. pybind11_call_cpp（Python → C++）

```sh
cd python/interop_c_cpp/pybind11_call_cpp
c++ -O3 -Wall -Wextra -shared -std=c++17 -fPIC -undefined dynamic_lookup \
    $(python3 -m pybind11 --includes) sensor_module.cpp \
    -o sensor_module$(python3-config --extension-suffix)
python3 call_sensor_module.py
```

内容: C++クラスのコンストラクタ/メソッド公開、`std::vector<double>`⇔`list`の自動変換
（`pybind11/stl.h`）、C++例外(`std::invalid_argument`)のPython例外(`ValueError`)への自動変換。
`cpp_m/cpp_basics_class_memory_ownership.cpp`のSensorクラスに相当する題材。

[注意] `-undefined dynamic_lookup` はmacOS固有のリンカフラグ。Linuxでは通常不要
（拡張子も`.so`になる）。

## 3. embed_python_in_c（C/C++ → Python）

```sh
cd python/interop_c_cpp/embed_python_in_c

# C版
cc -O2 -Wall -Wextra $(python3-config --embed --cflags) embed_example.c \
    $(python3-config --embed --ldflags) -o embed_example
./embed_example

# C++版（RAIIでPyObject*の参照カウント管理を自動化した比較版）
c++ -O2 -Wall -Wextra -std=c++17 $(python3-config --embed --cflags) embed_example.cpp \
    $(python3-config --embed --ldflags) -o embed_example_cpp
./embed_example_cpp
```

内容: `Py_Initialize`/`Py_FinalizeEx`によるインタプリタの起動・終了、
`PyRun_SimpleString`での直接実行、`sensor_analysis.py`の関数をC/C++側から
引数付きで呼び出す例。C版は生の`PyObject*`+手動`Py_DECREF`、C++版は
`std::unique_ptr`+カスタムデリータでのRAII化を対比できる
（`cpp_m/cpp_basics_class_memory_ownership.cpp` §2〜3の「new/delete vs unique_ptr」と同じ発想）。

## 検証時に実際に踏んだ問題（学習メモ）

- **macOSでの拡張モジュールリンク**: `pybind11`のビルドで`-undefined dynamic_lookup`を
  付けずにリンクすると、Python C API側のシンボルが未解決でリンクエラーになった
  （Python拡張モジュールは実行時にホストのpythonバイナリからシンボルを解決する前提のため）。
- **zshでの引数分割**: `$(python3 -m pybind11 --includes)`の結果を一度変数へ代入してから
  展開すると、zshでは（bashと違い）自動的に単語分割されず、`-I`フラグが1つの文字列に
  まとまってしまいコンパイルエラーになった。コマンド置換はコマンドラインへ直接埋め込むこと。
- **CとPythonの出力バッファの違い**: C側の`printf`とPython側の`print`の呼び出し順どおりに
  ターミナルへ出力されるとは限らない（バッファリング経路が別のため）。`fflush(stdout)`を
  Python呼び出し前に挟むことで解決した。

## 検証環境

- macOS, Apple clang (Xcode Command Line Tools), Python 3.14 (Homebrew), pybind11 3.0.4
- 全例、ビルド→実行→AddressSanitizer/UndefinedBehaviorSanitizer（`embed_python_in_c/`のみ）で
  正常動作を確認済み。

## 関連ファイル

- C言語での構造体・ポインタ・配列の深掘り: `../../c/structures/struct_pointer_array_deep_dive.c`
- C++基礎（クラス/メモリ/所有権比較）: `../../cpp_m/cpp_basics_class_memory_ownership.cpp`
