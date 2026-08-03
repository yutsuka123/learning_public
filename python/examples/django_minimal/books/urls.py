"""
books アプリの URL パターン。

目的:
    API エンドポイントを名前付きで定義し、プロジェクト urls から include する。
"""

from django.urls import path

from books import views

urlpatterns = [
    path("api/books/", views.bookCollection, name="book-collection"),
    path("api/books/<int:book_id>/", views.bookDetail, name="book-detail"),
    # N+1問題の実演用（examples/README.md のラダー レベル3対応）。詳細は books/views.py 参照。
    path("api/books/n1-demo/", views.n1DemoBooks, name="book-n1-demo"),
    path("api/books/select-related-demo/", views.selectRelatedDemoBooks, name="book-select-related-demo"),
]
