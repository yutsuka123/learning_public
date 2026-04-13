"""
ビュー関数群。

目的:
    HttpRequest を受け取り HttpResponse（ここでは JsonResponse）を返す流れを示す。
"""

from __future__ import annotations

import json
from typing import Any, Dict, List

from django.http import HttpRequest, JsonResponse
from django.shortcuts import get_object_or_404
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_http_methods

from books.models import Book


def _serializeBook(book: Book) -> Dict[str, Any]:
    """
    Book インスタンスを JSON 化しやすい dict に変換する。

    引数:
        book (Book): 対象行。
    戻り値:
        Dict[str, Any]: クライアントへ返すプレーン辞書。
    """

    return {
        "id": book.id,
        "title": book.title,
        "author": book.author,
        "publishedAt": book.published_at.isoformat(),
    }


@require_GET
def listBooks(request: HttpRequest) -> JsonResponse:
    """
    全書籍を JSON 配列で返す簡易 API。

    引数:
        request (HttpRequest): HTTP リクエスト（GET のみ許可）。
    戻り値:
        JsonResponse: { "items": [...] } 形式。
    """

    rows: List[Book] = list(Book.objects.all())
    payload = {"items": [_serializeBook(book) for book in rows]}
    return JsonResponse(payload, json_dumps_params={"ensure_ascii": False})


@csrf_exempt
@require_http_methods(["GET", "POST"])
def bookCollection(request: HttpRequest) -> JsonResponse:
    """
    GET: 一覧。POST: 新規作成（JSON ボディ）。

    [禁止] 本番 API で `@csrf_exempt` を安易に使わないこと。理由: CSRF 攻撃面となるため。
    [念のため保存] 学習用に curl 等から POST を試せるよう一時的に免除している。

    目的:
        小さな CRUD の入口を 1 関数にまとめ、ルーティングを単純化する。
    引数:
        request (HttpRequest): メソッドにより分岐。
    戻り値:
        JsonResponse: 一覧または作成結果。
    """

    if request.method == "GET":
        return listBooks(request)

    # POST: application/json を想定
    try:
        body = request.body.decode("utf-8") if request.body else "{}"
        data = json.loads(body)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return JsonResponse(
            {"error": f"bookCollection: JSON の解析に失敗しました。body={request.body!r}, 原因={exc}"},
            status=400,
        )

    title = str(data.get("title", "")).strip()
    author = str(data.get("author", "")).strip()
    if not title or not author:
        return JsonResponse(
            {"error": "bookCollection: title と author は必須です。", "received": data},
            status=400,
        )

    book = Book.objects.create(title=title, author=author)
    return JsonResponse(_serializeBook(book), status=201, json_dumps_params={"ensure_ascii": False})


@require_GET
def bookDetail(request: HttpRequest, book_id: int) -> JsonResponse:
    """
    1 件取得 API。

    引数:
        request (HttpRequest): HTTP リクエスト。
        book_id (int): 主キー。
    """

    book = get_object_or_404(Book, pk=book_id)
    return JsonResponse(_serializeBook(book), json_dumps_params={"ensure_ascii": False})
