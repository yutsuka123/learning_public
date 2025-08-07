# C++ 開発環境インストールガイド

## 📋 概要
C++開発とOpenCVライブラリを使用した画像処理開発のための環境セットアップ手順を説明します。

## ✅ 前提条件
- Windows 10/11 または macOS、Linux
- 管理者権限でのコマンド実行が可能
- インターネット接続
- 4GB以上の空きディスク容量

## 🎯 インストール対象
- C++コンパイラ（GCC/Clang または Visual Studio Build Tools）
- CMake（ビルドシステム）
- OpenCV 4.x（画像処理ライブラリ）
- vcpkg（パッケージマネージャー）
- Visual Studio Code + C++ 拡張機能

---

## 🔧 基本環境のセットアップ

### **1. C++ コンパイラのインストール**

#### **Windows の場合**

##### **オプション A: Visual Studio Build Tools（推奨）**
```powershell
# Chocolateyを使用
choco install visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools"

# 手動インストール
# https://visualstudio.microsoft.com/ja/downloads/
# から "Build Tools for Visual Studio 2022" をダウンロード
```

##### **オプション B: MinGW-w64**
```powershell
# Chocolateyを使用
choco install mingw

# 手動インストール
# https://www.mingw-w64.org/downloads/
# から MSYS2 をダウンロードしてインストール
```

#### **macOS の場合**
```bash
# Xcode Command Line Tools
xcode-select --install

# Homebrewでの追加ツール
brew install cmake
brew install pkg-config
```

#### **Linux (Ubuntu/Debian) の場合**
```bash
# 基本的な開発ツール
sudo apt update
sudo apt install build-essential cmake pkg-config

# 追加の開発ライブラリ
sudo apt install libjpeg-dev libpng-dev libtiff-dev
sudo apt install libavcodec-dev libavformat-dev libswscale-dev
```

### **2. CMake のインストール**

#### **Windows**
```powershell
# Chocolateyを使用
choco install cmake

# 手動インストール
# https://cmake.org/download/ からダウンロード
```

#### **確認**
```bash
cmake --version
```

### **3. Visual Studio Code 拡張機能**

#### **必須拡張機能**
```bash
# C/C++ Extension Pack
code --install-extension ms-vscode.cpptools-extension-pack

# C/C++ IntelliSense
code --install-extension ms-vscode.cpptools

# CMake Tools
code --install-extension ms-vscode.cmake-tools
```

---

## 📷 OpenCV のインストール

### **4. vcpkg パッケージマネージャーの設定**

#### **vcpkg のインストール**
```powershell
# リポジトリのクローン
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# Windows
.\bootstrap-vcpkg.bat

# macOS/Linux
./bootstrap-vcpkg.sh
```

#### **Visual Studio との統合**
```powershell
# Windows（Visual Studio Build Tools使用時）
.\vcpkg integrate install
```

### **5. OpenCV のインストール**

#### **vcpkg を使用したインストール（推奨）**
```powershell
# Windows (64-bit)
.\vcpkg install opencv4:x64-windows

# macOS
./vcpkg install opencv4:x64-osx

# Linux
./vcpkg install opencv4:x64-linux
```

#### **手動インストール（Windows）**
```powershell
# OpenCV公式サイトからダウンロード
# https://opencv.org/releases/
# Windows用のpre-built librariesをダウンロード

# 環境変数の設定
$env:OpenCV_DIR = "C:\opencv\build"
$env:PATH += ";C:\opencv\build\x64\vc16\bin"
```

#### **パッケージマネージャーを使用（macOS）**
```bash
# Homebrew
brew install opencv

# 環境変数の設定
export OpenCV_DIR=/opt/homebrew/lib/cmake/opencv4
```

#### **パッケージマネージャーを使用（Linux）**
```bash
# Ubuntu/Debian
sudo apt install libopencv-dev

# CentOS/RHEL/Fedora
sudo dnf install opencv-devel
```

---

## 🏗️ プロジェクトの設定

### **6. CMakeLists.txt の作成**

#### **基本的なCMakeLists.txt**
```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(CppLearning)

# C++17を使用
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenCVを検索
find_package(OpenCV REQUIRED)

# 実行ファイルの作成
add_executable(hello_world hello_world.cpp)
add_executable(opencv_sample opencv_sample.cpp)

# OpenCVライブラリをリンク
target_link_libraries(opencv_sample ${OpenCV_LIBS})

# インクルードディレクトリの設定
target_include_directories(opencv_sample PRIVATE ${OpenCV_INCLUDE_DIRS})

# コンパイルオプション
target_compile_options(hello_world PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

target_compile_options(opencv_sample PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
```

