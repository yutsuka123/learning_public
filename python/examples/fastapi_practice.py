"""
FastAPI 実践入門サンプル（単一ファイル）

概要:
    小さな REST API を 1 ファイルで定義し、リクエスト検証とレスポンスモデルを体験する。
    [重要] examples/README.md の自己評価ラダーにおける「レベル2: 依存注入（Depends）で
    認可やDBセッションを差し替え可能な形にできる」「レベル3: 例外ハンドラ、ステータスコードの
    統一方針を決められる」に対応するため、以下2点を導入している。
    - ストアを `Depends(getItemStore)` で注入する（グローバル変数を直接参照しない）。
      理由: テスト時に `app.dependency_overrides` で差し替えられるようにするため。
      本番でDBセッションに置き換える際も、この依存関数の中身だけを変えればよい。
    - ドメイン例外 `ItemNotFoundError` を `@app.exception_handler` で一元的に処理する。
      理由: 各エンドポイントで try/except を繰り返さず、エラーレスポンスの形式を1箇所に統一するため。
主な仕様:
    - GET /health … 稼働確認
    - POST /items … JSON ボディを Pydantic で検証し、作成結果を返す（インメモリ保存のデモ）
制限事項:
    - データはプロセス内メモリのみ。再起動で消える（本番では DB へ）。
    - 認証・レート制限・HTTPS はこのデモでは扱わない。

依存関係:
    pip install "fastapi>=0.110" "uvicorn[standard]>=0.27"

実行例:
    uvicorn fastapi_practice:app --reload --port 8000
    # 別ターミナル例:
    # curl http://127.0.0.1:8000/health
    # curl -X POST http://127.0.0.1:8000/items -H "Content-Type: application/json" -d "{\"name\":\"book\",\"price\":123}"
"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, List, Optional
from uuid import uuid4

from fastapi import Depends, FastAPI, Request, status
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field, field_validator


def utcNowIso() -> str:
    """
    現在時刻を UTC の ISO8601 文字列で返す。

    目的:
        ログやレスポンスの時刻表現をタイムゾーン明示で統一する。
    戻り値:
        str: ISO8601 形式の UTC 時刻。
    """

    return datetime.now(timezone.utc).isoformat()


class ItemCreateRequest(BaseModel):
    """
    アイテム作成 API の入力モデル。

    目的:
        FastAPI がリクエスト JSON を自動検証し、型のついた Python オブジェクトに変換する流れを示す。
    """

    name: str = Field(..., min_length=1, max_length=80, description="商品名など、人間可読な名称。")
    price: int = Field(..., ge=0, le=10_000_000, description="価格（整数）。通貨単位は呼び出し側合意。")
    tags: Optional[List[str]] = Field(default=None, description="任意タグ。未指定なら空扱い。")

    @field_validator("tags")
    @classmethod
    def normalizeTags(cls, value: Optional[List[str]]) -> List[str]:
        """
        tags を正規化する。

        目的:
            None と [] の差を API 上で扱いやすくそろえる。
        引数:
            value (Optional[List[str]]): 入力タグ列。
        戻り値:
            List[str]: 正規化後のタグ列（None なら空リスト）。
        """

        if value is None:
            return []
        cleaned: List[str] = []
        for tag in value:
            stripped = tag.strip()
            if stripped:
                cleaned.append(stripped)
        return cleaned


class ItemResponse(BaseModel):
    """
    アイテム作成・取得のレスポンスモデル。

    目的:
        OpenAPI スキーマに載る「出力の契約」を明示する。
    """

    id: str
    name: str
    price: int
    tags: List[str]
    createdAt: str


class ItemNotFoundError(Exception):
    """
    アイテムが見つからない場合のドメイン例外。

    目的:
        リポジトリ層は HTTP を知らなくてよいようにし、HTTP への変換は
        `@app.exception_handler(ItemNotFoundError)` 側に一元化する。
    属性:
        item_id (str): 見つからなかった id。
    """

    def __init__(self, item_id: str) -> None:
        self.item_id = item_id
        super().__init__(f"item not found: {item_id}")


@dataclass
class InMemoryItemStore:
    """
    インメモリの簡易リポジトリ。

    目的:
        DB なしで CRUD の流れを素早く示す。本番では SQLAlchemy 等に置き換える。
    属性:
        itemsById (Dict[str, ItemResponse]): id をキーに保持。
    """

    itemsById: Dict[str, ItemResponse] = field(default_factory=dict)

    def createItem(self, payload: ItemCreateRequest) -> ItemResponse:
        """
        アイテムを新規作成する。

        引数:
            payload (ItemCreateRequest): 検証済みの作成リクエスト。
        戻り値:
            ItemResponse: 付与した id を含む保存結果。
        """

        new_id = str(uuid4())
        record = ItemResponse(
            id=new_id,
            name=payload.name,
            price=payload.price,
            tags=list(payload.tags or []),
            createdAt=utcNowIso(),
        )
        self.itemsById[new_id] = record
        return record

    def getItem(self, item_id: str) -> ItemResponse:
        """
        id でアイテムを取得する。無ければ ItemNotFoundError。

        引数:
            item_id (str): UUID 文字列を想定。
        戻り値:
            ItemResponse: 該当レコード。
        例外:
            ItemNotFoundError: 該当 id が存在しない場合。
        """

        if item_id not in self.itemsById:
            raise ItemNotFoundError(item_id)
        return self.itemsById[item_id]

    def listItems(self) -> List[ItemResponse]:
        """
        全件一覧を返す（デモ用。本番ではページング必須）。
        """

        return list(self.itemsById.values())


# --- 依存性注入 (Dependency Injection) --------------------------------------
#
# [重要] エンドポイント関数はグローバル変数を直接参照せず、`Depends(getItemStore)` で
# 受け取る。理由:
#   - テスト時に `app.dependency_overrides[getItemStore] = lambda: FakeStore()` の
#     ように差し替えられる（本物のストアを一切変更せずモックへ切り替えられる）。
#   - 本番でDBセッションに置き換える際も、この依存関数の中身だけを変えればよく、
#     各エンドポイントのシグネチャ（`store: InMemoryItemStore = Depends(...)`）は
#     変えずに済む。
_store = InMemoryItemStore()


def getItemStore() -> InMemoryItemStore:
    """
    アイテムストアを提供する依存関数（FastAPIの `Depends` から呼ばれる）。

    戻り値:
        InMemoryItemStore: プロセス内で共有するシングルトン
        （本番では `yield` を使ってリクエストスコープのDBセッションを提供する形が定番）。
    """

    return _store


app = FastAPI(
    title="fastapi_practice",
    version="0.1.0",
    description="学習用の最小 FastAPI 例。examples/README.md のラダーと併読を推奨。",
)


# =============================================================================
# [重要] このファイルに繰り返し出てくる `@app.xxx(...)` は「デコレータ」という
# Python の言語機能。以下、仕組みと FastAPI での使われ方をまとめて解説する
# （個々の `@app.get(...)` 等の直前コメントでは、ここで説明した前提のうえで
#  差分だけを書く）。
#
# 1) デコレータそのものの仕組み（FastAPI固有ではなく、Python標準の機能）
#    `@decorator` を関数定義の直前に書くと、Pythonは次のコードと**同じ意味**に解釈する。
#
#        def healthCheck(): ...
#        healthCheck = app.get("/health")(healthCheck)
#
#    つまり `app.get("/health")` が「関数を受け取って関数を返す関数」を作って返し、
#    それを元の `healthCheck` に適用している。デコレータは「関数を、別の（多くの場合は
#    元の関数を内部で呼び出しつつ何かを追加する）関数に置き換える」ための糖衣構文。
#
# 2) FastAPIでの意味: 「このURLパス+HTTPメソッドが呼ばれたら、この関数を実行する」という
#    ルーティング表（実体は `app.routes` というリスト）へ登録する副作用を持つ。
#    - `@app.get("/health")`  → GET /health を healthCheck に割り当てる
#    - `@app.post("/items")`  → POST /items を createItemEndpoint に割り当てる
#    - `@app.exception_handler(ItemNotFoundError)` → その型の例外が飛んだら
#      この関数（下記1))で呼び出す、という例外ハンドラ表へ登録する
#    いずれも「元の関数の中身を変えない」点が特徴（関数自体はそのまま呼べる状態を保ちつつ、
#    フレームワーク側の管理台帳に登録するだけ）。
#
# 3) `@app.get`/`@app.post` に渡せる主な引数（このファイルで使っているもの）:
#    - `response_model=ItemResponse` : 戻り値をこのPydanticモデルの形へ変換・検証し、
#      OpenAPI（自動生成されるAPI仕様書 /docs）にもレスポンス形式として載せる。
#    - `status_code=status.HTTP_201_CREATED` : 正常時に返すHTTPステータスコードを固定する
#      （指定しなければ既定は200）。
#    - `summary="アイテム作成"` : /docs 画面に表示される短い説明文（挙動には影響しない）。
#
# 4) デコレータされた関数の「引数」は、Python構文としてはただの通常の関数引数だが、
#    FastAPIはその型ヒントを見て自動的に埋める（本ファイルの `Depends(getItemStore)` は
#    その代表例。パスパラメータ・クエリパラメータ・リクエストボディも同じ仕組みで注入される）。
# =============================================================================


# --- 例外ハンドラ (統一エラーレスポンス) -------------------------------------
#
# [重要] 各エンドポイントで try/except を繰り返す代わりに、例外の型ごとに
# 1箇所でHTTPレスポンスへ変換する。エラーレスポンスの形式（{"error": ..., "path": ...}）が
# エンドポイント間でぶれないという利点がある。
# `@app.exception_handler(型)` の意味は上の解説ブロック2)のとおり:
# 「この型の例外が飛んだら、この関数を呼んでレスポンスへ変換する」という登録。


@app.exception_handler(ItemNotFoundError)
async def handleItemNotFoundError(request: Request, exc: ItemNotFoundError) -> JSONResponse:
    """
    ItemNotFoundError を 404 レスポンスへ変換する。

    引数:
        request (Request): 発生元のリクエスト（パスをログ/レスポンスに含めるため）。
        exc (ItemNotFoundError): 捕捉した例外。
    戻り値:
        JSONResponse: 404 とエラーメッセージ。
    """

    return JSONResponse(
        status_code=status.HTTP_404_NOT_FOUND,
        content={"error": str(exc), "path": request.url.path},
    )


@app.exception_handler(Exception)
async def handleUnexpectedError(request: Request, exc: Exception) -> JSONResponse:
    """
    想定外の例外を 500 レスポンスへ変換する「最後の砦」のハンドラ。

    [注意] FastAPI/Starlette は例外の型を厳密一致優先で解決するため、`RequestValidationError`や
    `HTTPException`など個別に登録済みのハンドラがある例外は、そちらが優先して処理される
    （このハンドラに横取りされない）。

    引数:
        request (Request): 発生元のリクエスト。
        exc (Exception): 捕捉した例外。
    戻り値:
        JSONResponse: 500 とエラーメッセージ。
    """

    return JSONResponse(
        status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
        content={"error": f"internal error: {exc}", "path": request.url.path},
    )


# --- エンドポイント (ルーティング) -------------------------------------------
#
# 以下の `@app.get(パス, ...)` / `@app.post(パス, ...)` の意味は上の解説ブロック2), 3)のとおり:
# 「このHTTPメソッド+パスが呼ばれたら、この関数を実行する」という登録。
# `response_model`/`status_code`/`summary` 引数の意味も同ブロック3)を参照。


@app.get("/health", summary="ヘルスチェック")
def healthCheck() -> dict:
    """
    プロセスが応答するかを確認する。

    戻り値:
        dict: status と時刻。
    """

    return {"status": "ok", "timeUtc": utcNowIso()}


@app.post("/items", response_model=ItemResponse, status_code=status.HTTP_201_CREATED, summary="アイテム作成")
def createItemEndpoint(
    payload: ItemCreateRequest,
    store: InMemoryItemStore = Depends(getItemStore),
) -> ItemResponse:
    """
    アイテムを作成する。

    引数:
        payload (ItemCreateRequest): リクエストボディ。FastAPI が検証する。
        store (InMemoryItemStore): `Depends(getItemStore)` で注入されるリポジトリ。
    戻り値:
        ItemResponse: 201 Created の本文。
    """

    # [注意] ここで想定外の例外が起きても、末尾の handleUnexpectedError が
    # 一元的に500へ変換するため、try/exceptを個々のエンドポイントで書く必要はない。
    return store.createItem(payload)


@app.get("/items/{item_id}", response_model=ItemResponse, summary="アイテム取得")
def getItemEndpoint(
    item_id: str,
    store: InMemoryItemStore = Depends(getItemStore),
) -> ItemResponse:
    """
    id 指定でアイテムを取得する。

    引数:
        item_id (str): パスパラメータ。
        store (InMemoryItemStore): `Depends(getItemStore)` で注入されるリポジトリ。
    戻り値:
        ItemResponse: 該当レコード。
    """

    # store.getItem が ItemNotFoundError を投げた場合、handleItemNotFoundError が
    # 404レスポンスへ変換する（このエンドポイントでは404を意識する必要がない）。
    return store.getItem(item_id)


@app.get("/items", response_model=List[ItemResponse], summary="アイテム一覧")
def listItemsEndpoint(store: InMemoryItemStore = Depends(getItemStore)) -> List[ItemResponse]:
    """
    インメモリの全件を返す。

    引数:
        store (InMemoryItemStore): `Depends(getItemStore)` で注入されるリポジトリ。
    戻り値:
        List[ItemResponse]: 件数が多いと危険なのでデモ専用とする。
    """

    return store.listItems()
