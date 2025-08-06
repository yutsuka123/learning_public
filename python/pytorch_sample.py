"""
PyTorch サンプルプログラム
概要: PyTorchライブラリを使用した基本的な行列計算とニューラルネットワークのサンプル
主な仕様:
- テンソル操作の基本
- 自動微分（autograd）の使用
- 簡単なニューラルネットワークの定義と訓練
- 行列計算のサンプル
制限事項: PyTorch 1.9以降が推奨
"""

import torch
import torch.nn as nn
import torch.optim as optim
import torch.nn.functional as F
from typing import Tuple, List
import numpy as np


class TensorOperations:
    """
    PyTorchテンソル操作のサンプルクラス
    基本的な行列計算とテンソル操作を実装
    """
    
    def __init__(self) -> None:
        """
        コンストラクタ
        PyTorchのバージョンと利用可能なデバイスを確認
        """
        try:
            print(f"PyTorch バージョン: {torch.__version__}")
            self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
            print(f"使用デバイス: {self.device}")
            
            if torch.cuda.is_available():
                print(f"CUDA デバイス名: {torch.cuda.get_device_name(0)}")
            
        except Exception as e:
            print(f"エラーが発生しました - クラス: TensorOperations, メソッド: __init__, エラー: {e}")
            raise
    
    def basic_tensor_operations(self) -> None:
        """
        基本的なテンソル操作のサンプル
        テンソルの作成、操作、計算を実行
        """
        try:
            print("\n=== 基本的なテンソル操作 ===")
            
            # テンソルの作成
            tensor_a = torch.tensor([[1.0, 2.0, 3.0], 
                                   [4.0, 5.0, 6.0]], device=self.device)
            tensor_b = torch.tensor([[7.0, 8.0, 9.0], 
                                   [10.0, 11.0, 12.0]], device=self.device)
            
            print(f"テンソル A:\n{tensor_a}")
            print(f"テンソル A のshape: {tensor_a.shape}")
            print(f"テンソル A のデータ型: {tensor_a.dtype}")
            print()
            
            print(f"テンソル B:\n{tensor_b}")
            print(f"テンソル B のshape: {tensor_b.shape}")
            print()
            
            # 基本的な算術演算
            addition_result = tensor_a + tensor_b
            multiplication_result = tensor_a * tensor_b
            
            print(f"加算結果 (A + B):\n{addition_result}")
            print(f"要素ごとの乗算 (A * B):\n{multiplication_result}")
            print()
            
            # 行列積
            matrix_c = torch.tensor([[1.0, 2.0], 
                                   [3.0, 4.0], 
                                   [5.0, 6.0]], device=self.device)
            
            matrix_product = torch.mm(tensor_a, matrix_c)
            print(f"行列積 (A @ C):\n{matrix_product}")
            print(f"行列積の shape: {matrix_product.shape}")
            print()
            
        except Exception as e:
            print(f"エラーが発生しました - クラス: TensorOperations, メソッド: basic_tensor_operations, エラー: {e}")
            raise
    
    def advanced_matrix_operations(self) -> None:
        """
        高度な行列計算のサンプル
        固有値、特異値分解、逆行列などを計算
        """
        try:
            print("\n=== 高度な行列計算 ===")
            
            # ランダムな正方行列を生成
            torch.manual_seed(42)  # 再現性のためのシード設定
            matrix = torch.randn(4, 4, device=self.device)
            symmetric_matrix = matrix @ matrix.T  # 対称行列を作成
            
            print(f"対称行列:\n{symmetric_matrix}")
            print()
            
            # 固有値と固有ベクトル
            eigenvalues, eigenvectors = torch.linalg.eigh(symmetric_matrix)
            print(f"固有値:\n{eigenvalues}")
            print(f"固有ベクトル:\n{eigenvectors}")
            print()
            
            # 特異値分解 (SVD)
            U, S, Vh = torch.linalg.svd(matrix)
            print(f"特異値分解の特異値:\n{S}")
            print()
            
            # 逆行列（可逆な場合）
            try:
                matrix_inv = torch.linalg.inv(symmetric_matrix)
                identity_check = torch.mm(symmetric_matrix, matrix_inv)
                print(f"逆行列との積（単位行列に近似）:\n{identity_check}")
                print()
            except Exception as inv_error:
                print(f"逆行列計算でエラー: {inv_error}")
            
            # ノルム計算
            frobenius_norm = torch.linalg.norm(matrix, ord='fro')
            print(f"フロベニウスノルム: {frobenius_norm:.4f}")
            
        except Exception as e:
            print(f"エラーが発生しました - クラス: TensorOperations, メソッド: advanced_matrix_operations, エラー: {e}")
            raise
    
    def autograd_example(self) -> None:
        """
        自動微分（autograd）のサンプル
        勾配計算の基本的な使用方法を実演
        """
        try:
            print("\n=== 自動微分のサンプル ===")
            
            # 勾配計算を有効にしたテンソル
            x = torch.tensor([2.0, 3.0], requires_grad=True, device=self.device)
            print(f"入力テンソル x: {x}")
            
            # 関数 f(x) = x₁² + 2*x₂³ + x₁*x₂ を定義
            y = x[0]**2 + 2*x[1]**3 + x[0]*x[1]
            print(f"関数値 y = x₁² + 2*x₂³ + x₁*x₂ = {y.item():.4f}")
            
            # 勾配計算
            y.backward()
            print(f"勾配 ∂y/∂x: {x.grad}")
            
            # 理論値との比較
            # ∂y/∂x₁ = 2*x₁ + x₂ = 2*2 + 3 = 7
            # ∂y/∂x₂ = 6*x₂² + x₁ = 6*9 + 2 = 56
            theoretical_grad = torch.tensor([2*x[0].item() + x[1].item(), 
                                           6*x[1].item()**2 + x[0].item()], 
                                          device=self.device)
            print(f"理論値: {theoretical_grad}")
            print(f"誤差: {torch.abs(x.grad - theoretical_grad)}")
            
        except Exception as e:
            print(f"エラーが発生しました - クラス: TensorOperations, メソッド: autograd_example, エラー: {e}")
            raise


