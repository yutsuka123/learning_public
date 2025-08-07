# 日本語文字化け対策・フォント設定ガイド

本リポジトリのサンプルスクリプトを Windows の PowerShell あるいは VS Code のターミナルで実行した際、
日本語が "????" や "文字化け" として表示される場合は、以下の手順で解決できます。

---
## 1. コンソール(ターミナル) の文字コードを UTF-8 に変更する
PowerShell の場合、コマンドを実行する直前に次を実行してください。

```powershell
# 現在のコードページ番号を確認 (オプション)
chcp

# 65001 = UTF-8
chcp 65001
```

Windows Terminal / CMD でも同様です。

---
## 2. Python 側の標準入出力エンコーディングを UTF-8 に固定 (任意)
```powershell
# 一時的に
set PYTHONIOENCODING=utf-8

# 永続化したい場合は (ユーザ環境変数に追加)
[環境変数] → 新規 → 変数名: PYTHONIOENCODING, 値: utf-8
```

---
## 3. matplotlib グラフで日本語を正しく表示する
スクリプト `python/pytorch_sample.py` では、matplotlib が利用可能な場合に
自動で日本語フォントを設定する処理を追加済みです。

```python
from matplotlib import font_manager, rcParams
jp_fonts = ["IPAexGothic", "Noto Sans CJK JP", "Yu Gothic", "Meiryo"]
for font in jp_fonts:
    if any(font in f.name for f in font_manager.fontManager.ttflist):
        rcParams["font.family"] = font
        break
rcParams["axes.unicode_minus"] = False  # "−" 記号が文字化けしないように
```

### 3-1. 日本語フォントが PC に入っていない場合
1. [IPAexフォント](https://moji.or.jp/ipafont/) または
2. [Noto Sans CJK JP](https://fonts.google.com/noto/specimen/Noto+Sans+JP)

をダウンロードしてインストールしてください。インストール後、再度スクリプトを実行すると自動検出されます。

---
## 4. よくある Q&A
| 症状 | 対策 |
|---|---|
| コンソールの日本語が "????" | 手順 1 を実行して `chcp 65001` に変更 |
| matplotlib のラベルが "□" になる | 手順 3 で日本語フォントをインストール |
| `pip install` 後も文字化け | IDE が別仮想環境を使用していないか確認 |

---
最終更新: 2025-08-07
