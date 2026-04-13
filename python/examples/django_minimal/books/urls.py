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
]
