# Python 開発環境インストールガイド

## 📋 概要
Python開発とPyTorchを使用した機械学習・深層学習開発のための環境セットアップ手順を説明します。

## ✅ 前提条件
- Windows 10/11 または macOS、Linux
- 管理者権限でのコマンド実行が可能
- インターネット接続
- 8GB以上のRAM（PyTorch使用時）
- 5GB以上の空きディスク容量

## 🎯 インストール対象
- Python 3.8以降
- pip（パッケージマネージャー）
- PyTorch（機械学習フレームワーク）
- 科学計算ライブラリ（NumPy、Pandas、Matplotlib等）
- Jupyter Notebook（オプション）
- Visual Studio Code + Python 拡張機能

---

## 🐍 Python 環境のセットアップ

### **1. Python のインストール状況確認**

#### **現在の状況**
```powershell
python --version
```
✅ **既にインストール済み**: Python 3.11.9

#### **pip の確認・更新**
```bash
# pip のバージョン確認
pip --version

# pip の更新
python -m pip install --upgrade pip
```

### **2. 仮想環境の作成（推奨）**

#### **venv を使用した仮想環境**
```bash
# 仮想環境の作成
python -m venv pytorch_env

# 仮想環境の有効化
# Windows
pytorch_env\Scripts\activate

# macOS/Linux
source pytorch_env/bin/activate

# 仮想環境の無効化
deactivate
```

#### **conda を使用する場合（オプション）**
```bash
# Anacondaのインストール
# https://www.anaconda.com/products/distribution からダウンロード

# conda環境の作成
conda create -n pytorch_env python=3.11
conda activate pytorch_env
```

### **3. Visual Studio Code 拡張機能**

#### **必須拡張機能**
```bash
# Python Extension Pack
code --install-extension ms-python.python

# Pylance（高速な言語サーバー）
code --install-extension ms-python.vscode-pylance

# Python Debugger
code --install-extension ms-python.debugpy
```

#### **推奨拡張機能**
```bash
# Jupyter
code --install-extension ms-toolsai.jupyter

# autoDocstring（docstring自動生成）
code --install-extension njpwerner.autodocstring

# Python Type Hint
code --install-extension ms-python.mypy-type-checker

# Black Formatter（コードフォーマッター）
code --install-extension ms-python.black-formatter
```

---

## 🧠 PyTorch のインストール

### **4. PyTorch のインストール**

#### **CPU版（推奨開始）**
```bash
# 最新の安定版
pip install torch torchvision torchaudio

# 特定バージョン
pip install torch==2.1.0 torchvision==0.16.0 torchaudio==2.1.0
```

#### **GPU版（CUDA対応）**
```bash
# CUDA 11.8 対応版
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu118

# CUDA 12.1 対応版
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu121
```

#### **CUDA環境の確認**
```python
# Python で実行
import torch
print(f"PyTorch version: {torch.__version__}")
print(f"CUDA available: {torch.cuda.is_available()}")
if torch.cuda.is_available():
    print(f"CUDA version: {torch.version.cuda}")
    print(f"GPU count: {torch.cuda.device_count()}")
    print(f"GPU name: {torch.cuda.get_device_name(0)}")
```

### **5. 科学計算ライブラリのインストール**

#### **基本的な科学計算ライブラリ**
```bash
# NumPy（数値計算）
pip install numpy

# Pandas（データ分析）
pip install pandas

# Matplotlib（グラフ描画）
pip install matplotlib

# Seaborn（統計的可視化）
pip install seaborn

# SciPy（科学計算）
pip install scipy

# scikit-learn（機械学習）
pip install scikit-learn
```

#### **画像処理・コンピュータビジョン**
```bash
# OpenCV（画像処理）
pip install opencv-python

# Pillow（画像処理）
pip install Pillow

# ImageIO（画像読み込み）
pip install imageio
```

#### **深層学習関連**
```bash
# TensorBoard（可視化）
pip install tensorboard

# Transformers（自然言語処理）
pip install transformers

# Datasets（データセット）
pip install datasets

# TIMM（画像モデル）
pip install timm
```

