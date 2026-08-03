# Django 最小実践例

[重要] 開発用の最小構成です。`SECRET_KEY` は学習用の固定値であり、**本番では環境変数から注入**してください。  
[厳守] 手順どおりに仮想環境を切ってから実行してください。

## 目的

- `Model` → `migrate` → 管理画面 → `JsonResponse` API までの**一連の導線**を短いコードで把握する。
- `examples/README.md` の自己評価ラダー（レベル 1〜3）と併読する。
  - レベル1〜2: `Book`/`Author` モデル、`migrate`、管理画面、`urls.py` の `include`。
  - レベル3: `books/views.py` の `n1DemoBooks`（N+1を意図的に発生）と
    `selectRelatedDemoBooks`（`select_related`で解決）を見比べ、クエリ数の違いを体感する。

## モデル構成

- `Author`（著者）1 : N `Book`（書籍）。`Book.author` は `ForeignKey(Author)`。
- サンプルデータ投入: `python manage.py seed_books`（著者3名・書籍6冊を投入）。

## セットアップと実行（Windows PowerShell 例）

```powershell
cd E:\develop\src\learning_public\python\examples\django_minimal
python -m venv .venv
.\.venv\Scripts\activate
pip install -r requirements.txt
python manage.py migrate
python manage.py seed_books
python manage.py createsuperuser
python manage.py runserver
```

- 管理画面: http://127.0.0.1:8000/admin/
- JSON 一覧: http://127.0.0.1:8000/api/books/
- N+1デモ（悪い例）: http://127.0.0.1:8000/api/books/n1-demo/ → `queryCount` が書籍数+1になる
- select_relatedデモ（良い例）: http://127.0.0.1:8000/api/books/select-related-demo/ → `queryCount` は常に1

POST 例（PowerShell。学習用ビューは CSRF を免除しているため curl から試せるが、**本番では設計し直すこと**）:

```powershell
curl -X POST http://127.0.0.1:8000/api/books/ -H "Content-Type: application/json" -d "{\"title\":\"Django入門\",\"author\":\"山田\"}"
```

## 変更履歴

| 日付 | 内容 |
|------|------|
| 2026-08-03 | `Author`モデル追加・`Book.author`をForeignKey化。N+1問題の実演(`n1DemoBooks`)と`select_related`解決版(`selectRelatedDemoBooks`)を追加。`seed_books`管理コマンド追加。実際にクエリ数7→1になることを確認済み（書籍6件時）。 |
| 2026-04-13 | 初版。books アプリと API エンドポイントを追加。 |
