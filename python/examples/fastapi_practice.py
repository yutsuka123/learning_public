"""
FastAPI 実践入門サンプル（単一ファイル）

概要:
    小さな REST API を 1 ファイルで定義し、リクエスト検証とレスポンスモデルを体験する。
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

from fastapi import FastAPI, HTTPException, status
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
        id でアイテムを取得する。無ければ例外。

        引数:
            item_id (str): UUID 文字列を想定。
        戻り値:
            ItemResponse: 該当レコード。
        """

        if item_id not in self.itemsById:
            raise KeyError(item_id)
        return self.itemsById[item_id]

    def listItems(self) -> List[ItemResponse]:
        """
        全件一覧を返す（デモ用。本番ではページング必須）。
        """

        return list(self.itemsById.values())


store = InMemoryItemStore()
app = FastAPI(
    title="fastapi_practice",
    version="0.1.0",
    description="学習用の最小 FastAPI 例。examples/README.md のラダーと併読を推奨。",
)


@app.get("/health", summary="ヘルスチェック")
def healthCheck() -> dict:
    """
    プロセスが応答するかを確認する。

    戻り値:
        dict: status と時刻。
    """

    return {"status": "ok", "timeUtc": utcNowIso()}


@app.post("/items", response_model=ItemResponse, status_code=status.HTTP_201_CREATED, summary="アイテム作成")
def createItemEndpoint(payload: ItemCreateRequest) -> ItemResponse:
    """
    アイテムを作成する。

    引数:
        payload (ItemCreateRequest): リクエストボディ。FastAPI が検証する。
    戻り値:
        ItemResponse: 201 Created の本文。
    """

    try:
        return store.createItem(payload)
    except Exception as exc:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"createItemEndpoint: 作成処理に失敗しました。payload={payload.model_dump()}, 原因={exc}",
        ) from exc


@app.get("/items/{item_id}", response_model=ItemResponse, summary="アイテム取得")
def getItemEndpoint(item_id: str) -> ItemResponse:
    """
    id 指定でアイテムを取得する。

    引数:
        item_id (str): パスパラメータ。
    """

    try:
        return store.getItem(item_id)
    except KeyError:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"getItemEndpoint: 該当 id がありません。item_id={item_id}",
        ) from None
    except Exception as exc:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"getItemEndpoint: 取得処理に失敗しました。item_id={item_id}, 原因={exc}",
        ) from exc


@app.get("/items", response_model=List[ItemResponse], summary="アイテム一覧")
def listItemsEndpoint() -> List[ItemResponse]:
    """
    インメモリの全件を返す。

    戻り値:
        List[ItemResponse]: 件数が多いと危険なのでデモ専用とする。
    """

    return store.listItems()
