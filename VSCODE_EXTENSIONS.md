# VS Code 拡張機能インストールガイド

## 📋 概要
各プログラミング言語の開発に必要なVisual Studio Code拡張機能の一覧とインストール手順を説明します。

## 🎯 対象言語
- C#
- C++
- C言語
- Python
- Java
- Kotlin
- TypeScript
- 共通開発ツール

---

## 🔧 拡張機能の一括インストール

### **すべての言語対応（推奨セット）**
```bash
# 基本的な言語サポート
code --install-extension ms-dotnettools.csdevkit                    # C# Dev Kit
code --install-extension ms-vscode.cpptools-extension-pack          # C/C++ Extension Pack
code --install-extension ms-python.python                          # Python
code --install-extension vscjava.vscode-java-pack                  # Extension Pack for Java
code --install-extension fwcd.kotlin                               # Kotlin
# TypeScript は VS Code に標準搭載

# 共通開発ツール
code --install-extension usernamehw.errorlens                      # Error Lens
code --install-extension esbenp.prettier-vscode                    # Prettier
code --install-extension ms-vscode.vscode-json                     # JSON
code --install-extension redhat.vscode-yaml                        # YAML
code --install-extension ms-vscode.hexeditor                       # Hex Editor

# Git関連
code --install-extension eamodio.gitlens                           # GitLens
code --install-extension mhutchie.git-graph                        # Git Graph

# その他便利ツール
code --install-extension formulahendry.code-runner                 # Code Runner
code --install-extension christian-kohler.path-intellisense       # Path Intellisense
code --install-extension ms-vscode.live-server                     # Live Server
```

---

## 📝 言語別拡張機能詳細

### **C# 開発**

#### **必須拡張機能**
```bash
# C# Dev Kit（Microsoft公式、最新推奨）
code --install-extension ms-dotnettools.csdevkit

# IntelliCode for C# Dev Kit
code --install-extension ms-dotnettools.vscodeintellicode-csharp
```

#### **推奨拡張機能**
```bash
# NuGet Package Manager
code --install-extension jmrog.vscode-nuget-package-manager

# .NET Core Test Explorer
code --install-extension formulahendry.dotnet-test-explorer

# C# XML Documentation Comments
code --install-extension k--kato.docomment
```

---

### **C++ 開発**

#### **必須拡張機能**
```bash
# C/C++ Extension Pack（包括的なC++サポート）
code --install-extension ms-vscode.cpptools-extension-pack

# 含まれる拡張機能:
# - C/C++ (ms-vscode.cpptools)
# - C/C++ Themes (ms-vscode.cpptools-themes)
# - CMake Tools (ms-vscode.cmake-tools)
# - CMake (twxs.cmake)
```

#### **推奨拡張機能**
```bash
# Better C++ Syntax
code --install-extension jeff-hykin.better-cpp-syntax

# C++ Intellisense
code --install-extension austin.code-gnu-global

# Clang-Format
code --install-extension xaver.clang-format
```

---

### **C言語 開発**

#### **必須拡張機能**
```bash
# C/C++ Extension Pack（C++と共通）
code --install-extension ms-vscode.cpptools-extension-pack
```

#### **推奨拡張機能**
```bash
# C/C++ Advanced Lint
code --install-extension jbenden.c-cpp-flylint

# GNU Global（コード検索）
code --install-extension austin.code-gnu-global
```

---

### **Python 開発**

#### **必須拡張機能**
```bash
# Python Extension Pack（Microsoft公式）
code --install-extension ms-python.python

# Pylance（高速言語サーバー）
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

# Black Formatter
code --install-extension ms-python.black-formatter

# isort（import整理）
code --install-extension ms-python.isort

# Python Indent
code --install-extension kevinrose.vsc-python-indent
```

---

### **Java 開発**

#### **必須拡張機能**
```bash
# Extension Pack for Java（Microsoft公式）
code --install-extension vscjava.vscode-java-pack

# 含まれる拡張機能:
# - Language Support for Java(TM) by Red Hat
# - Debugger for Java
# - Test Runner for Java
# - Maven for Java
# - Project Manager for Java
# - Visual Studio IntelliCode
```

#### **推奨拡張機能**
```bash
# Spring Boot Extension Pack
code --install-extension vmware.vscode-spring-boot

# Gradle for Java
code --install-extension vscjava.vscode-gradle

# SonarLint（コード品質）
code --install-extension sonarsource.sonarlint-vscode

# Checkstyle for Java
code --install-extension shengchen.vscode-checkstyle
```

---

### **Kotlin 開発**

#### **必須拡張機能**
```bash
# Kotlin Language Support
code --install-extension fwcd.kotlin
```

#### **推奨拡張機能**
```bash
# Gradle for Java（Kotlinプロジェクトでも使用）
code --install-extension vscjava.vscode-gradle

# Code Runner
code --install-extension formulahendry.code-runner
```

---

### **TypeScript 開発**

#### **必須拡張機能**
```bash
# TypeScript は VS Code に標準搭載されているため追加不要

# ESLint（リンター）
code --install-extension dbaeumer.vscode-eslint

# Prettier（フォーマッター）
code --install-extension esbenp.prettier-vscode
```

#### **推奨拡張機能**
```bash
# Auto Rename Tag
code --install-extension formulahendry.auto-rename-tag

# Bracket Pair Colorizer
code --install-extension coenraads.bracket-pair-colorizer-2

# Path Intellisense
code --install-extension christian-kohler.path-intellisense

# TypeScript Importer
code --install-extension pmneo.tsimporter

# REST Client（API テスト用）
code --install-extension humao.rest-client
```

---