### **6. 開発ツール**

#### **Jupyter Notebook**
```bash
# Jupyter Notebook
pip install jupyter

# JupyterLab（次世代Jupyter）
pip install jupyterlab

# Jupyter拡張機能
pip install jupyter_contrib_nbextensions
jupyter contrib nbextension install --user
```

#### **コード品質管理ツール**
```bash
# Black（コードフォーマッター）
pip install black

# isort（import文の整理）
pip install isort

# flake8（リンター）
pip install flake8

# mypy（型チェッカー）
pip install mypy

# pytest（テストフレームワーク）
pip install pytest
```

---

## 🏗️ プロジェクトの設定

### **7. requirements.txt の作成**

#### **基本的な requirements.txt**
```text
# requirements.txt
# 基本ライブラリ
numpy>=1.21.0
pandas>=1.3.0
matplotlib>=3.4.0
seaborn>=0.11.0
scipy>=1.7.0

# 機械学習
torch>=2.0.0
torchvision>=0.15.0
torchaudio>=2.0.0
scikit-learn>=1.0.0

# 画像処理
opencv-python>=4.5.0
Pillow>=8.0.0

# 開発ツール
jupyter>=1.0.0
black>=22.0.0
flake8>=4.0.0
pytest>=7.0.0

# 可視化
tensorboard>=2.8.0
```

#### **インストール**
```bash
pip install -r requirements.txt
```

### **8. VS Code の設定ファイル**

#### **.vscode/settings.json**
```json
{
    "python.defaultInterpreterPath": "./pytorch_env/Scripts/python.exe",
    "python.linting.enabled": true,
    "python.linting.pylintEnabled": false,
    "python.linting.flake8Enabled": true,
    "python.formatting.provider": "black",
    "python.formatting.blackArgs": ["--line-length", "79"],
    "python.sortImports.args": ["--profile", "black"],
    "[python]": {
        "editor.formatOnSave": true,
        "editor.codeActionsOnSave": {
            "source.organizeImports": true
        }
    },
    "jupyter.askForKernelRestart": false,
    "python.testing.pytestEnabled": true,
    "python.testing.unittestEnabled": false,
    "python.testing.pytestArgs": [
        "."
    ]
}
```

#### **.vscode/launch.json**
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Python: Current File",
            "type": "python",
            "request": "launch",
            "program": "${file}",
            "console": "integratedTerminal",
            "justMyCode": true
        },
        {
            "name": "Python: PyTorch Script",
            "type": "python",
            "request": "launch",
            "program": "${workspaceFolder}/python/pytorch_sample.py",
            "console": "integratedTerminal",
            "env": {
                "PYTHONPATH": "${workspaceFolder}"
            }
        }
    ]
}
```

### **9. 設定ファイル**

#### **pyproject.toml**
```toml
[tool.black]
line-length = 79
target-version = ['py38']

[tool.isort]
profile = "black"
line_length = 79

[tool.mypy]
python_version = "3.8"
warn_return_any = true
warn_unused_configs = true
disallow_untyped_defs = true

[tool.pytest.ini_options]
testpaths = ["tests"]
python_files = ["test_*.py", "*_test.py"]
```

#### **.flake8**
```ini
[flake8]
max-line-length = 79
extend-ignore = E203, W503
exclude = 
    .git,
    __pycache__,
    build,
    dist,
    *.egg-info,
    .venv,
    pytorch_env
```

---

## 🧪 動作確認

### **10. サンプルプロジェクトの実行**

#### **基本的なPython実行**
```bash
# プロジェクトディレクトリに移動
cd python

# Hello World の実行
python hello_world.py

# PyTorch サンプルの実行
python pytorch_sample.py
```

#### **Jupyter Notebook の起動**
```bash
# Jupyter Notebook 起動
jupyter notebook

# JupyterLab 起動
jupyter lab
```

#### **PyTorch 動作確認スクリプト**
```python
# pytorch_check.py
import torch
import numpy as np

