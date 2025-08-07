# C言語 開発環境インストールガイド

## 📋 概要
C言語開発のための基本的な環境セットアップ手順を説明します。システムプログラミング、組み込み開発、アルゴリズム学習に適した環境を構築します。

## ✅ 前提条件
- Windows 10/11 または macOS、Linux
- 管理者権限でのコマンド実行が可能
- インターネット接続
- 2GB以上の空きディスク容量

## 🎯 インストール対象
- C コンパイラ（GCC/Clang または Visual Studio Build Tools）
- Make（ビルドツール）
- デバッガー（GDB/LLDB）
- Valgrind（メモリリーク検出、Linux/macOS）
- Visual Studio Code + C 拡張機能

---

## 🔧 基本環境のセットアップ

### **1. C コンパイラのインストール**

#### **Windows の場合**

##### **オプション A: MinGW-w64（推奨）**
```powershell
# Chocolateyを使用
choco install mingw

# 手動インストール - MSYS2
# https://www.msys2.org/ からダウンロード
# インストール後、MSYS2 ターミナルで:
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-gdb
pacman -S mingw-w64-x86_64-make
```

##### **オプション B: Visual Studio Build Tools**
```powershell
# Chocolateyを使用
choco install visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools"
```

#### **macOS の場合**
```bash
# Xcode Command Line Tools（Clangコンパイラを含む）
xcode-select --install

# Homebrewでの追加ツール
brew install gcc          # GCCも使いたい場合
brew install make
brew install gdb          # デバッガー
```

#### **Linux (Ubuntu/Debian) の場合**
```bash
# 基本的な開発ツール
sudo apt update
sudo apt install build-essential

# 個別インストール
sudo apt install gcc
sudo apt install gdb
sudo apt install make
sudo apt install valgrind    # メモリリーク検出ツール
```

#### **Linux (CentOS/RHEL/Fedora) の場合**
```bash
# 開発ツールグループ
sudo dnf groupinstall "Development Tools"

# または個別インストール
sudo dnf install gcc gdb make valgrind
```

### **2. 動作確認**
```bash
# コンパイラの確認
gcc --version
clang --version    # macOS/Linux

# デバッガーの確認
gdb --version

# Makeの確認
make --version
```

### **3. Visual Studio Code 拡張機能**

#### **必須拡張機能**
```bash
# C/C++ Extension Pack
code --install-extension ms-vscode.cpptools-extension-pack

# C/C++ IntelliSense
code --install-extension ms-vscode.cpptools
```

#### **推奨拡張機能**
```bash
# Error Lens（エラーの可視化）
code --install-extension usernamehw.errorlens

# Code Runner（簡単実行）
code --install-extension formulahendry.code-runner

# Bracket Pair Colorizer（括弧の色分け）
code --install-extension coenraads.bracket-pair-colorizer-2
```

---

## 🏗️ プロジェクトの設定

### **4. Makefile の作成**

#### **基本的なMakefile**
```makefile
# Makefile
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -g
TARGET = hello_world
SOURCES = hello_world.c

# デバッグ用フラグ
DEBUG_FLAGS = -DDEBUG -fsanitize=address -fsanitize=undefined

# リリース用フラグ
RELEASE_FLAGS = -O2 -DNDEBUG

# デフォルトターゲット
$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

# デバッグビルド
debug: $(SOURCES)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) -o $(TARGET)_debug $(SOURCES)

# リリースビルド
release: $(SOURCES)
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o $(TARGET)_release $(SOURCES)

# クリーンアップ
clean:
	rm -f $(TARGET) $(TARGET)_debug $(TARGET)_release *.o

# 実行
run: $(TARGET)
	./$(TARGET)

# デバッグ実行
run-debug: debug
	./$(TARGET)_debug

# Valgrindでのメモリチェック（Linux/macOS）
memcheck: debug
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)_debug

# ヘルプ
help:
	@echo "使用可能なターゲット:"
	@echo "  make          - 通常ビルド"
	@echo "  make debug    - デバッグビルド"
	@echo "  make release  - リリースビルド"
	@echo "  make run      - 実行"
	@echo "  make run-debug- デバッグ版実行"
	@echo "  make memcheck - メモリチェック"
	@echo "  make clean    - ファイル削除"

.PHONY: debug release clean run run-debug memcheck help
```

### **5. VS Code の設定ファイル**

#### **.vscode/c_cpp_properties.json**
```json
{
    "configurations": [
        {
            "name": "Win32",
            "includePath": [
                "${workspaceFolder}/**",
                "C:/msys64/mingw64/include"
            ],
            "defines": [
                "_DEBUG",
                "__GNUC__"
            ],
            "compilerPath": "C:/msys64/mingw64/bin/gcc.exe",
            "cStandard": "c99",
            "intelliSenseMode": "gcc-x64"
        },
        {
            "name": "macOS",
            "includePath": [
                "${workspaceFolder}/**",
                "/usr/local/include",
                "/opt/homebrew/include"
            ],
            "defines": [],
            "compilerPath": "/usr/bin/clang",
            "cStandard": "c99",
            "intelliSenseMode": "clang-x64"
        },
        {
            "name": "Linux",
            "includePath": [
                "${workspaceFolder}/**",
                "/usr/include",
                "/usr/local/include"
            ],
            "defines": [],
            "compilerPath": "/usr/bin/gcc",
            "cStandard": "c99",
            "intelliSenseMode": "gcc-x64"
        }
    ],
    "version": 4
}
```

