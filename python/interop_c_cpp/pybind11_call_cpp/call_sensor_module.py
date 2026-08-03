"""
pybind11でビルドしたC++拡張モジュール(sensor_module)を呼び出すサンプル。

概要:
    `cpp_m/cpp_basics_class_memory_ownership.cpp` で学んだクラス/メソッド/例外が、
    pybind11経由でPythonからどう見えるかを確認する。
主な仕様:
    - demonstrateBasicClassUsage(): C++クラスのコンストラクタ・メソッド・
      std::vector<double><->list自動変換を確認する。
    - demonstrateExceptionTranslation(): C++の`std::invalid_argument`が
      PythonのValueErrorとして捕捉できることを確認する。

実行方法:
    cd python/interop_c_cpp/pybind11_call_cpp
    pip install pybind11
    c++ -O3 -Wall -Wextra -shared -std=c++17 -fPIC -undefined dynamic_lookup \\
        $(python3 -m pybind11 --includes) sensor_module.cpp \\
        -o sensor_module$(python3-config --extension-suffix)
    python3 call_sensor_module.py
    # [注意] -undefined dynamic_lookup はmacOS固有のリンカフラグ。Linuxでは不要
    # （代わりに拡張子は .so になり、通常は追加フラグなしでリンクできる）。
"""

import sensor_module


def demonstrateBasicClassUsage():
    """
    C++クラスのコンストラクタ・メソッド・`std::vector<double>`⇔listの自動変換を確認する。

    実行結果（例）:
        current_value=20.0
        after add_reading(21.5), add_reading(22.0):
        current_value=22.0
        readings=[21.5, 22.0]
        average_reading=21.75
        describe()=temperature=22.000000
        name=temperature
    """

    print("=== 1. basic class usage: SensorDevice (C++ class via pybind11) ===")

    device = sensor_module.SensorDevice("temperature", 20.0)
    print(f"current_value={device.current_value()}")

    device.add_reading(21.5)
    device.add_reading(22.0)
    print("after add_reading(21.5), add_reading(22.0):")
    print(f"current_value={device.current_value()}")

    # [重要] C++側は std::vector<double> を返しているだけだが、pybind11/stl.h の効果で
    # Python側にはごく普通の list として見える（手動の変換コードは不要）。
    print(f"readings={device.readings()}")
    print(f"average_reading={device.average_reading()}")
    print(f"describe()={device.describe()}")
    print(f"name={device.name}")


def demonstrateExceptionTranslation():
    """
    C++の`std::invalid_argument`が、PythonのValueErrorとして捕捉できることを確認する。

    実行結果（例）:
        caught ValueError: averageReading: no readings recorded yet
    """

    print("\n=== 2. exception translation: std::invalid_argument -> ValueError ===")

    freshDevice = sensor_module.SensorDevice("humidity", 0.0)
    try:
        freshDevice.average_reading()  # readingsが空のままなのでC++側でthrowされる
    except ValueError as e:
        print(f"caught ValueError: {e}")


if __name__ == "__main__":
    demonstrateBasicClassUsage()
    demonstrateExceptionTranslation()