def check_pytorch_installation():
    print("=== PyTorch インストール確認 ===")
    print(f"PyTorch version: {torch.__version__}")
    print(f"NumPy version: {np.__version__}")
    
    # CPU テンソル作成
    cpu_tensor = torch.randn(3, 3)
    print(f"CPU tensor:\n{cpu_tensor}")
    
    # CUDA確認
    if torch.cuda.is_available():
        print(f"CUDA available: Yes")
        print(f"CUDA version: {torch.version.cuda}")
        print(f"GPU count: {torch.cuda.device_count()}")
        
        # GPU テンソル作成
        gpu_tensor = cpu_tensor.cuda()
        print(f"GPU tensor device: {gpu_tensor.device}")
    else:
        print("CUDA available: No")
    
    # 自動微分テスト
    x = torch.randn(2, 2, requires_grad=True)
    y = x.pow(2).sum()
    y.backward()
    print(f"Gradient:\n{x.grad}")
    
    print("✅ PyTorch インストール確認完了!")

if __name__ == "__main__":
    check_pytorch_installation()
```

---

## 🔍 トラブルシューティング

### **よくある問題と解決方法**

#### **PyTorchのインポートエラー**
```bash
# 依存関係の確認
pip list | grep torch

# 再インストール
pip uninstall torch torchvision torchaudio
pip install torch torchvision torchaudio

# キャッシュクリア
pip cache purge
```

#### **CUDA関連エラー**
```bash
# CUDA ドライバーの確認
nvidia-smi

# CUDA Toolkit の確認
nvcc --version

# PyTorch CUDA バージョンの確認
python -c "import torch; print(torch.version.cuda)"
```

#### **メモリ不足エラー**
```python
# PyTorch メモリ使用量の確認
import torch
if torch.cuda.is_available():
    print(f"GPU memory allocated: {torch.cuda.memory_allocated()}")
    print(f"GPU memory cached: {torch.cuda.memory_reserved()}")
    
    # メモリクリア
    torch.cuda.empty_cache()
```

#### **VS Code で Python インタープリターが見つからない**
1. Ctrl+Shift+P でコマンドパレット
2. "Python: Select Interpreter" を選択
3. 仮想環境のPythonを選択

#### **Jupyter カーネルエラー**
```bash
# カーネルの確認
jupyter kernelspec list

# カーネルの追加
python -m ipykernel install --user --name pytorch_env --display-name "PyTorch"
```

---

## 🚀 パフォーマンス最適化

### **11. 高速化設定**

#### **NumPy/SciPy 最適化**
```bash
# Intel MKL（数値計算ライブラリ）
pip install intel-mkl

# OpenBLAS
pip install openblas
```

#### **PyTorch 最適化設定**
```python
# pytorch_config.py
import torch

# スレッド数の設定
torch.set_num_threads(4)  # CPUコア数に応じて調整

# CUDNN ベンチマークの有効化（GPU使用時）
if torch.cuda.is_available():
    torch.backends.cudnn.benchmark = True
    torch.backends.cudnn.deterministic = False

print("PyTorch 最適化設定完了")
```

---

## 📚 参考リンク

- [PyTorch 公式ドキュメント](https://pytorch.org/docs/stable/index.html)
- [NumPy 公式ドキュメント](https://numpy.org/doc/stable/)
- [Pandas 公式ドキュメント](https://pandas.pydata.org/docs/)
- [Matplotlib 公式ドキュメント](https://matplotlib.org/stable/contents.html)
- [Jupyter 公式ドキュメント](https://jupyter.org/documentation)

---

## 📝 インストール完了チェックリスト

- [ ] Python 3.8以降がインストール済み
- [ ] pip が最新版に更新済み
- [ ] 仮想環境が作成・有効化済み
- [ ] VS Code + Python 拡張機能がインストール済み
- [ ] PyTorch がインストール済み
- [ ] 基本的な科学計算ライブラリがインストール済み
- [ ] hello_world.py が正常に実行できる
- [ ] pytorch_sample.py が正常に実行できる
- [ ] Jupyter Notebook が起動できる
- [ ] PyTorch の動作確認が完了

**✅ すべて完了したら、機械学習・深層学習プロジェクトを開始できます！**