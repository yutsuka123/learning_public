"""
書籍・著者モデル定義モジュール。

目的:
    ORM の基本（CharField、DateTimeField、__str__）に加え、ForeignKey による
    テーブル間の関連（1対多）を示す。Author と Book の関係は N+1 問題の実演にも使う
    （books/views.py の n1DemoBooks / selectRelatedDemoBooks を参照）。
"""

from django.db import models


class Author(models.Model):
    """
    著者を表すモデル。

    属性:
        name (str): 著者名（一意）。
    """

    name = models.CharField("著者名", max_length=120, unique=True)

    class Meta:
        ordering = ["name"]
        verbose_name = "著者"
        verbose_name_plural = "著者"

    def __str__(self) -> str:
        """
        管理画面などでの表示用文字列。

        戻り値:
            str: 著者名。
        """

        return self.name


class Book(models.Model):
    """
    書籍 1 行を表すモデル。

    属性:
        title (str): 書名。
        author (Author): 著者への外部キー。1人の著者が複数の書籍を持てる（1対多）。
        published_at (datetime): 登録日時（自動設定）。
    """

    title = models.CharField("タイトル", max_length=200)
    author = models.ForeignKey(
        Author,
        verbose_name="著者",
        on_delete=models.CASCADE,
        related_name="books",
    )
    published_at = models.DateTimeField("登録日時", auto_now_add=True)

    class Meta:
        ordering = ["-published_at", "id"]
        verbose_name = "書籍"
        verbose_name_plural = "書籍"

    def __str__(self) -> str:
        """
        管理画面などでの表示用文字列。

        戻り値:
            str: タイトルと著者名を含む短い説明。
        """

        return f"{self.title} / {self.author.name}"
