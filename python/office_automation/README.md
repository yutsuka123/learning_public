# Excel / Office 自動化 Python サンプル

[重要] このフォルダは、Excel/Word/PowerPointをアプリを起動せずにPythonだけで
作成・編集するためのサンプルです。定型レポートの自動生成等に使う。

## ファイル構成

| ファイル | 内容 | ライブラリ |
|---|---|---|
| `excel_automation.py` | セル書き込み/数式/書式設定/読み込み/pandas連携 | openpyxl, pandas |
| `word_powerpoint_automation.py` | Word文書（見出し/段落/表）、PowerPointスライド（タイトル/箇条書き） | python-docx, python-pptx |

## セットアップ

```sh
cd python/office_automation
pip install -r requirements.txt
```

## 実行方法

```sh
python3 excel_automation.py
python3 word_powerpoint_automation.py
```

## 検証時に実際に踏んだバグ（学習メモ）

`excel_automation.py`の`demonstrateReadBack`で、当初`min_row=2`を指定してヘッダー行を
スキップしたつもりだったが、実際のシート構成は「1行目=個別セル書き込み分、
2行目=`append()`で追加したname/scoreヘッダー」だったため、`min_row=2`では
ヘッダー行自体を読み込んでしまっていた。実行して出力を確認したことで発覚し、
`min_row=3`に修正した。「何行目から本当のデータか」を思い込みで決め打ちせず、
実際に作られたファイルの構造を確認することの重要性を示す実例。

## 検証環境

- Python 3.14, openpyxl 3.1.5, python-docx, python-pptx, pandas 3.0.5
- 全ファイル、実行して生成物のバイト数・内容を確認済み。
