"""
書籍モデル定義モジュール。

目的:
    ORM の基本（CharField、DateTimeField、__str__）を示す。
"""

from django.db import models


class Book(models.Model):
    """
    書籍 1 行を表すモデル。

    属性:
        title (str): 書名。
        author (str): 著者名（簡易のため文字列一括り）。
        published_at (datetime): 登録日時（自動設定）。
    """

    title = models.CharField("タイトル", max_length=200)
    author = models.CharField("著者", max_length=120)
    published_at = models.DateTimeField("登録日時", auto_now_add=True)

    class Meta:
        ordering = ["-published_at", "id"]
        verbose_name = "書籍"
        verbose_name_plural = "書籍"

    def __str__(self) -> str:
        """
        管理画面などでの表示用文字列。

        戻り値:
            str: タイトルと著者を含む短い説明。
        """

        return f"{self.title} / {self.author}"
