# GUI Python サンプル（tkinter / FreeSimpleGUI）

[重要] このフォルダは、同じ「摂氏→華氏変換」アプリを`tkinter`（標準ライブラリ）と
`FreeSimpleGUI`（サードパーティ）の両方で作り、書き方の思想の違いを比較するためのものです。

## なぜPySimpleGUIではなくFreeSimpleGUIか

`PySimpleGUI`は2023年にライセンス形態を変更し、現行版(6.x系)は有償ライセンスに
なっていることを実際にPyPIで確認した（`pip index versions PySimpleGUI`で
`6.3, 6.2, 6.0, 4.60.5.1`のみが返り、無償で使えるのは最終無償版の`4.60.5.1`だけ）。
そのため本フォルダでは、無償・LGPL3で活発にメンテナンスされているコミュニティフォーク
**FreeSimpleGUI**を採用した。PySimpleGUIとほぼ同じAPIのため、`import`文を
変えるだけで大半のコードがそのまま動く。

## ファイル構成

| ファイル | 内容 |
|---|---|
| `tkinter_basics.py` | 標準ライブラリのtkinter。コールバック関数をウィジェットに紐付ける方式 |
| `freesimplegui_basics.py` | FreeSimpleGUI。`window.read()`のイベントループ方式 |

## セットアップ

```sh
# tkinterがimportできない場合（Homebrew版Pythonでよくある）
brew install python-tk@3.14   # 使用中のPythonバージョンに合わせる

cd python/gui
pip install -r requirements.txt
```

## 実行方法

```sh
python3 tkinter_basics.py
python3 freesimplegui_basics.py
```

通常はウィンドウが表示されたままになるので、手動で閉じてください。自動検証したい場合は
環境変数`GUI_AUTO_CLOSE_MS`を設定すると、指定ミリ秒後に自動でボタン押下→ウィンドウを
閉じる動作を行います（例: `GUI_AUTO_CLOSE_MS=800 python3 tkinter_basics.py`）。

## 検証時に実際に踏んだ問題（学習メモ）

- **Homebrew版Pythonにtkinterが無い**: `import tkinter`が
  `ModuleNotFoundError: No module named '_tkinter'`で失敗した。
  `brew install python-tk@3.14`（Pythonのバージョンに対応するもの）を追加インストールして解決した。
- **destroy後のウィジェットアクセスはエラーになる**: `tkinter_basics.py`で、ウィンドウを
  `destroy()`した後に`resultLabel.cget(...)`を呼んだところ
  `_tkinter.TclError: application has been destroyed`が発生した。ウィンドウを閉じる前に、
  必要な値を外部の変数へ退避しておく設計に修正した。

## 検証環境

- macOS, tkinter (Tk 9.0, `python-tk@3.14`経由), FreeSimpleGUI 5.2.0
- 両ファイルとも、環境変数`GUI_AUTO_CLOSE_MS`を使って自動実行し、
  変換結果（25℃→77.0℉）が正しく表示されることを確認済み。
