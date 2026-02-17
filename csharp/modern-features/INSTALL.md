# modern-features/INSTALL.md

[重要] `modern-features/` のサンプルは .NET SDK を使用して実行します。  
[厳守] 実行前に `dotnet --version` で SDK が利用できることを確認します。理由: まず環境要因を切り分けるため。  

## 実行手順（ModernCSharpApp）
```powershell
cd csharp\modern-features\ModernCSharpApp
dotnet run
```

## 期待される確認ポイント
- ラムダ式のセクションで、キャプチャ方式によって結果が変わること（`expected:` が一致すること）
- modern features のセクションで、nullable/switch式/record の出力が確認できること

