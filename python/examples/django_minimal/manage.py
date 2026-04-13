#!/usr/bin/env python
"""
Django の管理コマンドエントリポイント。

目的:
    `python manage.py <command>` の起点となるスクリプト。
"""

import os
import sys


def main() -> None:
    """
    DJANGO_SETTINGS_MODULE を設定し、コマンドラインから Django を起動する。

    例外:
        インポート失敗時は分かりやすいメッセージを表示する。
    """

    os.environ.setdefault("DJANGO_SETTINGS_MODULE", "config.settings")
    try:
        from django.core.management import execute_from_command_line
    except ImportError as exc:
        raise ImportError(
            "manage.py: Django をインポートできませんでした。"
            " 仮想環境を有効化し、`pip install -r requirements.txt` を実行してください。"
            f" sys.executable={sys.executable}, 原因={exc}"
        ) from exc
    execute_from_command_line(sys.argv)


if __name__ == "__main__":
    main()
