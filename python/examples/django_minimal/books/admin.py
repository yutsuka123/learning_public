"""
管理サイトの登録モジュール。

目的:
    `createsuperuser` 後にブラウザからレコードを投入できるようにする。
"""

from django.contrib import admin

from books.models import Author, Book


@admin.register(Author)
class AuthorAdmin(admin.ModelAdmin):
    """
    Author モデルの管理画面カスタマイズ（最小）。

    属性:
        list_display: 一覧に出すカラム。
        search_fields: 検索対象。
    """

    list_display = ("id", "name")
    search_fields = ("name",)


@admin.register(Book)
class BookAdmin(admin.ModelAdmin):
    """
    Book モデルの管理画面カスタマイズ（最小）。

    属性:
        list_display: 一覧に出すカラム。
        search_fields: 検索対象。
        list_select_related: 管理画面の一覧表示自体もN+1になりうるため、
            `author`を`select_related`してJOINで取得する（books/views.pyの
            n1DemoBooks/selectRelatedDemoBooksと同じ考え方を管理画面にも適用した例）。
    """

    list_display = ("id", "title", "author", "published_at")
    search_fields = ("title", "author__name")
    list_select_related = ("author",)
