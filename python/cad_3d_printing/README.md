# CAD / 3Dプリンター Python サンプル

[重要] このフォルダは、PythonでパラメトリックCAD（コードで立体形状を組み立てる手法）を扱い、
3Dプリンター用のSTL・CADソフト間交換用のSTEPを出力するためのものです。

## なぜCadQueryか（信頼性の調査結果）

「Pythonで動くCADライブラリ」として調査した結果、以下の理由でCadQueryを採用しました。

- **実績のあるCADカーネル(OpenCascade/OCCT)を使う**: 自前で幾何演算を実装しているのではなく、
  工業用途で実績のあるオープンソースCADカーネルOCCTのPythonラッパー。FreeCAD（GUI版の
  オープンソースCAD）もこのカーネルを使っており、車輪の再発明ではない、実績のある土台の上に
  構築されている。
- **能動的に開発が継続している**: 2026年時点でも活発にメンテナンスされている
  （検証時の最新版は2.8.0）。
- **標準フォーマットへの出力が容易**: STL（3Dプリンター用）、STEP（CAD交換用）等、
  業界標準フォーマットへの出力を1行で行える。

比較検討した他の選択肢:
- **SolidPython(2)**: OpenSCADという別ソフトの入力コード(.scadファイル)をPythonで生成する
  ラッパー。実際の幾何計算はOpenSCAD側が行うため、**OpenSCAD本体のインストールが別途必要**。
  [重要] 実際に調査したところ、macOS版OpenSCAD（Homebrew cask）は
  「**macOS Gatekeeperチェック不合格のため非推奨、2026-09-01にHomebrewから削除予定**」という
  警告が出た（`solidpython_openscad_basics.py`のファイル冒頭に実際の警告文を記載）。
  CLIとしての動作自体は確認できたが、将来的な入手性に懸念があるため**参考用サンプルとして
  別ファイルに分離**し、継続利用にはCadQueryを推奨することにした。
- **build123d**: CadQueryの設計思想を引き継ぐ新しいライブラリ。より新しくPythonicだが、
  CadQueryよりコミュニティ・実績がまだ少ない（2024年頃に本格始動）。将来的な選択肢として
  有望だが、本サンプルでは実績を優先しCadQueryを採用した。

## ファイル構成

| ファイル | 内容 | 信頼性 |
|---|---|---|
| `cadquery_basics.py` | 直方体/穴あけ/面取りの基本、パラメトリック設計、STL/STEPエクスポート | ✅ 推奨（純Pythonパッケージ、外部アプリ不要） |
| `solidpython_openscad_basics.py` | SolidPython2でSCADコード生成→OpenSCAD CLIでSTLへレンダリング | ⚠️ 参考用（macOS版OpenSCADの配布終了懸念あり、上記参照） |

## セットアップ

```sh
cd python/cad_3d_printing
pip install -r requirements.txt

# solidpython_openscad_basics.py を試す場合のみ（任意、上記の信頼性の注意を参照）
brew install openscad --cask
```

[注意] CadQueryはOpenCascade(OCCT)という大きなCADカーネルに依存しており、初回`import`時の
初期化に数十秒かかることがある（2回目以降は速い）。

## 実行方法

```sh
python3 cadquery_basics.py               # 推奨
python3 solidpython_openscad_basics.py   # 参考用（openscadコマンドが必要）
```

`bracket.stl`（3Dプリンター用）・`bracket.step`（CAD交換用）が生成されます。
`bracket.stl`はスライサーソフト（[Cura](https://ultimaker.com/software/ultimaker-cura/)、
[PrusaSlicer](https://www.prusa3d.com/page/prusaslicer_424/)等）に読み込んで、
実際に3Dプリンター用のGコードへ変換できます。

## 検証環境

- macOS, Python 3.14, cadquery 2.8.0, solid2 (SolidPython2), OpenSCAD 2021.01 (Homebrew cask)
- 両ファイルとも実行して、体積の実測値・STL/STEPファイルが実際に有効なフォーマットで
  出力されることを確認済み（`file`コマンドで検証）。

## 関連ファイル

- 構造体・ポインタ・配列の深掘り（形状をデータとして表現する考え方の基礎）:
  `../../c/structures/struct_pointer_array_deep_dive.c`
