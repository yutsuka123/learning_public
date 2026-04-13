# Python 実践例（PyTorch / Django / FastAPI）

[重要] 各サンプルは**別ファイル・別フォルダ**に分離しています。依存関係が異なるため、**仮想環境を分ける**ことを推奨します。  
[厳守] 実行手順は各 README またはファイル先頭コメントに従ってください。

## 目次

| 内容 | 場所 | 概要 |
|------|------|------|
| PyTorch | `pytorch_practice.py` | テンソル・自動微分・小さな訓練ループ（コメント多め） |
| FastAPI | `fastapi_practice.py` | 単一ファイルで起動可能な REST API 例 |
| Django | `django_minimal/` | 最小プロジェクト構成（モデル・管理画面・JSON 応答） |

レガシーな統合サンプル（1 ファイルに NN まで含む長めの例）: 上位フォルダの `pytorch_sample.py`。

---

## 「基礎〜実務で経験ありと言える」目安（自己評価用ラダー）

以下は**面接や職務経歴で嘘をつかないための目安**です。チームや領域で重みは変わります。

### レベル 1（基礎を説明できる）

- **言語**: 仮想環境、`import`、例外、`with`、リスト/辞書内包、関数と型ヒントの意味を説明できる。  
- **PyTorch**: テンソルの `shape` / `dtype` / `device` を説明し、`backward()` が「何のためか」を言語化できる。  
- **Django**: `request` → `view` → `response` の流れと、`models.Model` が DB 行に対応することを説明できる。  
- **FastAPI**: パス関数が「HTTP と Python 関数の橋」と説明でき、`Pydantic` が入力検証に使われることを説明できる。

### レベル 2（チュートリアル超え・小改修できる）

- **PyTorch**: `Dataset` / `DataLoader` で自前データを読み、`train` / `eval` と `torch.no_grad()` の違いをコードで分けられる。  
- **Django**: `makemigrations` / `migrate` の因果、管理画面の登録、`urls.py` の include を自分で直せる。  
- **FastAPI**: 依存注入（`Depends`）で認可や DB セッションを差し替え可能な形にできる。

### レベル 3（実務で「任せてよい」ゾーンの入り口）

- **PyTorch**: 過学習対策（正則化・早期停止）、再現性（シード・決定性の限界）、メトリクス設計、チェックポイント保存を**設計として説明**できる。  
- **Django**: 本番想定の `DEBUG=False`、秘密情報の分離、ミドルウェア、N+1 と `select_related` の話をコードレビューで指摘できる。  
- **FastAPI**: OpenAPI を前提にフロントと契約を切る、例外ハンドラ、ステータスコードの統一方針を決められる。

### レベル 4（設計・運用まで含めて語れる）

- ログ・メトリクス・トレース、デプロイ、マイグレーション戦略、マルチプロセス/ワーカー、GPU ジョブのキューイング等、**本番運用の文脈**で語れる。

この `examples/` のコードは主に **レベル 1〜2 に到達するための足場**です。レベル 3 以上は実案件またはオープンソース貢献で補強するのが現実的です。

---

## クイック実行（共通）

```powershell
cd E:\develop\src\learning_public\python
python -m venv .venv-examples
.\.venv-examples\Scripts\activate
```

以降は各サンプルの手順に従って追加インストールしてください。

---

## 変更履歴

| 日付 | 内容 |
|------|------|
| 2026-04-13 | `examples/` 新設。ラダー説明と PyTorch / FastAPI / Django への案内を追加。 |
