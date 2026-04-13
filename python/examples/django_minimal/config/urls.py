"""
プロジェクト直下の URL ルーティング。

目的:
    管理画面と各アプリの urlpatterns を束ねる。
"""

from django.contrib import admin
from django.urls import include, path

urlpatterns = [
    path("admin/", admin.site.urls),
    path("", include("books.urls")),
]
