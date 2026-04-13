# Django 最小実践例

[重要] 開発用の最小構成です。`SECRET_KEY` は学習用の固定値であり、**本番では環境変数から注入**してください。  
[厳守] 手順どおりに仮想環境を切ってから実行してください。

## 目的

- `Model` → `migrate` → 管理画面 → `JsonResponse` API までの**一連の導線**を短いコードで把握する。
- `examples/README.md` の自己評価ラダー（レベル 1〜2）と併読する。

## セットアップと実行（Windows PowerShell 例）

```powershell
cd E:\develop\src\learning_public\python\examples\django_minimal
python -m venv .venv
.\.venv\Scripts\activate
pip install -r requirements.txt
python manage.py migrate
python manage.py createsuperuser
python manage.py runserver
```

- 管理画面: http://127.0.0.1:8000/admin/  
- JSON 一覧: http://127.0.0.1:8000/api/books/

POST 例（PowerShell。学習用ビューは CSRF を免除しているため curl から試せるが、**本番では設計し直すこと**）:

```powershell
curl -X POST http://127.0.0.1:8000/api/books/ -H "Content-Type: application/json" -d "{\"title\":\"Django入門\",\"author\":\"山田\"}"
```

## 変更履歴

| 日付 | 内容 |
|------|------|
| 2026-04-13 | 初版。books アプリと API エンドポイントを追加。 |
