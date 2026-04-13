"""
Django AppConfig 定義。

目的:
    `INSTALLED_APPS` に `"books.apps.BooksConfig"` と書けるようにする。
"""

from django.apps import AppConfig


class BooksConfig(AppConfig):
    """
    books アプリのメタデータ。

    属性:
        default_auto_field: 主キーの既定型。
        name: アプリの Python パス名。
    """

    default_auto_field = "django.db.models.BigAutoField"
    name = "books"
    verbose_name = "書籍サンプル"
