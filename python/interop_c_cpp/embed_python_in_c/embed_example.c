/**
 * @file embed_example.c
 * @brief [重要] C言語からPythonインタプリタを埋め込み、Python関数を呼び出すサンプル。
 * @details
 * - 目的: これまでの「PythonからC/C++を呼ぶ」方向（ctypes_call_c/, pybind11_call_cpp/）とは
 *   逆に、「C側にPythonを埋め込んで呼び出す」方向を確認する。GUI等の母体アプリがC/C++で
 *   書かれていて、一部の設定・スクリプト処理だけPythonにやらせたい場合等に使われる構成。
 * - ビルド方法・実行方法は README.md を参照。
 *
 * 実行手順:
 * cd python/interop_c_cpp/embed_python_in_c
 * cc -O2 -Wall -Wextra $(python3-config --embed --cflags) embed_example.c \
 *     $(python3-config --embed --ldflags) -o embed_example
 * ./embed_example
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdio.h>

/**
 * @brief 最も単純な例: PyRun_SimpleStringでPythonコードの文字列をそのまま実行する。
 * @return void
 */
static void runSimpleString(void) {
    printf("--- 1. PyRun_SimpleString: run a Python code string directly ---\n");
    fflush(stdout); /* [重要] Cのstdioバッファと、Python側のstdout書き込みは別経路のため、
                      * flushしないと出力の順序が入れ替わって見えることがある
                      * （実際にこのファイル作成時、flush無しで順序が入れ替わることを確認した）。 */
    PyRun_SimpleString(
        "print('hello from embedded Python')\n"
        "print('2 + 3 =', 2 + 3)\n");
}

/**
 * @brief sensor_analysis.py の average() 関数を、C側から引数付きで呼び出す。
 * @details
 * [重要] Python C APIの多くの関数は「参照を渡すと消費(steal)する」ものと
 * 「渡しても消費しない(borrow)」ものが混在しており、どちらかを間違えると
 * メモリリークまたは二重解放につながる。
 * - `PyList_SetItem` は第3引数の参照を**steal**する（呼び出し後、別途Py_DECREFしない）。
 * - `PyTuple_Pack` は渡した各引数の参照カウントを**増やす**（stealしない）ため、
 *   渡した`pList`は呼び出し後も自分でPy_DECREFする責任が残る。
 * このような「所有権の受け渡しルール」は、`cpp_m/cpp_basics_class_memory_ownership.cpp`で
 * 見た「関数がポインタの所有権を受け取るか借りるだけか」という考え方そのものであり、
 * ドキュメント（Python公式のC API リファレンス）で関数ごとに必ず確認する必要がある。
 * @return void
 */
static void callPythonFunction(void) {
    printf("\n--- 2. call a Python function from C, with arguments and a return value ---\n");

    /* カレントディレクトリをモジュール検索パスに追加する（sensor_analysis.pyを見つけるため） */
    PyRun_SimpleString("import sys; sys.path.insert(0, '.')");

    PyObject *pModule = PyImport_ImportModule("sensor_analysis");
    if (pModule == NULL) {
        PyErr_Print();
        fprintf(stderr, "[error] function=callPythonFunction: sensor_analysis のimportに失敗しました\n");
        return;
    }

    PyObject *pFunc = PyObject_GetAttrString(pModule, "average");
    if (pFunc == NULL || !PyCallable_Check(pFunc)) {
        PyErr_Print();
        fprintf(stderr, "[error] function=callPythonFunction: average関数が見つかりません\n");
        Py_XDECREF(pFunc);
        Py_DECREF(pModule);
        return;
    }

    /* Pythonのlist [1.0, 2.0, 3.0, 4.0] をCから組み立てる */
    PyObject *pList = PyList_New(4);
    PyList_SetItem(pList, 0, PyFloat_FromDouble(1.0)); /* SetItemは参照をstealする */
    PyList_SetItem(pList, 1, PyFloat_FromDouble(2.0));
    PyList_SetItem(pList, 2, PyFloat_FromDouble(3.0));
    PyList_SetItem(pList, 3, PyFloat_FromDouble(4.0));

    PyObject *pArgs = PyTuple_Pack(1, pList); /* Pack はstealしないので、pListは後で別途解放が必要 */
    PyObject *pResult = PyObject_CallObject(pFunc, pArgs);

    if (pResult == NULL) {
        PyErr_Print();
        fprintf(stderr, "[error] function=callPythonFunction: average()の呼び出しに失敗しました\n");
    } else {
        double averageValue = PyFloat_AsDouble(pResult);
        printf("average([1.0, 2.0, 3.0, 4.0]) = %f\n", averageValue);
        Py_DECREF(pResult);
    }

    Py_DECREF(pArgs);
    Py_DECREF(pList);
    Py_DECREF(pFunc);
    Py_DECREF(pModule);
}

/**
 * @brief エントリポイント。Pythonインタプリタの初期化・実行・終了処理を行う。
 * @return int 終了コード
 */
int main(void) {
    Py_Initialize(); /* [重要] Python C APIを使う前に必ず呼ぶ。対応するPy_FinalizeExを忘れないこと */

    runSimpleString();
    callPythonFunction();

    if (Py_FinalizeEx() < 0) {
        return 1;
    }
    return 0;
}
