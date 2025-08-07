# hello_world & OpenCV サンプル ビルド & 実行ガイド

このドキュメントでは、`cpp/hello_world.cpp`（内部で `runOpenCvSample()` を呼び出し）を **CMake** でビルドし、実行するまでの手順をまとめています。PowerShell／コマンドプロンプトにそのまま貼り付けてお使いください。

---

## 1. 前提条件

| 必須 | バージョン例 | 補足 |
|------|--------------|------|
| CMake | 3.10 以上 | `cmake --version` で確認 |
| C++ コンパイラ | C++11 対応 | Windows なら Visual Studio 2022 など |
| OpenCV | 4.0 以上 | `opencv_world4xx.dll` などがインストール済みで、`OpenCV_DIR` が設定されていること |
| ビルドツール | (任意) Ninja | 高速ビルドに便利 |

> **Tips**  
> MSVC を使用する場合、`cpp/CMakeLists.txt` に `/utf-8` オプションを追加済みのため、日本語文字列も文字化けしません。

---

## 2. 作業ディレクトリへ移動
```pwsh
cd C:\mydata\project\myproject\learning_public\cpp
```

---

## 3. ビルド用ディレクトリの生成と CMake 設定
`build` ディレクトリを新規作成するか、既存キャッシュを削除してから CMake を実行します。

### 3-0. 既存キャッシュを削除したい場合
```pwsh
# 旧設定を完全に削除
Remove-Item -Recurse -Force build
```

### 3-A. Visual Studio 17 2022 (MSVC) を利用する場合
```pwsh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

### 3-B. Ninja を利用する場合
```pwsh
cmake -S . -B build -G Ninja
```

### 3-C. 既定 (Unix Makefiles／MinGW など)
```pwsh
cmake -S . -B build
```

> `-S` はソースディレクトリ、`-B` はビルドディレクトリを表します。

---

## 4. コンパイル (ターゲット: hello_world)
ジェネレータによって手順が異なります。

### 4-A. Visual Studio ジェネレータ
Debug ビルド (例):
```pwsh
cmake --build build --config Debug --target hello_world
```

### 4-B. Ninja／Makefiles
Release ビルド (例):
```pwsh
cmake --build build --config Release --target hello_world
```

---

## 5. 実行
| ジェネレータ | 実行例 |
|--------------|--------|
| Visual Studio (Debug) | `./build/Debug/hello_world.exe` |
| Ninja／Makefiles      | `./build/hello_world.exe`       |

実行すると以下を確認できます。
1. コンソールに "Hello, World!" と計算結果 `3` が表示。
2. 古典的メモリ管理とスマートポインタのデモ。
3. OpenCV による図形描画サンプルが走り、`opencv_sample.png` が生成されます。

---

## 6. よくあるエラーと対処
| エラー内容 | 原因・対策 |
|------------|-----------|
| `error C1075: '{': 一致するトークンが見つかりませんでした` | ソースコードの波括弧 `{}` が対応していない箇所があります。エラーメッセージに表示される行番号付近を確認し、開き括弧と閉じ括弧の数を合わせてください。 |
| `generator platform: x64 Does not match the platform used previously` | 既存 `build` ディレクトリが別プラットフォームで生成済みです。`Remove-Item -Recurse -Force build` で削除するか、`-B build_x64` のように新ディレクトリを指定してください。 |
| `OpenCV not found` (CMake) | OpenCV のパスが見つかりません。環境変数 `OpenCV_DIR` を設定するか、`-D OpenCV_DIR="C:/path/to/opencv/build"` を CMake 実行時に追加してください。 |

---

## 7. 参考情報
* `cpp/CMakeLists.txt` では OpenCV を `find_package(OpenCV REQUIRED)` で検出し、ライブラリとヘッダを自動リンクしています。
* PNG 画像は実行ディレクトリ（`cpp/` または `build/` 直下）に `opencv_sample.png` として出力されます。
* CMake キャッシュ削除後は、必ず 3. から手順をやり直してください。

---

以上で `hello_world` ＆ OpenCV サンプルのビルド・実行手順は完了です。問題が解決しない場合は、発生したエラーメッセージを添えてご相談ください。