class SimpleNeuralNetwork(nn.Module):
    """
    簡単なニューラルネットワークのサンプルクラス
    多層パーセプトロンを実装
    """
    
    def __init__(self, input_size: int, hidden_size: int, output_size: int) -> None:
        """
        コンストラクタ
        
        Args:
            input_size (int): 入力層のサイズ
            hidden_size (int): 隠れ層のサイズ
            output_size (int): 出力層のサイズ
        """
        super(SimpleNeuralNetwork, self).__init__()
        
        try:
            self.input_size = input_size
            self.hidden_size = hidden_size
            self.output_size = output_size
            
            # 層の定義
            self.linear1 = nn.Linear(input_size, hidden_size)
            self.linear2 = nn.Linear(hidden_size, hidden_size)
            self.linear3 = nn.Linear(hidden_size, output_size)
            self.dropout = nn.Dropout(0.2)
            
            print(f"ニューラルネットワークを作成しました: {input_size} -> {hidden_size} -> {hidden_size} -> {output_size}")
            
        except Exception as e:
            print(f"エラーが発生しました - クラス: SimpleNeuralNetwork, メソッド: __init__, エラー: {e}")
            raise
    
    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """
        順伝播の定義
        
        Args:
            x (torch.Tensor): 入力テンソル
            
        Returns:
            torch.Tensor: 出力テンソル
        """
        try:
            # 第1層 + ReLU活性化関数
            x = F.relu(self.linear1(x))
            x = self.dropout(x)
            
            # 第2層 + ReLU活性化関数
            x = F.relu(self.linear2(x))
            x = self.dropout(x)
            
            # 出力層
            x = self.linear3(x)
            
            return x
            
        except Exception as e:
            print(f"エラーが発生しました - クラス: SimpleNeuralNetwork, メソッド: forward, エラー: {e}")
            raise
    
    def train_sample_data(self, device: torch.device) -> None:
        """
        サンプルデータでの訓練例
        
        Args:
            device (torch.device): 計算デバイス
        """
        try:
            print(f"\n=== ニューラルネットワーク訓練サンプル ===")
            
            # サンプルデータの生成（回帰問題）
            torch.manual_seed(42)
            n_samples = 100
            X = torch.randn(n_samples, self.input_size, device=device)
            # 目標関数: y = sum(x^2) + ノイズ
            y = torch.sum(X**2, dim=1, keepdim=True) + 0.1 * torch.randn(n_samples, 1, device=device)
            
            print(f"訓練データ: X.shape={X.shape}, y.shape={y.shape}")
            
            # モデルをデバイスに移動
            self.to(device)
            
            # 損失関数と最適化器
            criterion = nn.MSELoss()
            optimizer = optim.Adam(self.parameters(), lr=0.01)
            
            # 訓練ループ
            epochs = 100
            for epoch in range(epochs):
                # 順伝播
                outputs = self.forward(X)
                loss = criterion(outputs, y)
                
                # 逆伝播
                optimizer.zero_grad()
                loss.backward()
                optimizer.step()
                
                # 進捗表示
                if (epoch + 1) % 20 == 0:
                    print(f"エポック [{epoch+1}/{epochs}], 損失: {loss.item():.6f}")
            
            # 最終的な予測精度
            with torch.no_grad():
                final_outputs = self.forward(X)
                final_loss = criterion(final_outputs, y)
                print(f"最終損失: {final_loss.item():.6f}")
                
                # いくつかの予測例を表示
                print("\n予測例:")
                for i in range(min(5, n_samples)):
                    actual = y[i].item()
                    predicted = final_outputs[i].item()
                    print(f"サンプル {i+1}: 実際値={actual:.4f}, 予測値={predicted:.4f}, 誤差={abs(actual-predicted):.4f}")
                    
        except Exception as e:
            print(f"エラーが発生しました - クラス: SimpleNeuralNetwork, メソッド: train_sample_data, エラー: {e}")
            raise


def main() -> None:
    """
    メイン関数
    PyTorchのサンプル実行
    """
    try:
        print("=== PyTorch サンプルプログラム ===")
        
        # テンソル操作のサンプル
        tensor_ops = TensorOperations()
        tensor_ops.basic_tensor_operations()
        tensor_ops.advanced_matrix_operations()
        tensor_ops.autograd_example()
        
        # ニューラルネットワークのサンプル
        input_size = 10
        hidden_size = 20
        output_size = 1
        
        neural_net = SimpleNeuralNetwork(input_size, hidden_size, output_size)
        neural_net.train_sample_data(tensor_ops.device)
        
        print("\nプログラムが正常に終了しました。")
        
    except Exception as e:
        print(f"予期しないエラーが発生しました - 関数: main, エラー: {e}")
        raise


if __name__ == "__main__":
    main()