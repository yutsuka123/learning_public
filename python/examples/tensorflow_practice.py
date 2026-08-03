"""
TensorFlow 実践入門サンプル（PyTorch版 pytorch_practice.py との比較用）。

概要:
    テンソル操作・自動微分(GradientTape)・最小の訓練ループを、`pytorch_practice.py`と
    同じ題材（合成データでの回帰 y ≈ Wx + b）で確認する。両ファイルを読み比べることで、
    「同じことをする」ときのAPIの違いを把握できる。
主な仕様:
    - demonstrateTensorBasics(): テンソルの作成・演算・shape確認。
    - demonstrateGradientTape(): `tf.GradientTape`による自動微分
      （PyTorchの`loss.backward()`に相当）。
    - trainLinearRegression(): 合成データ上の回帰を数ステップ学習し、損失が下がることを確認する。
制限事項:
    - [重要] TensorFlow（検証時点の最新版）はPython 3.14に対応しておらず、本ファイルは
      **Python 3.11の別仮想環境**で検証している（`python/examples/`の他ファイルが使う
      `.venv-examples`とは別。セットアップ手順を参照）。
    - 本ファイルは教育用の最小例である。大規模データ・分散学習・本番推論は対象外。

実行例:
    python3.11 -m venv .venv-tensorflow
    source .venv-tensorflow/bin/activate
    pip install tensorflow
    python tensorflow_practice.py
    # 検証環境: Python 3.11, tensorflow 2.21.0 で実行結果を確認済み。
"""

import numpy as np
import tensorflow as tf


def demonstrateTensorBasics():
    """
    テンソルの作成・演算・shape確認の基本を確認する。

    実行結果（例）:
        a=[1. 2. 3.], shape=(3,), dtype=<dtype: 'float32'>
        a + 10 = [11. 12. 13.]
        matmul result:
        [[19. 22.]
         [43. 50.]]
    """

    print("=== 1. tensor basics ===")

    a = tf.constant([1.0, 2.0, 3.0])
    print(f"a={a.numpy()}, shape={a.shape}, dtype={a.dtype}")

    print(f"a + 10 = {(a + 10).numpy()}")

    m1 = tf.constant([[1.0, 2.0], [3.0, 4.0]])
    m2 = tf.constant([[5.0, 6.0], [7.0, 8.0]])
    print(f"matmul result:\n{tf.matmul(m1, m2).numpy()}")


def demonstrateGradientTape():
    """
    `tf.GradientTape`による自動微分を確認する。

    目的:
        `y = x^2` の `x=3` における微分（`dy/dx = 2x = 6`）を、手で計算するのではなく
        TensorFlowの自動微分機能で求める。PyTorchの`loss.backward()` + `x.grad`に相当する。

    実行結果（例）:
        y = x^2 at x=3.0: y=9.0
        dy/dx at x=3.0: 6.0 (expected 2*x=6.0)
    """

    print("\n=== 2. automatic differentiation (tf.GradientTape) ===")

    x = tf.Variable(3.0)  # [重要] 勾配を追跡したい変数は tf.Variable にする（tf.constantでは不可）
    with tf.GradientTape() as tape:
        y = x**2

    dy_dx = tape.gradient(y, x)
    print(f"y = x^2 at x={x.numpy()}: y={y.numpy()}")
    print(f"dy/dx at x={x.numpy()}: {dy_dx.numpy()} (expected 2*x=6.0)")


def trainLinearRegression(steps=200, learningRate=0.05, logEvery=20):
    """
    合成データ上の回帰(y ≈ Wx + b)を学習し、損失が下がることを確認する。

    `pytorch_practice.py`と同じ題材・同じハイパーパラメータで、TensorFlow版の
    訓練ループがどう書けるかを比較できるようにしている。

    引数:
        steps (int): 更新回数。
        learningRate (float): 学習率。
        logEvery (int): 何ステップごとに損失を表示するか。

    実行結果（例。乱数シード固定のため再現可能。TensorFlowのバージョン/実行環境により
    浮動小数点演算の丸め方が変わり、末尾桁が多少変動しうる）:
        step 0: loss=36.3575
        step 20: loss=0.2581
        step 40: loss=0.2422
        step 60: loss=0.2419
        (以降 loss=0.2419 でほぼ収束)
        final W=2.0169 (target 2.0), final b=1.0298 (target 1.0)
    """

    print("\n=== 3. train linear regression (y ~= Wx + b) ===")

    rng = np.random.default_rng(seed=0)
    trueW, trueB = 2.0, 1.0
    xTrain = rng.uniform(-5, 5, size=(64, 1)).astype(np.float32)
    noise = rng.normal(scale=0.5, size=(64, 1)).astype(np.float32)
    yTrain = (trueW * xTrain + trueB + noise).astype(np.float32)

    W = tf.Variable(0.0)
    b = tf.Variable(0.0)
    optimizer = tf.keras.optimizers.SGD(learning_rate=learningRate)

    for step in range(steps):
        with tf.GradientTape() as tape:
            predictions = W * xTrain + b
            loss = tf.reduce_mean(tf.square(predictions - yTrain))  # 平均二乗誤差(MSE)

        gradients = tape.gradient(loss, [W, b])
        optimizer.apply_gradients(zip(gradients, [W, b]))

        if step % logEvery == 0:
            print(f"step {step}: loss={loss.numpy():.4f}")

    print(f"final W={W.numpy():.4f} (target {trueW}), final b={b.numpy():.4f} (target {trueB})")


if __name__ == "__main__":
    demonstrateTensorBasics()
    demonstrateGradientTape()
    trainLinearRegression()
