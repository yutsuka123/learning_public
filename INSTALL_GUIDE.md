# インストールガイド

## 🔧 必要な開発環境のインストール手順

### **現在の環境状況**
- ✅ Python 3.11.9 - インストール済み
- ✅ .NET 9.0.102 - インストール済み  
- ✅ Java 24.0.1 - インストール済み
- ❌ GCC/C++コンパイラ - 未インストール
- ❌ PyTorch - 未インストール
- ❌ OpenCV - 未インストール
- ❌ Kotlin - 未インストール
- ❌ TypeScript - 未インストール

---

## **1. C/C++ 開発環境（最優先）**

### **Visual Studio Build Tools のインストール**
```powershell
# Chocolateyを使用する場合
choco install visualstudio2022buildtools

# 手動インストールの場合
# https://visualstudio.microsoft.com/ja/downloads/ から
# "Visual Studio 2022 Build Tools" をダウンロードしてインストール
```

**または MinGW-w64 を使用:**
```powershell
# Chocolateyを使用
choco install mingw

# 手動インストール
# https://www.mingw-w64.org/ からダウンロード
```

---

## **2. Python ライブラリ**

### **PyTorch のインストール**
```powershell
# CPU版（推奨）
pip install torch torchvision torchaudio

# GPU版（CUDA対応GPUがある場合）
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu118
```

### **その他のPythonライブラリ**
```powershell
pip install numpy pandas matplotlib seaborn scikit-learn
pip install opencv-python  # Python用OpenCV
pip install jupyter notebook  # Jupyter Notebook（オプション）
```

---

## **3. OpenCV（C++用）**

### **vcpkg を使用したインストール（推奨）**
```powershell
# vcpkgのクローン
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# OpenCVのインストール
.\vcpkg install opencv4:x64-windows
.\vcpkg integrate install
```

### **手動インストール**
```powershell
# OpenCV公式サイトからダウンロード
# https://opencv.org/releases/
# Windows用のpre-built librariesをダウンロードして展開
```

---

## **4. Kotlin**

### **Kotlin コンパイラのインストール**
```powershell
# Chocolateyを使用
choco install kotlin

# 手動インストール
# https://kotlinlang.org/docs/command-line.html
# からKotlin command line compilerをダウンロード
```

---

## **5. TypeScript**

### **Node.js と TypeScript のインストール**
```powershell
# Node.jsのインストール
choco install nodejs

# TypeScriptのインストール
npm install -g typescript
npm install -g ts-node  # 直接実行用

# プロジェクト設定
npm init -y
npm install -D @types/node
```

---

## **6. 開発ツール（オプション）**

### **Chocolatey のインストール（未インストールの場合）**
```powershell
# PowerShellを管理者権限で実行
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
```

### **便利な開発ツール**
```powershell
choco install git
choco install vscode  # VS Code（未インストールの場合）
choco install cmake   # CMake（C++プロジェクト用）
```

---

## **7. 環境変数の設定**

### **OpenCV（手動インストールの場合）**
```powershell
# システム環境変数に追加
$env:OpenCV_DIR = "C:\opencv\build"
$env:PATH += ";C:\opencv\build\x64\vc16\bin"
```

### **Kotlin**
```powershell
# Kotlinのパスを追加
$env:PATH += ";C:\kotlin\bin"
```

---

## **8. インストール確認コマンド**

### **基本環境の確認**
```powershell
python --version
dotnet --version
java -version
gcc --version      # MinGW-w64インストール後
g++ --version      # MinGW-w64インストール後
```

### **ライブラリの確認**
```powershell
# Python
python -c "import torch; print(f'PyTorch: {torch.__version__}')"
python -c "import cv2; print(f'OpenCV: {cv2.__version__}')"

# Node.js/TypeScript
node --version
tsc --version

# Kotlin
kotlin -version
```

---

## **9. サンプル実行テスト**

### **各言語のテスト実行**
```powershell
# C#
cd csharp
dotnet run HelloWorld.cs

# Python
cd python
python hello_world.py
python pytorch_sample.py  # PyTorchインストール後

# Java
cd java
javac HelloWorld.java
java HelloWorld

# TypeScript
cd typescript
tsc hello_world.ts
node hello_world.js

# Kotlin
cd kotlin
kotlinc HelloWorld.kt -include-runtime -d HelloWorld.jar
java -jar HelloWorld.jar

# C++（OpenCVインストール後）
cd cpp
g++ -std=c++17 hello_world.cpp -o hello_world
./hello_world

# C言語
cd c
gcc -std=c99 hello_world.c -o hello_world
./hello_world
```

---

## **10. トラブルシューティング**

### **よくある問題と解決方法**

#### **C++コンパイルエラー**
```powershell
# Visual Studio Build Toolsが見つからない場合
# Developer Command Prompt for VS 2022を使用
```

#### **OpenCVが見つからない**
```powershell
# pkg-configを使用（Linux/Mac風）
# Windowsの場合は直接パスを指定
g++ -std=c++17 opencv_sample.cpp -o opencv_sample -IC:\opencv\build\include -LC:\opencv\build\x64\vc16\lib -lopencv_world480
```

#### **PyTorchのインポートエラー**
```powershell
# 依存関係の確認
pip install --upgrade pip
pip install torch --force-reinstall
```

#### **権限エラー**
```powershell
# PowerShellを管理者権限で実行
# または実行ポリシーを変更
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```