"""
管理サイトの登録モジュール。

目的:
    `createsuperuser` 後にブラウザからレコードを投入できるようにする。
"""

from django.contrib import admin

from books.models import Book


@admin.register(Book)
class BookAdmin(admin.ModelAdmin):
    """
    Book モデルの管理画面カスタマイズ（最小）。

    属性:
        list_display: 一覧に出すカラム。
        search_fields: 検索対象。
    """

    list_display = ("id", "title", "author", "published_at")
    search_fields = ("title", "author")
