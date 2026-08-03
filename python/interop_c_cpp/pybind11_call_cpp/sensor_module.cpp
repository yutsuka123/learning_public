/**
 * @file sensor_module.cpp
 * @brief [重要] pybind11でC++クラスをPythonモジュール化するサンプル。
 * @details
 * - 目的: `cpp_m/cpp_basics_class_memory_ownership.cpp` で学んだクラス/メソッド/例外が、
 *   pybind11経由でPythonからどう見えるかを確認する。
 * - `pybind11/stl.h` をインクルードすると、`std::vector<double>` が自動的にPythonの
 *   `list`と相互変換される（手動でのマーシャリングが不要になる）。
 * - ビルド方法・実行方法は README.md を参照。
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // std::vector <-> Python list の自動変換に必要

#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;

/**
 * @brief センサーデバイスを表すC++クラス（cpp_basics の Sensor クラスに相当）。
 */
class SensorDevice {
 public:
  SensorDevice(std::string name, double initialValue)
      : name_(std::move(name)), value_(initialValue) {}

  void addReading(double reading) {
    readings_.push_back(reading);
    value_ = reading;
  }

  double currentValue() const { return value_; }

  double averageReading() const {
    if (readings_.empty()) {
      // [重要] C++の例外は、pybind11が自動的に対応するPython例外へ変換して投げ直す
      // （std::invalid_argument -> Pythonの ValueError）。C++側は「Pythonがある」ことを
      // 一切意識せずに書ける点がpybind11の利点。
      throw std::invalid_argument("averageReading: no readings recorded yet");
    }
    return std::accumulate(readings_.begin(), readings_.end(), 0.0) / static_cast<double>(readings_.size());
  }

  const std::vector<double>& readings() const { return readings_; }

  const std::string& name() const { return name_; }

  std::string describe() const { return name_ + "=" + std::to_string(value_); }

 private:
  std::string name_;
  double value_;
  std::vector<double> readings_;
};

PYBIND11_MODULE(sensor_module, m) {
  m.doc() = "pybind11 sample: expose a C++ SensorDevice class to Python";

  py::class_<SensorDevice>(m, "SensorDevice")
      .def(py::init<std::string, double>(), py::arg("name"), py::arg("initial_value"))
      .def("add_reading", &SensorDevice::addReading, py::arg("reading"))
      .def("current_value", &SensorDevice::currentValue)
      .def("average_reading", &SensorDevice::averageReading)
      .def("readings", &SensorDevice::readings)
      .def("describe", &SensorDevice::describe)
      .def_property_readonly("name", &SensorDevice::name);
}
