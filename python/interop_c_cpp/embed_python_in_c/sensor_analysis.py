"""
C埋め込みPython(embed_example.c)から呼び出される、ごく小さなPythonモジュール。

概要:
    「Cの中にPythonを埋め込んで、Python側の処理を呼ぶ」構成の実演用。C/C++の母体アプリが
    設定ファイルの解釈や、変更頻度の高いロジックだけをPythonスクリプトに委ねたい場合等に
    使われる構成（Pythonを「組み込みスクリプト言語」として使うパターン）。
"""


def average(values):
    """
    数値のリストの平均を返す。

    引数:
        values (list[float]): 数値のリスト。
    戻り値:
        float: 平均値（空リストなら0.0）。
    """

    if not values:
        return 0.0
    return sum(values) / len(values)
