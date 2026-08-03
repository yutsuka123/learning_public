# 自動テスト（pytest）Python サンプル

[重要] このフォルダは、pytestによる自動テストの主要機能を、実際に動くテスト対象コード
（`inventory.py`）とそのテスト（`test_inventory.py`）で確認するためのものです。

## ファイル構成

| ファイル | 内容 |
|---|---|
| `inventory.py` | テスト対象の小さな在庫管理モジュール |
| `test_inventory.py` | 上記に対する pytest テスト。fixture/parametrize/例外テスト/monkeypatch/tmp_path を一通り実演 |

## 確認できるpytestの機能

- **基本の`assert`**: `test_addStock_increases_quantity`
- **`pytest.raises`（例外テスト）**: `test_removeStock_raises_when_insufficient`
- **`@pytest.fixture`（前準備の共通化）**: `stockedInventory`
- **`@pytest.mark.parametrize`（同じテストを複数の入力で繰り返す）**: `test_isLowStock_various_thresholds`
- **`monkeypatch`（依存の一時的な差し替え）**: `test_isExpired_uses_monkeypatched_today`
- **`tmp_path`（一時ファイルを使うテスト）**: `test_save_and_load_inventory_roundtrip`

## 実行方法

```sh
cd python/testing
pip install pytest
pytest -v test_inventory.py
```

## 検証環境

- Python 3.14, pytest 9.1.1
- 全11テストがPASSすることを確認済み。

## 設計上の工夫（テストしやすいコードの書き方）

`inventory.py`の`isExpired`関数は、内部で`datetime.date.today()`を直接呼ぶのではなく
`today`引数として外から日付を注入できる設計にしている。これにより「今日」を
固定してテストでき、実行日に依存しない決定的なテストが書ける
（`monkeypatch`で標準ライブラリ関数自体を差し替えるのは有効な代替手段だが、
可能であれば依存を引数として渡せる設計にする方がテストが単純になる、という比較を
`test_isExpired_with_fixed_today`と`test_isExpired_uses_monkeypatched_today`の
両方を並べることで確認できる）。
