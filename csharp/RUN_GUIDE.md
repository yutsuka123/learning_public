# C#コード実行ガイド

## 📋 概要
このガイドでは、プロジェクト内のC#コード（HelloWorld.cs）を実行するための手順を説明します。

## ✅ 前提条件
- .NET SDK（バージョン6.0以降）がインストールされていること
- コマンドラインからdotnetコマンドが利用可能であること

---

## 🔧 実行方法

### **1. プロジェクトの作成と実行**

```powershell
# csharpsフォルダに移動
cd csharp

# 新しいコンソールアプリケーションプロジェクトを作成
dotnet new console -n HelloWorldApp

# プロジェクトディレクトリに移動
cd HelloWorldApp

# 既存のProgram.csを削除（自動生成されたもの）
del Program.cs    # Windowsの場合
# rm Program.cs   # macOS/Linuxの場合

# 既存のHelloWorld.csをコピー
copy ..\HelloWorld.cs .    # Windowsの場合
# cp ../HelloWorld.cs .    # macOS/Linuxの場合

# プロジェクトを実行
dotnet run
```

### **2. プロジェクト構造の説明**

実行後のプロジェクト構造は以下のようになります：

```
csharp/
├── HelloWorld.cs        # 元のソースファイル
├── HelloWorldApp/       # 作成されたプロジェクトフォルダ
│   ├── HelloWorld.cs    # コピーされたソースファイル
│   ├── HelloWorldApp.csproj  # プロジェクトファイル
│   ├── obj/             # ビルド中間ファイル
│   └── bin/             # ビルド出力ファイル
└── INSTALL.md           # インストールガイド
```

---

## 🔄 代替方法

### **3. .NET SDKを使った単一ファイル実行（.NET 5以降）**

.NET 5以降では、プロジェクトを作成せずに単一ファイルを直接実行することも可能です：

```powershell
# csharpsフォルダに移動
cd csharp

# 単一ファイル実行
dotnet run --file HelloWorld.cs
```

または：

```powershell
dotnet HelloWorld.cs
```

### **4. dotnet-scriptを使用する方法**

```powershell
# dotnet-scriptをグローバルツールとしてインストール
dotnet tool install -g dotnet-script

# C#スクリプトとして実行
dotnet script HelloWorld.cs
```

---

## 🧪 トラブルシューティング

### **よくある問題と解決方法**

#### **プロジェクトが見つからないエラー**
```
実行するプロジェクトが見つかりませんでした。
```

**解決策**：csharpフォルダには.csprojファイルがないため、プロジェクトとして認識されません。上記の「プロジェクトの作成と実行」手順に従ってプロジェクトを作成してください。

#### **ファイルパスの問題**
```
C:\mydata\project\myproject\learning_public\csharp\HelloWorld.cs(1,1): error MSB4025: プロジェクト ファイルを読み込めませんでした。
```

**解決策**：HelloWorld.csはソースファイルであり、プロジェクトファイルではありません。`--project`オプションではなく、プロジェクトを作成する手順に従ってください。

#### **.NET SDKのバージョン問題**
```
指定されたフレームワーク 'Microsoft.NETCore.App', version '3.1.0' が見つかりませんでした。
```

**解決策**：.NET SDKの最新バージョンをインストールしてください。

```powershell
# インストールされているバージョンの確認
dotnet --list-sdks

# 最新版の.NETをダウンロード・インストール
# https://dotnet.microsoft.com/download
```

---

## 📚 参考リンク

- [.NET CLI の概要](https://docs.microsoft.com/ja-jp/dotnet/core/tools/)
- [dotnet run コマンド](https://docs.microsoft.com/ja-jp/dotnet/core/tools/dotnet-run)
- [C# コンソールアプリの作成](https://docs.microsoft.com/ja-jp/dotnet/core/tutorials/with-visual-studio-code)
- [Visual Studio Codeでの.NET開発](https://code.visualstudio.com/docs/languages/dotnet)

---

## 📝 実行確認チェックリスト

- [ ] .NET SDKがインストール済み（`dotnet --version`で確認）
- [ ] プロジェクトの作成が完了
- [ ] HelloWorld.csがプロジェクトにコピーされている
- [ ] `dotnet run`コマンドでエラーなく実行できる
- [ ] 「Hello World!」と「C#プログラミング学習を開始します。」のメッセージが表示される
- [ ] Personクラスのインスタンス作成と自己紹介が正常に表示される
- [ ] 年齢の増加処理が正常に実行される

**✅ すべての項目が確認できたら、C#コードの実行成功です！**

---

## 🆕 追加サンプル（modern-features）

[重要] `csharp/modern-features/ModernCSharpApp/` は、プロジェクトとして同梱しているため、そのまま実行できます。

```powershell
cd csharp\modern-features\ModernCSharpApp
dotnet run
```

