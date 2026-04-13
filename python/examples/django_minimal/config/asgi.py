"""
ASGI エントリポイント（将来の WebSocket 等で使用）。

目的:
    非同期サーバが参照する設定を提供する。
"""

import os

from django.core.asgi import get_asgi_application

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "config.settings")

application = get_asgi_application()
