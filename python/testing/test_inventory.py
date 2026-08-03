"""
pytestの主要機能を、`inventory.py`の自動テストを通して確認するサンプル。

概要:
    素朴な`assert`ベースのテストから、fixture（テストごとの前準備の共通化）、
    parametrize（同じテストを複数の入力で繰り返す）、pytest.raises（例外のテスト）、
    monkeypatch（依存の差し替え）、tmp_path（一時ファイルを使うテスト）まで、
    pytestで頻出するパターンを一通り確認する。
制限事項:
    - このファイルは `pytest` コマンドから実行される前提（`if __name__ == "__main__"`は無い）。
      関数名は必ず `test_` で始める必要がある（pytestが自動的に収集する規約）。

実行方法:
    cd python/testing
    pip install pytest
    pytest -v test_inventory.py
    # 検証環境: pytest 9.1.1 で全テストのPASSを確認済み。
"""

import datetime

import pytest

from inventory import (
    Inventory,
    InsufficientStockError,
    calculateTotalValue,
    isExpired,
    loadInventoryFromFile,
    saveInventoryToFile,
)


def test_addStock_increases_quantity():
    """
    [基本] `assert`だけで書ける、最も単純なテスト。
    """

    inventory = Inventory()
    inventory.addStock("pen", 10)
    assert inventory.quantityOf("pen") == 10


def test_addStock_rejects_non_positive_quantity():
    """
    [悪い入力] 0以下の数量を追加しようとするとValueErrorになることを確認する。
    """

    inventory = Inventory()
    with pytest.raises(ValueError):
        inventory.addStock("pen", 0)


@pytest.fixture
def stockedInventory():
    """
    [fixture] 複数のテストで使い回す「ペンが10個入った在庫」を用意する。

    目的:
        各テスト関数の冒頭で同じセットアップを繰り返し書かなくて済むようにする。
        この関数名(`stockedInventory`)を、テスト関数の引数名として書くだけで
        pytestが自動的に呼び出し、戻り値を渡してくれる。
    戻り値:
        Inventory: "pen"が10個入った状態のInventoryインスタンス。
    """

    inventory = Inventory()
    inventory.addStock("pen", 10)
    return inventory


def test_removeStock_decreases_quantity(stockedInventory):
    """
    [fixture利用] 引数名を`stockedInventory`にするだけで、上のfixtureが適用される。
    """

    remaining = stockedInventory.removeStock("pen", 3)
    assert remaining == 7
    assert stockedInventory.quantityOf("pen") == 7


def test_removeStock_raises_when_insufficient(stockedInventory):
    """
    [例外テスト] 在庫以上の出庫を試みると`InsufficientStockError`になることを確認する。

    [重要] `pytest.raises`は「このブロック内で指定した例外が実際に発生すること」を
    要求する。何も例外が起きなければテストは失敗する（例外が"起きないこと"を確認する
    テストではない点に注意）。
    """

    with pytest.raises(InsufficientStockError) as excInfo:
        stockedInventory.removeStock("pen", 100)

    # 例外オブジェクト自体（メッセージ等）も検証できる
    assert "100" in str(excInfo.value)


# [parametrize] 同じテストロジックを、複数の入力/期待値の組で繰り返す。
# 引数名("quantity", "threshold", "expected")がテスト関数の引数と対応する。
@pytest.mark.parametrize(
    "quantity, threshold, expected",
    [
        (0, 5, True),  # 在庫0、閾値5 -> 低在庫
        (5, 5, True),  # 在庫=閾値 -> 低在庫（境界値）
        (6, 5, False),  # 在庫が閾値を上回る -> 低在庫ではない
    ],
)
def test_isLowStock_various_thresholds(quantity, threshold, expected):
    """
    [parametrize] 在庫数と閾値の組み合わせごとに、低在庫判定が正しいかを確認する。

    目的:
        `if文を3つ書いて3回assertする`代わりに、入力と期待値の表だけを書けば、
        pytestが3つの独立したテストケースとして実行してくれる（1つ失敗しても
        他のケースの結果は個別に分かる）。
    """

    inventory = Inventory()
    if quantity > 0:
        inventory.addStock("widget", quantity)
    assert inventory.isLowStock("widget", threshold) == expected


def test_calculateTotalValue():
    """
    [基本] 複数商品の在庫評価額を計算する。
    """

    stock = {"pen": 10, "notebook": 5, "unknown_item": 3}
    prices = {"pen": 100, "notebook": 300}
    # unknown_itemは価格未設定なので0円として扱われる想定
    assert calculateTotalValue(stock, prices) == 10 * 100 + 5 * 300 + 3 * 0


def test_isExpired_with_fixed_today():
    """
    [日付をテストする定石] `datetime.date.today()`を直接呼ばず、`today`引数として
    注入できる設計にしておくことで、実行日に依存しない決定的なテストが書ける。

    [重要] もし`isExpired`の内部で直接`datetime.date.today()`を呼んでいたら、
    「今日」を固定できず、実行するたびに結果が変わりかねない（またはmonkeypatchで
    `datetime`モジュール自体を差し替える、より重い対応が必要になる）。
    """

    fixedToday = datetime.date(2026, 6, 15)

    assert isExpired("2026-06-14", today=fixedToday) is True  # 昨日 -> 期限切れ
    assert isExpired("2026-06-15", today=fixedToday) is False  # 今日ちょうど -> まだ有効
    assert isExpired("2026-06-16", today=fixedToday) is False  # 明日 -> まだ有効


def test_isExpired_uses_monkeypatched_today(monkeypatch):
    """
    [monkeypatch] `today`引数を省略した場合の既定動作（実際の今日を使う）を、
    `datetime.date.today`自体を差し替えてテストする。

    目的:
        「引数で日付を渡せる設計」が使えない/使いたくない場合の代替手段として、
        `monkeypatch`で標準ライブラリの関数さえも一時的に差し替えられることを確認する
        （このテスト関数を抜けると、pytestが自動的に元に戻す）。
    """

    class FixedDate(datetime.date):
        @classmethod
        def today(cls):
            return cls(2026, 1, 1)

    monkeypatch.setattr(datetime, "date", FixedDate)

    assert isExpired("2025-12-31") is True  # 差し替えた「今日」(2026-01-01)より過去


def test_save_and_load_inventory_roundtrip(tmp_path):
    """
    [tmp_path] ファイルI/Oを伴う処理を、テスト専用の一時ディレクトリで検証する。

    目的:
        実際のファイルシステムに書き込む処理は、リポジトリ内の場所を汚さない
        一時ディレクトリでテストする。`tmp_path`はpytestが**テストごとに自動生成**する
        `pathlib.Path`で、テスト終了後の後始末もpytestに任せられる。
    """

    filePath = tmp_path / "inventory.json"
    original = {"pen": 10, "notebook": 5}

    saveInventoryToFile(original, filePath)
    assert filePath.exists()

    loaded = loadInventoryFromFile(filePath)
    assert loaded == original
