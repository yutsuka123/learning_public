# C# 開発環境インストールガイド

## 📋 概要
C#開発に必要な環境のセットアップとAWS・Google Apps Script連携のための準備手順を説明します。

## ✅ 前提条件
- Windows 10/11 または macOS、Linux
- 管理者権限でのコマンド実行が可能
- インターネット接続

## 🎯 インストール対象
- .NET SDK（最新版）
- Visual Studio Code + C# 拡張機能
- AWS CLI
- Google Cloud CLI（GAS連携用）
- NuGet パッケージ

---

## 🔧 基本環境のセットアップ

### **1. .NET SDK のインストール**

#### **現在の状況確認**
```powershell
dotnet --version
```
✅ **既にインストール済み**: .NET 9.0.102

#### **最新版への更新（必要に応じて）**
```powershell
# Chocolateyを使用
choco upgrade dotnet

# 手動インストール
# https://dotnet.microsoft.com/download からダウンロード
```

### **2. Visual Studio Code 拡張機能**

#### **必須拡張機能**
```bash
# C# Dev Kit（Microsoft公式）
code --install-extension ms-dotnettools.csdevkit

# IntelliCode for C# Dev Kit
code --install-extension ms-dotnettools.vscodeintellicode-csharp
```

#### **推奨拡張機能**
```bash
# NuGet Package Manager
code --install-extension jmrog.vscode-nuget-package-manager

# Error Lens（エラーの可視化）
code --install-extension usernamehw.errorlens

# Prettier（コードフォーマッター）
code --install-extension esbenp.prettier-vscode
```

---

## ☁️ クラウド連携環境

### **3. AWS CLI のインストール**

#### **インストール**
```powershell
# Chocolateyを使用
choco install awscli

# 手動インストール（Windows）
# https://aws.amazon.com/cli/ からMSIインストーラーをダウンロード
```

#### **設定**
```powershell
# AWS認証情報の設定
aws configure

# 入力項目:
# AWS Access Key ID: [あなたのアクセスキー]
# AWS Secret Access Key: [あなたのシークレットキー]
# Default region name: ap-northeast-1
# Default output format: json
```

#### **接続確認**
```powershell
aws sts get-caller-identity
```

### **4. Google Cloud CLI のインストール（GAS連携用）**

#### **インストール**
```powershell
# Chocolateyを使用
choco install gcloudsdk

# 手動インストール
# https://cloud.google.com/sdk/docs/install からダウンロード
```

#### **初期設定**
```powershell
# 初期化
gcloud init

# 認証
gcloud auth login
gcloud auth application-default login
```

---

## 📦 NuGet パッケージのインストール

### **5. プロジェクトの作成と依存関係**

#### **新しいコンソールプロジェクトの作成**
```powershell
# プロジェクトディレクトリに移動
cd csharp

# 新しいプロジェクトを作成
dotnet new console -n HelloWorldApp
cd HelloWorldApp
```

#### **AWS SDK のインストール**
```powershell
# AWS SDK Core
dotnet add package AWSSDK.Core

# S3サービス
dotnet add package AWSSDK.S3

# Lambda サービス
dotnet add package AWSSDK.Lambda

# DynamoDB サービス
dotnet add package AWSSDK.DynamoDBv2

# Systems Manager（設定管理用）
dotnet add package AWSSDK.SimpleSystemsManagement
```

#### **HTTP クライアント（GAS連携用）**
```powershell
# HTTP クライアント
dotnet add package Microsoft.Extensions.Http

# JSON処理
dotnet add package Newtonsoft.Json

# 設定管理
dotnet add package Microsoft.Extensions.Configuration
dotnet add package Microsoft.Extensions.Configuration.Json
```

#### **ログ出力**
```powershell
# Serilog（構造化ログ）
dotnet add package Serilog
dotnet add package Serilog.Sinks.Console
dotnet add package Serilog.Sinks.File

# または NLog
dotnet add package NLog
dotnet add package NLog.Extensions.Logging
```

---

## 🧪 動作確認

### **6. サンプルプロジェクトの実行**

#### **基本的なHello Worldの実行**
```powershell
# 既存のサンプルを実行
cd csharp
dotnet run HelloWorld.cs
```

#### **AWS接続テスト用のサンプル**
```csharp
// AWS接続テストサンプル（aws_test.cs）
using Amazon.S3;
using Amazon.S3.Model;

class AwsTest 
{
    static async Task Main(string[] args)
    {
        try 
        {
            var s3Client = new AmazonS3Client();
            var response = await s3Client.ListBucketsAsync();
            Console.WriteLine($"AWS接続成功！バケット数: {response.Buckets.Count}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"AWS接続エラー: {ex.Message}");
        }
    }
}
```

#### **GAS連携テスト用のサンプル**
```csharp
// GAS連携テストサンプル（gas_test.cs）
using System.Text;
using Newtonsoft.Json;

class GasTest 
{
    private static readonly HttpClient client = new HttpClient();
    
    static async Task Main(string[] args)
    {
        try 
        {
            var data = new { message = "Hello from C#!" };
            var json = JsonConvert.SerializeObject(data);
            var content = new StringContent(json, Encoding.UTF8, "application/json");
            
            // GASのWebアプリURLを設定
            var gasUrl = "https://script.google.com/macros/s/YOUR_SCRIPT_ID/exec";
            var response = await client.PostAsync(gasUrl, content);
            var result = await response.Content.ReadAsStringAsync();
            
            Console.WriteLine($"GAS連携成功！レスポンス: {result}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"GAS連携エラー: {ex.Message}");
        }
    }
}
```

---

## 🔍 トラブルシューティング

### **よくある問題と解決方法**

#### **.NET SDKが見つからない**
```powershell
# パスの確認
echo $env:PATH

# .NET SDKの場所確認
dotnet --info
```

#### **AWS認証エラー**
```powershell
# 認証情報の確認
aws configure list

# 認証情報ファイルの場所
# Windows: %USERPROFILE%\.aws\credentials
# macOS/Linux: ~/.aws/credentials
```

#### **NuGetパッケージの復元エラー**
```powershell
# キャッシュをクリア
dotnet nuget locals all --clear

# パッケージの復元
dotnet restore
```

#### **VS Code でIntelliSenseが動作しない**
1. C# Dev Kit拡張機能がインストールされているか確認
2. .NET SDKが正しくインストールされているか確認
3. VS Codeを再起動
4. コマンドパレット（Ctrl+Shift+P）で「.NET: Restart Language Server」を実行

---

## 📚 参考リンク

- [.NET 公式ドキュメント](https://docs.microsoft.com/ja-jp/dotnet/)
- [AWS SDK for .NET](https://aws.amazon.com/sdk-for-net/)
- [Google Cloud .NET Client Libraries](https://cloud.google.com/dotnet/docs)
- [C# プログラミング ガイド](https://docs.microsoft.com/ja-jp/dotnet/csharp/)

---

## 📝 インストール完了チェックリスト

- [ ] .NET SDK 9.0以降がインストール済み
- [ ] VS Code + C# Dev Kit拡張機能がインストール済み
- [ ] AWS CLI がインストール・設定済み
- [ ] Google Cloud CLI がインストール・設定済み（GAS連携する場合）
- [ ] 基本的なNuGetパッケージがインストール済み
- [ ] HelloWorld.cs が正常に実行できる
- [ ] AWS/GAS連携のテストサンプルが動作する（該当する場合）

**✅ すべて完了したら、実際のプロジェクト開発を開始できます！**