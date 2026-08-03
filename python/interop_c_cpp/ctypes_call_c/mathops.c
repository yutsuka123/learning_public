/**
 * @file mathops.c
 * @brief [重要] Python(ctypes)から呼び出すための小さなC共有ライブラリ。
 * @details
 * - 目的: `c/structures/struct_pointer_array_deep_dive.c` で学んだポインタ・配列・構造体が、
 *   Pythonの`ctypes`からどう見えるかを、実際に共有ライブラリとしてビルドして確認する。
 * - ビルド方法・実行方法は README.md を参照。
 */

/**
 * @brief 2つのintを足す、最も単純な関数。
 * @param a int
 * @param b int
 * @return int a+b
 */
int addInts(int a, int b) {
    return a + b;
}

/**
 * @brief double配列の平均値を計算する。
 * @param values const double* 配列の先頭ポインタ
 * @param count int 要素数
 * @return double 平均値（countが0以下なら0.0）
 */
double computeAverage(const double *values, int count) {
    if (count <= 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += values[i];
    }
    return sum / count;
}

/**
 * @brief 配列の各要素をfactor倍し、その場で書き換える。
 * @details [重要] ポインタ越しに呼び出し元のメモリを直接書き換える。
 * ctypes越しでもポインタは「同じメモリを指す」ため、この変更はPython側にもそのまま見える。
 * @param values double* 書き換え対象の配列
 * @param count int 要素数
 * @param factor double 掛ける係数
 * @return void
 */
void scaleArrayInPlace(double *values, int count, double factor) {
    for (int i = 0; i < count; i++) {
        values[i] *= factor;
    }
}

/**
 * @brief センサー読み取り結果を表す構造体。
 */
typedef struct {
    int id;
    double value;
} SensorReading;

/**
 * @brief SensorReadingを組み立てて値渡しで返す。
 * @param id int センサーID
 * @param value double 測定値
 * @return SensorReading 組み立てた構造体（値渡し）
 */
SensorReading makeSensorReading(int id, double value) {
    SensorReading reading;
    reading.id = id;
    reading.value = value;
    return reading;
}

/**
 * @brief SensorReading(値渡し)からvalueフィールドだけ取り出す。
 * @param reading SensorReading 値渡しされた構造体
 * @return double reading.value
 */
double sensorReadingValue(SensorReading reading) {
    return reading.value;
}
