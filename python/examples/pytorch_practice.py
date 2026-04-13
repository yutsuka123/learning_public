"""
PyTorch 実践入門サンプル（ファイル分割版）

概要:
    テンソル、自動微分、最小の訓練ループを「実務で説明できる粒度」までコメントで補足する。
主な仕様:
    - CPU を既定とし、CUDA が利用可能なら自動で GPU を選択する（device の明示的切替の練習）。
    - 合成データ上の回帰（y ≈ Wx + b）を数ステップで学習し、損失が下がることを確認する。
制限事項:
    - 本ファイルは教育用の最小例である。大規模データ・分散学習・本番推論は対象外。
    - より長いニューラルネットワーク例はリポジトリ直下の pytorch_sample.py を参照。

実行例:
    pip install torch
    python pytorch_practice.py
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Tuple

import torch
from torch import Tensor, nn


@dataclass(frozen=True)
class TrainConfig:
    """
    訓練のハイパーパラメータをまとめる設定オブジェクト。

    目的:
        マジックナンバーを散らさず、関数引数を増やしすぎないようにする。
    属性:
        learningRate (float): 最適化の学習率。大きすぎると発散、小さすぎると遅い。
        steps (int): 更新回数。デモ用に小さめ。
        logEvery (int): 何ステップごとに損失を表示するか。
    """

    learningRate: float = 0.05
    steps: int = 200
    logEvery: int = 20


def resolveDevice() -> torch.device:
    """
    計算デバイスを決定する。

    目的:
        CUDA 有無で分岐し、後続コードで device を一貫させる。
    戻り値:
        torch.device: "cuda" または "cpu"。
    """

    try:
        if torch.cuda.is_available():
            return torch.device("cuda")
        return torch.device("cpu")
    except Exception as exc:  # pragma: no cover - 極端な環境向けの防御
        raise RuntimeError(
            f"resolveDevice: デバイス決定に失敗しました。torch={torch.__version__}, 引数なし, 原因={exc}"
        ) from exc


def makeSyntheticLinearData(
    numSamples: int,
    inputDim: int,
    device: torch.device,
    seed: int = 7,
) -> Tuple[Tensor, Tensor, Tensor, Tensor]:
    """
    線形関係 y = Xw + b + noise を模した合成データを生成する。

    目的:
        Dataset を用意せずに、訓練ループの骨格（forward / loss / backward / step）に集中する。
    引数:
        numSamples (int): サンプル数。0 以下は不正。
        inputDim (int): 特徴量次元。1 以上。
        device (torch.device): テンソルを載せるデバイス。
        seed (int): 乱数シード。再現性のデモ用。
    戻り値:
        Tuple[Tensor, Tensor, Tensor, Tensor]:
            features_X, targets_y, true_weights_w, true_bias_b
            - features_X: shape [numSamples, inputDim]
            - targets_y: shape [numSamples, 1]
            - true_weights_w: shape [inputDim, 1]（教師として隠した真の重み）
            - true_bias_b: shape [1, 1]
    """

    if numSamples <= 0:
        raise ValueError(f"makeSyntheticLinearData: numSamples は 1 以上である必要があります。受け取り値={numSamples}")
    if inputDim <= 0:
        raise ValueError(f"makeSyntheticLinearData: inputDim は 1 以上である必要があります。受け取り値={inputDim}")

    try:
        generator = torch.Generator(device=device)
        generator.manual_seed(seed)

        true_weights = torch.randn((inputDim, 1), device=device, generator=generator)
        true_bias = torch.randn((1, 1), device=device, generator=generator)

        features = torch.randn((numSamples, inputDim), device=device, generator=generator)
        noise = 0.05 * torch.randn((numSamples, 1), device=device, generator=generator)
        targets = features @ true_weights + true_bias + noise
        return features, targets, true_weights, true_bias
    except Exception as exc:
        raise RuntimeError(
            "makeSyntheticLinearData: 合成データ生成に失敗しました。"
            f" numSamples={numSamples}, inputDim={inputDim}, device={device}, seed={seed}, 原因={exc}"
        ) from exc


class TinyLinearModel(nn.Module):
    """
    1 層の線形モデル（y_hat = x @ W + b）を nn.Module として定義する。

    目的:
        nn.Parameter と optimizer がパラメータを共有する流れを最小構成で体験する。
    """

    def __init__(self, inputDim: int) -> None:
        super().__init__()
        if inputDim <= 0:
            raise ValueError(f"TinyLinearModel.__init__: inputDim は 1 以上である必要があります。受け取り値={inputDim}")
        # nn.Linear は内部で weight/bias を Parameter として登録する（optimizer が拾える）。
        self.linear = nn.Linear(in_features=inputDim, out_features=1, bias=True)

    def forward(self, features: Tensor) -> Tensor:
        """
        順伝播。入力特徴量から予測値を返す。

        引数:
            features (Tensor): shape [batch, inputDim]
        戻り値:
            Tensor: shape [batch, 1]
        """

        try:
            return self.linear(features)
        except Exception as exc:
            raise RuntimeError(
                f"TinyLinearModel.forward: 順伝播に失敗しました。features.shape={tuple(features.shape)}, 原因={exc}"
            ) from exc


def runTrainingLoop(
    model: TinyLinearModel,
    features: Tensor,
    targets: Tensor,
    config: TrainConfig,
) -> None:
    """
    勾配降下で線形回帰を学習する最小ループ。

    目的:
        「optimizer.zero_grad → backward → step」の順序を体に覚えさせる。
    引数:
        model (TinyLinearModel): 学習対象。
        features (Tensor): 入力。
        targets (Tensor): 教師信号。
        config (TrainConfig): 学習設定。
    戻り値:
        None（標準出力に損失ログを出す）。
    """

    loss_fn = nn.MSELoss()
    optimizer = torch.optim.SGD(model.parameters(), lr=config.learningRate)

    try:
        for step_index in range(1, config.steps + 1):
            model.train()
            predictions = model(features)
            loss = loss_fn(predictions, targets)

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            optimizer.step()

            if step_index == 1 or step_index % config.logEvery == 0 or step_index == config.steps:
                # item() で Python 数値に落とし、ログを読みやすくする。
                loss_value = float(loss.detach().cpu().item())
                print(
                    f"[train] step={step_index:4d}/{config.steps} "
                    f"loss(mse)={loss_value:.6f}"
                )
    except Exception as exc:
        raise RuntimeError(
            "runTrainingLoop: 訓練ループ中にエラーが発生しました。"
            f" steps={config.steps}, lr={config.learningRate}, features.shape={tuple(features.shape)}, "
            f"targets.shape={tuple(targets.shape)}, 原因={exc}"
        ) from exc


def compareParametersToTruth(
    model: TinyLinearModel,
    true_weights: Tensor,
    true_bias: Tensor,
) -> None:
    """
    学習後のパラメータと真の値をざっくり比較表示する。

    目的:
        「損失が下がった」だけでなく、パラメータが近づいたかを直感で確認する。
    """

    try:
        with torch.no_grad():
            learned_weight = model.linear.weight.detach().squeeze(0).unsqueeze(-1)
            learned_bias = model.linear.bias.detach().view(1, 1)

            weight_rmse = torch.sqrt(torch.mean((learned_weight - true_weights) ** 2)).item()
            bias_error = abs(float(learned_bias.item() - true_bias.item()))

            print("\n[compare] 真の重みと学習後の重みの RMSE（小さいほど良い）")
            print(f"  weight_rmse={weight_rmse:.6f}")
            print(f"  bias_abs_error={bias_error:.6f}")
    except Exception as exc:
        raise RuntimeError(
            "compareParametersToTruth: 比較表示に失敗しました。"
            f" true_weights.shape={tuple(true_weights.shape)}, true_bias.shape={tuple(true_bias.shape)}, 原因={exc}"
        ) from exc


def main() -> None:
    """
    エントリポイント。環境情報の表示とデモ訓練を実行する。
    """

    print("=== PyTorch 実践入門（examples/pytorch_practice.py）===")
    print(f"torch.__version__ = {torch.__version__}")

    device = resolveDevice()
    print(f"使用デバイス: {device}")

    num_samples = 512
    input_dim = 8
    features, targets, true_w, true_b = makeSyntheticLinearData(
        numSamples=num_samples,
        inputDim=input_dim,
        device=device,
    )

    model = TinyLinearModel(input_dim).to(device)
    config = TrainConfig()

    print(
        "\n[info] 合成データは y = Xw + b + noise。"
        f" num_samples={num_samples}, input_dim={input_dim}, steps={config.steps}, lr={config.learningRate}"
    )
    runTrainingLoop(model=model, features=features, targets=targets, config=config)
    compareParametersToTruth(model=model, true_weights=true_w, true_bias=true_b)

    print("\n完了。次のステップの学習指標:")
    print("- DataLoader + 自分の Dataset クラスでファイル読み込み")
    print("- train/eval 切替と torch.no_grad() の使い分け")
    print("- 検証データでの汎化評価（過学習の見方）")


if __name__ == "__main__":
    main()
