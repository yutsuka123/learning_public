# cpp_m/INSTALL.md

[重要] このフォルダは「C++11/14/17/20/23 の差分」と「modern(最新の総合)」を学ぶための、**単体コンパイル前提**のサンプル集です。  
[厳守] `cpp_m/` 配下は「1ファイル = 1実行ファイル」を基本とします。理由: 標準バージョンごとにコンパイルオプションが変わり、複数 `main()` が衝突しやすいため。  
[禁止] 「とりあえず動かすために標準を下げる」運用。理由: 学習したい標準の機能が無効化され、原因が追いにくくなるため。  

## 前提環境
- **OS**: Windows 10/11
- **コンパイラ**（いずれか）
  - **MSVC**（Visual Studio または Build Tools）
  - **clang++**
  - **g++**（MinGW-w64 など）

## 目標
- 各ファイルを該当標準でビルドし、実行結果とコードを突き合わせて理解する。

## MSVC (cl.exe) でのビルド例
[推奨] PowerShell で、Developer Command Prompt（または vcvarsall.bat 済みの環境）を使います。理由: `cl` へのパスと標準ライブラリ設定が確実になるため。

```powershell
cd c:\mydata\project\myproject\learning_public\cpp_m

# C++11
cl /std:c++14 /EHsc /W4 /nologo /utf-8 /Zc:__cplusplus .\cpp11.cpp

# C++14
cl /std:c++14 /EHsc /W4 /nologo /utf-8 /Zc:__cplusplus .\cpp14.cpp

# C++17
cl /std:c++17 /EHsc /W4 /nologo /utf-8 /Zc:__cplusplus .\cpp17.cpp

# C++20
cl /std:c++20 /EHsc /W4 /nologo /utf-8 /Zc:__cplusplus .\cpp20.cpp

# C++23 (Visual Studio により /std:c++latest の場合あり)
cl /std:c++latest /EHsc /W4 /nologo /utf-8 /Zc:__cplusplus .\cpp23.cpp

# modern (最新の総合)
cl /std:c++latest /EHsc /W4 /nologo /utf-8 /Zc:__cplusplus .\modern.cpp
```

[注意] MSVC は `__cplusplus` が古い値になることがあります（`/Zc:__cplusplus` に依存）。各サンプルは `__cplusplus` と `_MSVC_LANG` の両方を表示します。

[注意] 日本語コメントを含むため、MSVC では `/utf-8` を付けることを推奨します。理由: 既定コードページ(例: 932)による警告(C4819)や文字化けを避けるため。

[推奨] 実行時にコンソール出力が文字化けする場合は、UTF-8 対応のターミナル（Windows Terminal 等）を使うか、必要に応じて `chcp 65001` を試してください。

## clang++ / g++ でのビルド例
```powershell
cd c:\mydata\project\myproject\learning_public\cpp_m

clang++ -std=c++11 -Wall -Wextra -pedantic .\cpp11.cpp -o cpp11.exe
clang++ -std=c++14 -Wall -Wextra -pedantic .\cpp14.cpp -o cpp14.exe
clang++ -std=c++17 -Wall -Wextra -pedantic .\cpp17.cpp -o cpp17.exe
clang++ -std=c++20 -Wall -Wextra -pedantic .\cpp20.cpp -o cpp20.exe
clang++ -std=c++23 -Wall -Wextra -pedantic .\cpp23.cpp -o cpp23.exe
clang++ -std=c++23 -Wall -Wextra -pedantic .\modern.cpp -o modern.exe
```

## 実行例
```powershell
.\cpp17.exe
.\modern.exe --numbers 1 2 3 4 5
```

## うまくビルドできない時
- **(1)** まず `main.cpp` をビルドして「検出された標準」表示を確認
- **(2)** そのファイルの先頭コメント（[厳守]）に書いた標準オプションで再ビルド
- **(3)** それでもだめなら、エラーメッセージ全文を `問題点記録書.md` に貼って原因と対策を残す（再発防止）

