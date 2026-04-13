"""
WSGI エントリポイント。

目的:
    本番サーバ（gunicorn 等）から呼び出される設定を提供する。
"""

import os

from django.core.wsgi import get_wsgi_application

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "config.settings")

application = get_wsgi_application()
