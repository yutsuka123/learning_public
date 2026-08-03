"""
ビュー関数群。

目的:
    HttpRequest を受け取り HttpResponse（ここでは JsonResponse）を返す流れを示す。
    [重要] examples/README.md の自己評価ラダーにおける「レベル3: N+1 と select_related
    の話をコードレビューで指摘できる」に対応するため、あえてN+1を発生させる
    `n1DemoBooks` と、`select_related`で解決した`selectRelatedDemoBooks`を並べて置く。
    通常運用で使う `listBooks`/`bookDetail` は最初から`select_related`済みにしてある
    （N+1はデモ関数だけに封じ込め、本番相当のコードには残さない）。
"""

from __future__ import annotations

import json
from typing import Any, Dict, List

from django.db import connection, reset_queries
from django.http import HttpRequest, JsonResponse
from django.shortcuts import get_object_or_404
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_http_methods

from books.models import Author, Book


def _serializeBook(book: Book) -> Dict[str, Any]:
    """
    Book インスタンスを JSON 化しやすい dict に変換する。

    [注意] `book.author.name` にアクセスするたびに、`author`が未取得（select_related未使用）
    なら追加のSELECTクエリが発行される。呼び出し元がN+1を意識する必要があるため、
    このヘルパー自体はクエリ発行の有無を隠さず、呼び出し元のqueryset側で制御する設計にしている。

    引数:
        book (Book): 対象行。
    戻り値:
        Dict[str, Any]: クライアントへ返すプレーン辞書。
    """

    return {
        "id": book.id,
        "title": book.title,
        "authorName": book.author.name,
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

    # [重要] select_related("author") でBookとAuthorをJOINし、1クエリで取得する。
    # これを付け忘れると _serializeBook 内の book.author.name アクセスのたびに
    # 追加クエリが発行される（N+1問題。詳細は n1DemoBooks を参照）。
    rows: List[Book] = list(Book.objects.select_related("author").all())
    payload = {"items": [_serializeBook(book) for book in rows]}
    return JsonResponse(payload, json_dumps_params={"ensure_ascii": False})


@require_GET
def n1DemoBooks(request: HttpRequest) -> JsonResponse:
    """
    [悪い例] N+1問題を意図的に発生させる一覧API（学習用）。

    Book一覧を取得したあと、ループの中で `book.author.name` にアクセスするたびに、
    関連するAuthorを取得する追加クエリが1件ずつ発行される。書籍がN件あれば
    「一覧取得1件 + 著者取得N件」＝ 1+N 件のクエリが飛ぶ（N+1問題）。
    [禁止] 本番のコードでこのパターン（select_relatedなしでの関連先アクセスをループする）
    を書かないこと。理由: 件数が増えるほどDBラウンドトリップが線形に増え、性能劣化するため。

    戻り値:
        JsonResponse: { "items": [...], "queryCount": int }。queryCountで実際に
        発行されたSQLクエリ数を確認できる（selectRelatedDemoBooksと比較する）。
    """

    reset_queries()
    rows = list(Book.objects.all())  # ここで1クエリ（books テーブルへのSELECT）
    items = [
        {"id": b.id, "title": b.title, "authorName": b.author.name}  # 書籍ごとに追加で1クエリ
        for b in rows
    ]
    queryCount = len(connection.queries)
    return JsonResponse(
        {"items": items, "queryCount": queryCount},
        json_dumps_params={"ensure_ascii": False},
    )


@require_GET
def selectRelatedDemoBooks(request: HttpRequest) -> JsonResponse:
    """
    [良い例] select_related でN+1問題を解決した一覧API（学習用）。

    `select_related("author")` は SQL の JOIN を使い、BookとAuthorを1回のクエリで
    まとめて取得する。ループ内で `book.author.name` にアクセスしても追加クエリは発生しない。

    戻り値:
        JsonResponse: { "items": [...], "queryCount": int }。n1DemoBooksと同じ内容を
        返しつつ、queryCountが1件（書籍数に依存しない）になることを確認できる。
    """

    reset_queries()
    rows = list(Book.objects.select_related("author").all())  # JOINで1クエリにまとめる
    items = [
        {"id": b.id, "title": b.title, "authorName": b.author.name}  # 追加クエリは発生しない
        for b in rows
    ]
    queryCount = len(connection.queries)
    return JsonResponse(
        {"items": items, "queryCount": queryCount},
        json_dumps_params={"ensure_ascii": False},
    )


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
    authorName = str(data.get("author", "")).strip()
    if not title or not authorName:
        return JsonResponse(
            {"error": "bookCollection: title と author は必須です。", "received": data},
            status=400,
        )

    # [重要] クライアントに著者IDを意識させず名前だけで済ませるため、
    # get_or_create で「既存なら再利用、無ければ新規作成」する。
    author, _created = Author.objects.get_or_create(name=authorName)
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

    # 1件でもselect_relatedしておけば、_serializeBook内のauthor.nameアクセスで
    # 追加クエリが発生しない（get_object_or_404はquerysetを渡せる）。
    book = get_object_or_404(Book.objects.select_related("author"), pk=book_id)
    return JsonResponse(_serializeBook(book), json_dumps_params={"ensure_ascii": False})
