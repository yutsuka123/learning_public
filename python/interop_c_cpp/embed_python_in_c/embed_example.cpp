/**
 * @file embed_example.cpp
 * @brief [重要] C++からPythonインタプリタを埋め込み、Python関数を呼び出すサンプル（C++版）。
 * @details
 * - `embed_example.c` とほぼ同じ内容だが、C++では`std::unique_ptr`にカスタムデリータを
 *   渡すことで、`PyObject*`の`Py_DECREF`忘れを防げる（RAII）。これは
 *   `cpp_m/cpp_basics_class_memory_ownership.cpp` §2〜3 で見た「newしたら必ずdeleteを
 *   書かなければならない」問題を、Python C APIの参照カウント管理にも同じ発想で
 *   適用した例になっている。
 * - `Python.h`はC++からインクルードしても問題ない（内部で`extern "C"`により
 *   C++からのリンクに対応している）。
 * - ビルド方法・実行方法は README.md を参照。
 *
 * 実行手順:
 * cd python/interop_c_cpp/embed_python_in_c
 * c++ -O2 -Wall -Wextra -std=c++17 $(python3-config --embed --cflags) embed_example.cpp \
 *     $(python3-config --embed --ldflags) -o embed_example_cpp
 * ./embed_example_cpp
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdio>
#include <memory>

namespace {

/**
 * @brief `PyObject*`をRAIIで自動`Py_DECREF`するためのスマートポインタ型エイリアス。
 * @details `cpp_basics_class_memory_ownership.cpp`のunique_ptrの話と同じく、
 * 「スコープを抜けたら自動的に後始末される」ことで、DECREF忘れという
 * バグのクラスをそもそも起こさないようにする。
 */
using PyObjectPtr = std::unique_ptr<PyObject, decltype(&Py_DecRef)>;

PyObjectPtr makePyObjectPtr(PyObject* raw) { return PyObjectPtr(raw, &Py_DecRef); }

/**
 * @brief 最も単純な例: PyRun_SimpleStringでPythonコードの文字列をそのまま実行する。
 * @return void
 */
void runSimpleString() {
  std::printf("--- 1. PyRun_SimpleString: run a Python code string directly ---\n");
  std::fflush(stdout);  // CのstdioバッファとPython側の出力経路が別なため、順序保証のためflushする
  PyRun_SimpleString(
      "print('hello from embedded Python (C++)')\n"
      "print('2 + 3 =', 2 + 3)\n");
}

/**
 * @brief sensor_analysis.py の average() 関数を、C++側から引数付きで呼び出す。
 * @details unique_ptr(PyObjectPtr)により、途中でreturnしても自動的にDECREFされる
 * （生のPyObject*とPy_DECREFの手動呼び出しだけで書いていた embed_example.c と比較するとよい）。
 * @return void
 */
void callPythonFunction() {
  std::printf("\n--- 2. call a Python function from C++, with arguments and a return value ---\n");

  PyRun_SimpleString("import sys; sys.path.insert(0, '.')");

  PyObjectPtr pModule = makePyObjectPtr(PyImport_ImportModule("sensor_analysis"));
  if (!pModule) {
    PyErr_Print();
    std::fprintf(stderr, "[error] function=callPythonFunction: sensor_analysis のimportに失敗しました\n");
    return;
  }

  PyObjectPtr pFunc = makePyObjectPtr(PyObject_GetAttrString(pModule.get(), "average"));
  if (!pFunc || !PyCallable_Check(pFunc.get())) {
    PyErr_Print();
    std::fprintf(stderr, "[error] function=callPythonFunction: average関数が見つかりません\n");
    return;
  }

  PyObjectPtr pList = makePyObjectPtr(PyList_New(4));
  PyList_SetItem(pList.get(), 0, PyFloat_FromDouble(1.0));  // SetItemは参照をstealする
  PyList_SetItem(pList.get(), 1, PyFloat_FromDouble(2.0));
  PyList_SetItem(pList.get(), 2, PyFloat_FromDouble(3.0));
  PyList_SetItem(pList.get(), 3, PyFloat_FromDouble(4.0));

  PyObjectPtr pArgs = makePyObjectPtr(PyTuple_Pack(1, pList.get()));  // Packはstealしない
  PyObjectPtr pResult = makePyObjectPtr(PyObject_CallObject(pFunc.get(), pArgs.get()));

  if (!pResult) {
    PyErr_Print();
    std::fprintf(stderr, "[error] function=callPythonFunction: average()の呼び出しに失敗しました\n");
    return;
  }

  const double averageValue = PyFloat_AsDouble(pResult.get());
  std::printf("average([1.0, 2.0, 3.0, 4.0]) = %f\n", averageValue);
  // pResult/pArgs/pList/pFunc/pModule はここでスコープを抜けると自動的にDECREFされる。
}

}  // namespace

/**
 * @brief エントリポイント。
 * @return int 終了コード
 */
int main() {
  Py_Initialize();

  runSimpleString();
  callPythonFunction();

  if (Py_FinalizeEx() < 0) {
    return 1;
  }
  return 0;
}