#### **.vscode/tasks.json**
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "C: gcc build",
            "type": "shell",
            "command": "gcc",
            "args": [
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-g",
                "${file}",
                "-o",
                "${fileDirname}/${fileBasenameNoExtension}"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "presentation": {
                "echo": true,
                "reveal": "always",
                "focus": false,
                "panel": "shared"
            }
        },
        {
            "label": "C: make build",
            "type": "shell",
            "command": "make",
            "group": "build",
            "presentation": {
                "echo": true,
                "reveal": "always",
                "focus": false,
                "panel": "shared"
            }
        }
    ]
}
```

#### **.vscode/launch.json**
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "C: Debug",
            "type": "cppdbg",
            "request": "launch",
            "program": "${fileDirname}/${fileBasenameNoExtension}",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "miDebuggerPath": "/usr/bin/gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "C: gcc build"
        }
    ]
}
```

---

## 🧪 動作確認

### **6. サンプルプロジェクトのビルドと実行**

#### **直接コンパイル**
```bash
# 基本的なコンパイル
gcc -std=c99 -Wall -Wextra hello_world.c -o hello_world

# デバッグ情報付きコンパイル
gcc -std=c99 -Wall -Wextra -g hello_world.c -o hello_world_debug

# 実行
./hello_world
```

#### **Makefile を使用**
```bash
# プロジェクトディレクトリに移動
cd c

# 通常ビルド
make

# デバッグビルド
make debug

# 実行
make run

# デバッグ実行
make run-debug

# クリーンアップ
make clean
```

### **7. デバッグの実行**

#### **GDB を使用したデバッグ**
```bash
# デバッグ情報付きでコンパイル
gcc -g hello_world.c -o hello_world_debug

# GDB でデバッグ開始
gdb ./hello_world_debug

# GDB コマンド例
(gdb) break main        # main関数にブレークポイント設定
(gdb) run              # プログラム実行
(gdb) next             # 次の行へ
(gdb) print variable   # 変数の値を表示
(gdb) quit             # GDB終了
```

### **8. メモリリーク検出（Linux/macOS）**

#### **Valgrind を使用**
```bash
# メモリリークチェック
valgrind --leak-check=full --show-leak-kinds=all ./hello_world_debug

# より詳細な情報
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./hello_world_debug
```

#### **AddressSanitizer を使用**
```bash
# AddressSanitizer付きでコンパイル
gcc -fsanitize=address -g hello_world.c -o hello_world_asan

# 実行（メモリエラーがあれば自動検出）
./hello_world_asan
```

---

## 🔍 トラブルシューティング

### **よくある問題と解決方法**

#### **コンパイラが見つからない**
```bash
# パスの確認
echo $PATH

# コンパイラの場所確認
which gcc
which clang

# Windows (MinGW)
where gcc.exe
```

#### **ヘッダーファイルが見つからない**
```bash
# インクルードパスの確認
gcc -v -E -x c - < /dev/null

# 標準ライブラリの場所
find /usr -name "stdio.h" 2>/dev/null
```

#### **リンクエラー**
```bash
# ライブラリパスの確認
ld --verbose | grep SEARCH_DIR

# 数学ライブラリのリンク例
gcc hello_world.c -o hello_world -lm
```

#### **実行時エラー**
```bash
# 実行権限の確認・付与
chmod +x hello_world

# 動的ライブラリの確認
ldd ./hello_world  # Linux
otool -L ./hello_world  # macOS
```

---

## 📊 コード品質管理

### **9. 静的解析ツール**

#### **Cppcheck のインストールと使用**
```bash
# インストール
# Windows
choco install cppcheck

# macOS
brew install cppcheck

# Linux
sudo apt install cppcheck

# 使用方法
cppcheck --enable=all --std=c99 hello_world.c
```

#### **Splint のインストールと使用**
```bash
# インストール（Linux）
sudo apt install splint

# 使用方法
splint hello_world.c
```

### **10. コーディング規約**

#### **.clang-format** 設定ファイル
```yaml
# .clang-format
BasedOnStyle: LLVM
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 80
BreakBeforeBraces: Linux
AllowShortIfStatementsOnASingleLine: false
AllowShortLoopsOnASingleLine: false
AllowShortFunctionsOnASingleLine: None
```

---

## 📚 参考リンク

- [GCC 公式ドキュメント](https://gcc.gnu.org/onlinedocs/)
- [GDB デバッガー ガイド](https://www.gnu.org/software/gdb/documentation/)
- [Valgrind ユーザーマニュアル](https://valgrind.org/docs/manual/)
- [C プログラミング言語（K&R）](https://www.amazon.co.jp/dp/4320026926)

---

## 📝 インストール完了チェックリスト

- [ ] C コンパイラ（GCC/Clang）がインストール済み
- [ ] Make がインストール済み
- [ ] GDB デバッガーがインストール済み
- [ ] VS Code + C 拡張機能がインストール済み
- [ ] Valgrind がインストール済み（Linux/macOS）
- [ ] hello_world.c が正常にコンパイル・実行できる
- [ ] Makefile を使用したビルドができる
- [ ] GDB でのデバッグができる
- [ ] メモリリーク検出ツールが動作する

**✅ すべて完了したら、システムプログラミングやアルゴリズム実装の学習を開始できます！**