"""
pytestでテストする対象の、小さな在庫管理モジュール。

概要:
    `test_inventory.py`から実際にテストされる、意図的にシンプルな在庫管理ロジック。
    在庫の追加・出庫・在庫金額計算・期限切れ判定を提供する。
"""

from __future__ import annotations

import datetime
import json


class InsufficientStockError(Exception):
    """出庫しようとした数量が在庫数を上回る場合のエラー。"""


class Inventory:
    """
    商品名をキーにした在庫数量を管理する、シンプルな在庫クラス。
    """

    def __init__(self):
        self._stock = {}

    def addStock(self, itemName, quantity):
        """
        在庫を追加する。

        引数:
            itemName (str): 商品名。
            quantity (int): 追加する数量（正の整数）。
        戻り値:
            int: 追加後のその商品の在庫数。
        """

        if quantity <= 0:
            raise ValueError(f"quantity must be positive, got {quantity}")
        self._stock[itemName] = self._stock.get(itemName, 0) + quantity
        return self._stock[itemName]

    def removeStock(self, itemName, quantity):
        """
        在庫を出庫する。

        引数:
            itemName (str): 商品名。
            quantity (int): 出庫する数量（正の整数）。
        戻り値:
            int: 出庫後のその商品の在庫数。
        例外:
            InsufficientStockError: 在庫数が足りない場合。
        """

        current = self._stock.get(itemName, 0)
        if quantity > current:
            raise InsufficientStockError(
                f"cannot remove {quantity} of '{itemName}': only {current} in stock"
            )
        self._stock[itemName] = current - quantity
        return self._stock[itemName]

    def quantityOf(self, itemName):
        """指定商品の現在庫数を返す（未登録なら0）。"""

        return self._stock.get(itemName, 0)

    def isLowStock(self, itemName, threshold):
        """在庫数がthreshold以下ならTrue。"""

        return self.quantityOf(itemName) <= threshold


def calculateTotalValue(stockByItem, priceByItem):
    """
    在庫数量と単価から、在庫全体の評価額を計算する。

    引数:
        stockByItem (dict[str, int]): 商品名→数量。
        priceByItem (dict[str, float]): 商品名→単価。
    戻り値:
        float: 合計評価額。priceByItemに無い商品は0円として扱う。
    """

    return sum(quantity * priceByItem.get(itemName, 0) for itemName, quantity in stockByItem.items())


def isExpired(expiryDateIso, today=None):
    """
    賞味期限（ISO8601文字列）が今日より前かどうかを判定する。

    [重要] `today`を引数として外から渡せるようにしている（内部で`datetime.date.today()`を
    直接呼ばない）。理由: これにより、テスト側で「今日の日付」を差し替えて、
    未来・過去どちらの日付でも決定的にテストできる（`test_inventory.py`の
    `test_isExpired_with_fixed_today`参照）。

    引数:
        expiryDateIso (str): "YYYY-MM-DD"形式の賞味期限。
        today (datetime.date | None): 「今日」とみなす日付。Noneなら実際の今日を使う。
    戻り値:
        bool: 期限切れならTrue。
    """

    if today is None:
        today = datetime.date.today()
    expiryDate = datetime.date.fromisoformat(expiryDateIso)
    return expiryDate < today


def saveInventoryToFile(stockByItem, filePath):
    """
    在庫データをJSONファイルへ保存する。

    引数:
        stockByItem (dict[str, int]): 商品名→数量。
        filePath (str | pathlib.Path): 保存先パス。
    戻り値:
        None
    """

    with open(filePath, "w", encoding="utf-8") as f:
        json.dump(stockByItem, f, ensure_ascii=False)


def loadInventoryFromFile(filePath):
    """
    JSONファイルから在庫データを読み込む。

    引数:
        filePath (str | pathlib.Path): 読み込み元パス。
    戻り値:
        dict[str, int]: 商品名→数量。
    """

    with open(filePath, encoding="utf-8") as f:
        return json.load(f)