### **7. VS Code の設定ファイル**

#### **.vscode/c_cpp_properties.json**
```json
{
    "configurations": [
        {
            "name": "Win32",
            "includePath": [
                "${workspaceFolder}/**",
                "C:/vcpkg/installed/x64-windows/include"
            ],
            "defines": [
                "_DEBUG",
                "UNICODE",
                "_UNICODE"
            ],
            "windowsSdkVersion": "10.0.22000.0",
            "compilerPath": "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.37.32822/bin/Hostx64/x64/cl.exe",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-msvc-x64",
            "configurationProvider": "ms-vscode.cmake-tools"
        }
    ],
    "version": 4
}
```

#### **.vscode/settings.json**
```json
{
    "cmake.configureArgs": [
        "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
    ],
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools"
}
```

---

## 🧪 動作確認

### **8. サンプルプロジェクトのビルドと実行**

#### **CMake を使用したビルド**
```bash
# プロジェクトディレクトリに移動
cd cpp

# ビルドディレクトリの作成
mkdir build
cd build

# CMakeの設定
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# ビルド
cmake --build . --config Release

# 実行
./Release/hello_world.exe      # Windows
./Release/opencv_sample.exe    # Windows

./hello_world                  # macOS/Linux
./opencv_sample                # macOS/Linux
```

#### **直接コンパイル（テスト用）**
```bash
# 基本的なHello World
g++ -std=c++17 hello_world.cpp -o hello_world

# OpenCVサンプル（pkg-config使用）
g++ -std=c++17 opencv_sample.cpp -o opencv_sample `pkg-config --cflags --libs opencv4`

# OpenCVサンプル（手動リンク - Windows）
g++ -std=c++17 opencv_sample.cpp -o opencv_sample -IC:/opencv/build/include -LC:/opencv/build/x64/vc16/lib -lopencv_world480
```

### **9. OpenCV 動作確認**

#### **簡単な動作確認コード**
```cpp
// opencv_test.cpp
#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    std::cout << "OpenCV バージョン: " << CV_VERSION << std::endl;
    
    // 簡単な画像作成テスト
    cv::Mat image = cv::Mat::zeros(300, 400, CV_8UC3);
    image.setTo(cv::Scalar(100, 150, 200));
    
    std::cout << "画像作成成功: " << image.rows << "x" << image.cols << std::endl;
    
    return 0;
}
```

---

## 🔍 トラブルシューティング

### **よくある問題と解決方法**

#### **OpenCVが見つからない**
```bash
# pkg-configの確認
pkg-config --modversion opencv4

# 環境変数の確認
echo $OpenCV_DIR
echo $PKG_CONFIG_PATH
```

#### **コンパイラが見つからない**
```powershell
# Visual Studio Build Tools の確認
where cl.exe

# MinGW の確認
where g++.exe
where gcc.exe
```

#### **CMake エラー**
```bash
# CMake キャッシュのクリア
rm -rf build/
mkdir build

# 詳細なログ出力
cmake .. --debug-output
```

#### **リンクエラー**
```bash
# ライブラリパスの確認
ldd ./opencv_sample  # Linux
otool -L ./opencv_sample  # macOS

# Windows DLL パスの確認
echo $env:PATH
```

---

## 🚀 パフォーマンス最適化

### **10. 最適化オプション**

#### **CMakeLists.txt での最適化設定**
```cmake
# リリースビルドでの最適化
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# OpenMP サポート（並列処理）
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(opencv_sample OpenMP::OpenMP_CXX)
endif()

# AVX/SSE最適化（Intel CPU）
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(opencv_sample PRIVATE -mavx2 -msse4.2)
endif()
```

---

## 📚 参考リンク

- [OpenCV 公式ドキュメント](https://docs.opencv.org/)
- [vcpkg パッケージマネージャー](https://github.com/Microsoft/vcpkg)
- [CMake 公式ドキュメント](https://cmake.org/documentation/)
- [現代的なC++ プログラミング](https://isocpp.org/)

---

## 📝 インストール完了チェックリスト

- [ ] C++コンパイラ（GCC/Clang/MSVC）がインストール済み
- [ ] CMake 3.16以降がインストール済み
- [ ] VS Code + C++ 拡張機能がインストール済み
- [ ] vcpkg がインストール・設定済み
- [ ] OpenCV 4.x がインストール済み
- [ ] hello_world.cpp が正常にコンパイル・実行できる
- [ ] opencv_sample.cpp が正常にコンパイル・実行できる
- [ ] CMakeプロジェクトがビルドできる

**✅ すべて完了したら、OpenCVを使用した高度な画像処理プロジェクトを開始できます！**