## 🛠️ 共通開発ツール

### **必須共通ツール**
```bash
# Error Lens（エラーの可視化）
code --install-extension usernamehw.errorlens

# Prettier（コードフォーマッター）
code --install-extension esbenp.prettier-vscode

# GitLens（Git機能強化）
code --install-extension eamodio.gitlens

# Code Runner（簡単実行）
code --install-extension formulahendry.code-runner
```

### **推奨共通ツール**
```bash
# Bracket Pair Colorizer（括弧の色分け）
code --install-extension coenraads.bracket-pair-colorizer-2

# Auto Rename Tag（HTMLタグの自動リネーム）
code --install-extension formulahendry.auto-rename-tag

# Path Intellisense（パス補完）
code --install-extension christian-kohler.path-intellisense

# Indent Rainbow（インデント可視化）
code --install-extension oderwat.indent-rainbow

# TODO Highlight（TODOコメント強調）
code --install-extension wayou.vscode-todo-highlight

# Better Comments（コメント強調）
code --install-extension aaron-bond.better-comments
```

### **ファイル形式サポート**
```bash
# JSON
code --install-extension ms-vscode.vscode-json

# YAML
code --install-extension redhat.vscode-yaml

# XML
code --install-extension redhat.vscode-xml

# CSV
code --install-extension mechatroner.rainbow-csv

# Markdown
code --install-extension yzhang.markdown-all-in-one

# Docker
code --install-extension ms-azuretools.vscode-docker
```

### **テーマとアイコン**
```bash
# Material Theme（人気のテーマ）
code --install-extension equinusocio.vsc-material-theme

# Material Icon Theme（アイコンテーマ）
code --install-extension pkief.material-icon-theme

# One Dark Pro（人気のダークテーマ）
code --install-extension zhuangtongfa.material-theme
```

---

## ⚙️ 拡張機能の設定

### **VS Code 設定ファイルの例**

#### **settings.json（グローバル設定）**
```json
{
    "editor.fontSize": 14,
    "editor.fontFamily": "'Fira Code', 'Cascadia Code', Consolas, monospace",
    "editor.fontLigatures": true,
    "editor.tabSize": 4,
    "editor.insertSpaces": true,
    "editor.formatOnSave": true,
    "editor.formatOnPaste": true,
    "editor.minimap.enabled": true,
    "editor.wordWrap": "on",
    "editor.rulers": [80, 120],
    "editor.bracketPairColorization.enabled": true,
    "editor.guides.bracketPairs": "active",
    
    "files.autoSave": "afterDelay",
    "files.autoSaveDelay": 1000,
    "files.trimTrailingWhitespace": true,
    "files.insertFinalNewline": true,
    
    "workbench.colorTheme": "Material Theme Darker High Contrast",
    "workbench.iconTheme": "material-icon-theme",
    
    "terminal.integrated.fontSize": 13,
    "terminal.integrated.fontFamily": "'Cascadia Code', Consolas, monospace",
    
    "git.enableSmartCommit": true,
    "git.confirmSync": false,
    "git.autofetch": true,
    
    "errorLens.enabledDiagnosticLevels": ["error", "warning"],
    "errorLens.fontSize": "12px",
    
    "prettier.requireConfig": true,
    "prettier.useEditorConfig": false,
    
    "[json]": {
        "editor.defaultFormatter": "esbenp.prettier-vscode"
    },
    "[javascript]": {
        "editor.defaultFormatter": "esbenp.prettier-vscode"
    },
    "[typescript]": {
        "editor.defaultFormatter": "esbenp.prettier-vscode"
    }
}
```

---

## 🔍 トラブルシューティング

### **拡張機能が動作しない場合**

#### **1. 拡張機能の確認**
```bash
# インストール済み拡張機能の一覧
code --list-extensions

# 特定の拡張機能の確認
code --list-extensions | grep python
```

#### **2. 拡張機能の再インストール**
```bash
# 拡張機能のアンインストール
code --uninstall-extension ms-python.python

# 再インストール
code --install-extension ms-python.python
```

#### **3. VS Code のリセット**
1. Ctrl+Shift+P でコマンドパレット
2. "Developer: Reload Window" を実行
3. "Developer: Restart Extension Host" を実行

#### **4. 設定のリセット**
```bash
# 設定ファイルの場所
# Windows: %APPDATA%\Code\User\settings.json
# macOS: ~/Library/Application Support/Code/User/settings.json
# Linux: ~/.config/Code/User/settings.json

# 設定をデフォルトに戻す（バックアップを取ってから）
mv settings.json settings.json.backup
```

---

## 📚 参考リンク

- [VS Code 拡張機能マーケットプレース](https://marketplace.visualstudio.com/vscode)
- [VS Code 公式ドキュメント](https://code.visualstudio.com/docs)
- [拡張機能開発ガイド](https://code.visualstudio.com/api)

---

## 📝 インストール完了チェックリスト

### **基本チェック**
- [ ] 各言語の必須拡張機能がインストール済み
- [ ] Error Lens が動作している
- [ ] Prettier によるフォーマットが動作している
- [ ] GitLens が Git情報を表示している
- [ ] Code Runner でコードが実行できる

### **言語別チェック**
- [ ] **C#**: IntelliSense と構文ハイライトが動作
- [ ] **C++**: コンパイルエラーが表示される
- [ ] **Python**: インタープリターが選択されている
- [ ] **Java**: プロジェクトが認識されている
- [ ] **Kotlin**: 構文ハイライトが動作している
- [ ] **TypeScript**: 型チェックが動作している

**✅ すべて完了したら、効率的なマルチ言語開発環境が完成です！**