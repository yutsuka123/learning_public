"""
サンプルデータ投入コマンド。

目的:
    N+1 問題の実演（books/views.py の n1DemoBooks / selectRelatedDemoBooks）には
    複数件の Book/Author が必要なため、`python manage.py seed_books` で
    再現可能なテストデータを用意する。

使い方:
    python manage.py seed_books
"""

from __future__ import annotations

from typing import List, Tuple

from django.core.management.base import BaseCommand

from books.models import Author, Book

# (著者名, [書名, ...]) のサンプルデータ。
# 著者ごとに複数冊持たせているのは、N+1のクエリ数が「書籍数」に比例することを
# はっきり見せるため（著者数だけだと差が分かりにくい）。
SAMPLE_DATA: List[Tuple[str, List[str]]] = [
    ("夏目漱石", ["吾輩は猫である", "坊っちゃん", "こころ"]),
    ("芥川龍之介", ["羅生門", "蜘蛛の糸"]),
    ("宮沢賢治", ["銀河鉄道の夜"]),
]


class Command(BaseCommand):
    """
    `seed_books` コマンド本体。
    """

    help = "N+1デモ用に著者と書籍のサンプルデータを投入します（既存データは重複作成しません）。"

    def handle(self, *args: object, **options: object) -> None:
        """
        サンプルデータを投入する。

        引数:
            args: 未使用（BaseCommandの規約上受け取る）。
            options: 未使用（同上）。
        """

        createdBooks = 0
        for authorName, titles in SAMPLE_DATA:
            author, _ = Author.objects.get_or_create(name=authorName)
            for title in titles:
                _, created = Book.objects.get_or_create(title=title, author=author)
                if created:
                    createdBooks += 1

        self.stdout.write(
            self.style.SUCCESS(f"seed_books: {createdBooks} 件の書籍を新規作成しました。")
        )
