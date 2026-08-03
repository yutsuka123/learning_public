"""
tkinter（Python標準ライブラリのGUIツールキット）による基本操作サンプル。

概要:
    追加インストール不要（標準ライブラリ同梱）で使えるGUIツールキット。ウィンドウ・
    ラベル・入力欄・ボタンを配置し、ボタン押下イベントに応じて処理する
    「摂氏→華氏変換」アプリを作る。
主な仕様:
    - `Entry`で摂氏温度を入力し、`Button`押下で`Label`に華氏温度を表示する。
    - イベント処理は「ボタンにコールバック関数を紐付ける」方式（tkinterの基本パターン）。
制限事項:
    - [重要] macOSのHomebrew版Pythonは既定でtkinterが使えないことがある
      （`_tkinter`モジュール無し）。`brew install python-tk@3.14`
      （使用中のPythonバージョンに合わせる）で追加インストールが必要
      （実際にこのファイル作成時、この手順が必要だった）。
    - 自動検証用に環境変数`GUI_AUTO_CLOSE_MS`を設定すると、指定ミリ秒後にウィンドウが
      自動で閉じる（通常の対話的な利用では設定不要。ウィンドウは手動で閉じる）。

実行方法:
    cd python/gui
    brew install python-tk@3.14   # macOS。tkinterが使えない場合のみ
    python3 tkinter_basics.py
    # 検証環境: tkinter (Tk 9.0) で自動検証済み（GUI_AUTO_CLOSE_MS使用）。
"""

import os
import tkinter as tk
from tkinter import ttk


def celsiusToFahrenheit(celsius):
    """
    摂氏から華氏へ変換する。

    引数:
        celsius (float): 摂氏温度。
    戻り値:
        float: 華氏温度。
    """

    return celsius * 9 / 5 + 32


def buildApp(lastTextHolder=None):
    """
    ウィンドウとウィジェットを組み立てる。

    目的:
        ウィジェットの配置と、イベントハンドラ（ボタン押下時の処理）の結び付け方を確認する。

    引数:
        lastTextHolder (list[str] | None): 自動検証用。渡された場合、ラベルの表示テキストが
            更新されるたびに`lastTextHolder[0]`へ書き込む。
            [重要] ウィンドウ破棄(`root.destroy()`)後はウィジェットへ一切アクセスできず
            （`_tkinter.TclError: application has been destroyed`になる。実際にこのファイル
            作成時、破棄後に`resultLabel.cget(...)`を呼んで本当に遭遇した）、
            破棄前に値を退避しておく必要がある。このリストはそのための「窓口」。
    戻り値:
        tkinter.Tk: 組み立てたルートウィンドウ。
    """

    root = tk.Tk()
    root.title("Celsius to Fahrenheit")
    root.geometry("300x120")

    mainFrame = ttk.Frame(root, padding=10)
    mainFrame.pack(fill="both", expand=True)

    ttk.Label(mainFrame, text="Celsius:").grid(row=0, column=0, sticky="w")
    celsiusEntry = ttk.Entry(mainFrame)
    celsiusEntry.insert(0, "25")  # 初期値
    celsiusEntry.grid(row=0, column=1, sticky="ew")

    resultLabel = ttk.Label(mainFrame, text="Fahrenheit: --")
    resultLabel.grid(row=2, column=0, columnspan=2, sticky="w", pady=10)

    def onConvertClicked():
        """
        [重要] ボタンに紐付けるコールバック関数。tkinterでは「イベントが起きたら
        この関数を呼ぶ」という形で処理を書く（イベント駆動プログラミングの基本形）。
        """

        try:
            celsiusValue = float(celsiusEntry.get())
            fahrenheitValue = celsiusToFahrenheit(celsiusValue)
            newText = f"Fahrenheit: {fahrenheitValue:.1f}"
        except ValueError:
            newText = "Fahrenheit: (invalid input)"

        resultLabel.config(text=newText)
        if lastTextHolder is not None:
            lastTextHolder[0] = newText  # 破棄前に値を退避しておく

    convertButton = ttk.Button(mainFrame, text="Convert", command=onConvertClicked)
    convertButton.grid(row=1, column=0, columnspan=2, pady=5)

    mainFrame.columnconfigure(1, weight=1)

    # テスト/自動検証用: 環境変数が設定されていれば、指定時間後に自動でウィンドウを閉じる。
    autoCloseMs = os.environ.get("GUI_AUTO_CLOSE_MS")
    if autoCloseMs:
        # 自動テストとして「ボタン押下→表示更新」まで一通り動くことも確認してから閉じる。
        root.after(100, onConvertClicked)
        root.after(int(autoCloseMs), root.destroy)

    return root


if __name__ == "__main__":
    lastText = [None]
    app = buildApp(lastTextHolder=lastText)
    app.mainloop()
    print(f"final label text (captured before window closed): {lastText[0]}")
