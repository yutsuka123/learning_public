"""
FreeSimpleGUI による基本操作サンプル（tkinterとの比較用）。

概要:
    `tkinter_basics.py`と同じ「摂氏→華氏変換」アプリを、FreeSimpleGUIで作る。
    tkinterが「ウィジェットにコールバック関数を紐付ける」方式なのに対し、
    FreeSimpleGUIは「`window.read()`でイベントをループしながら待ち受ける」方式で、
    書き方の思想が異なる点を比較できる。

[重要・ライブラリ選定について]
    PySimpleGUIは2023年にライセンス形態を変更し、現行版(6.x系)は有償ライセンスに
    なっている（無償で使えるのは最終無償版の4.60.5.1のみ、PyPIで実際に確認済み）。
    本サンプルでは、無償・LGPL3で活発にメンテナンスされているコミュニティフォーク
    **FreeSimpleGUI**を採用した（PySimpleGUIとほぼ同じAPIで、`import`文を
    変えるだけで大半のコードがそのまま動く）。

主な仕様:
    - `sg.Window`でウィンドウを作り、`window.read()`のループでイベントを待つ。
    - "Convert"ボタン押下、またはウィンドウを閉じる操作をイベントとして受け取る。
制限事項:
    - 自動検証用に環境変数`GUI_AUTO_CLOSE_MS`を設定すると、指定ミリ秒後に
      自動でウィンドウを閉じる（通常の対話的な利用では設定不要）。

実行方法:
    cd python/gui
    pip install FreeSimpleGUI
    python3 freesimplegui_basics.py
    # 検証環境: FreeSimpleGUI 5.2.0 で自動検証済み（GUI_AUTO_CLOSE_MS使用）。
"""

import os

import FreeSimpleGUI as sg


def celsiusToFahrenheit(celsius):
    """摂氏から華氏へ変換する。"""

    return celsius * 9 / 5 + 32


def buildWindow():
    """
    ウィンドウのレイアウトを組み立てる。

    [重要] FreeSimpleGUI（PySimpleGUI系）は、ウィジェットを「2次元リストのリスト」
    （＝行ごとの配置）として宣言的に書く。tkinterの`grid()`/`pack()`のような
    配置メソッド呼び出しが不要で、見た目のレイアウトとコードの見た目が近い。

    戻り値:
        FreeSimpleGUI.Window: 組み立てたウィンドウ（`finalize=True`で即座に描画確定）。
    """

    layout = [
        [sg.Text("Celsius:"), sg.InputText("25", key="-CELSIUS-")],
        [sg.Button("Convert"), sg.Text("", key="-RESULT-", size=(25, 1))],
    ]
    return sg.Window("Celsius to Fahrenheit (FreeSimpleGUI)", layout, finalize=True)


def runEventLoop(window):
    """
    イベントループを実行する。

    目的:
        `window.read()`は「次に何かイベント（ボタン押下、ウィンドウを閉じる等）が
        起きるまでブロックして待つ」関数。tkinterの`mainloop()`+コールバック方式とは
        対照的に、「何が起きたか」をループの中で明示的に分岐して処理する
        （ゲームのメインループにも近い書き方）。

    引数:
        window (FreeSimpleGUI.Window): 対象ウィンドウ。
    戻り値:
        str | None: 最後に表示した結果テキスト（自動検証用）。
    """

    autoCloseMs = os.environ.get("GUI_AUTO_CLOSE_MS")
    if autoCloseMs:
        window.TKroot.after(100, lambda: window.write_event_value("-AUTO_CONVERT-", None))
        window.TKroot.after(int(autoCloseMs), lambda: window.write_event_value(sg.WIN_CLOSED, None))

    lastResultText = None
    while True:
        event, values = window.read()

        if event in (sg.WIN_CLOSED, None):
            break

        if event in ("Convert", "-AUTO_CONVERT-"):
            try:
                celsiusValue = float(values["-CELSIUS-"])
                fahrenheitValue = celsiusToFahrenheit(celsiusValue)
                lastResultText = f"Fahrenheit: {fahrenheitValue:.1f}"
            except ValueError:
                lastResultText = "Fahrenheit: (invalid input)"
            window["-RESULT-"].update(lastResultText)

    window.close()
    return lastResultText


if __name__ == "__main__":
    appWindow = buildWindow()
    finalResult = runEventLoop(appWindow)
    print(f"final result text (captured before window closed): {finalResult}")
