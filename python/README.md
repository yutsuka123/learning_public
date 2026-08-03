# Python 学習・復習フォルダ

## 目的
Pythonの基礎から最新機能まで、そしてPyTorchを活用した機械学習・深層学習の実装を目的とします。データサイエンス、AI開発に必要なスキルを総合的に学習します。

[相互参照] Web/DB/データ分析/AI/画像処理/組み込み/テスト等、分野別の主要ライブラリの
概要・人気・需要・日本海外での傾向は [`LIBRARY_LANDSCAPE_GUIDE.md`](LIBRARY_LANDSCAPE_GUIDE.md) を参照。

## 学習内容

### 基本的な言語機能
- **基本データ型**: int、float、str、bool、None
- **コレクション型**: list、tuple、dict、set
- **制御構造**: if文、for文、while文、内包表記
- **関数**: 引数、戻り値、デコレータ、ジェネレータ

### オブジェクト指向プログラミング
- **クラスとオブジェクト**: コンストラクタ、メソッド、プロパティ
- **継承**: 単一継承、多重継承、MRO（Method Resolution Order）
- **特殊メソッド**: __init__、__str__、__repr__、__call__
- **プロパティとデスクリプタ**: getter、setter、property

### 最新のPython機能（Python 3.8以降）
- **型ヒント**: typing、Optional、Union、Generic
- **データクラス**: @dataclass、フィールド定義
- **パターンマッチング**: match文（Python 3.10以降）
- **Walrus演算子**: 代入式（:=）
- **f-string**: フォーマット文字列

### フレームワーク実践例（ファイル分割）

- **PyTorch**: `examples/pytorch_practice.py`（コメント多めの最小訓練ループ）／統合長め例は `pytorch_sample.py`
- **TensorFlow**: `examples/tensorflow_practice.py`（PyTorch版と同じ題材で書き方を比較。**別venv必須**、後述）
- **FastAPI**: `examples/fastapi_practice.py`（単一ファイル API、`Depends`によるDI・統一例外ハンドラ付き）
- **Django**: `examples/django_minimal/`（最小プロジェクト＋管理画面＋ JSON API＋N+1問題と`select_related`の実演）
- **実務レベル目安**: `examples/README.md` のラダー（自己評価用）

[注意] `tensorflow_practice.py`のみ、検証時点の最新TensorFlowがPython 3.14未対応だったため
`python/.venv-tensorflow`（Python 3.11）という別の仮想環境で検証している
（他のファイルが使う`.venv-examples`とは別）。

### 組み込み Python（MicroPython）

- **場所**: `embedded_micropython/`
- **内容**: GPIO/PWM/ADC/UART/I2C/SPI/タイマー割り込み（実機必須）、`_thread`/`asyncio`による
  並列・並行処理、`socket`通信、`uctypes`/`memoryview`によるポインタ的操作、C/CPython/MicroPythonの
  メモリモデル比較。詳細は `embedded_micropython/README.md` を参照。

### Python ⇔ C/C++ 相互呼び出し

- **場所**: `interop_c_cpp/`
- **内容**: `ctypes`でCライブラリを呼ぶ（Python→C）、`pybind11`でC++クラスを公開する
  （Python→C++）、Python C APIでCおよびC++からPythonインタプリタを埋め込み呼び出す
  （C/C++→Python、C版とRAII化したC++版の両方）。詳細は `interop_c_cpp/README.md` を参照。

### 画像処理

- **場所**: `image_processing/`
- **内容**: Pillow（リサイズ/クロップ/回転/フィルタ/描画）、numpy（画像を配列として直接操作）、
  OpenCV（グレースケール/エッジ検出/輪郭検出）、scikit-learn KMeansによる色ベース画像分割
  （教師なしセグメンテーション）、torchvisionの学習済みモデルによるセマンティック
  セグメンテーション。詳細は `image_processing/README.md` を参照。

### データベース（SQLite / PostgreSQL）

- **場所**: `database/`
- **内容**: SQLite基礎（CRUD/パラメータ化クエリ/トランザクション/`with`の落とし穴）、
  SQLインジェクションの実演（脆弱な例が突破される様子と防御）、DB設計の良い例悪い例
  （型/制約/インデックスの効果を実測）、PostgreSQL基礎（`RETURNING`句、`psycopg`での
  `with`の違い）。基本/作法/注意点/プロレベルの勘所は `database/README.md` に整理。

### PyTorch / TensorFlow連携
- **場所**: `examples/pytorch_practice.py`（+ 長め例`pytorch_sample.py`）、`examples/tensorflow_practice.py`
- **内容**: テンソル操作・autograd/GradientTape・`nn.Module`/Kerasレイヤー定義・
  損失関数・最適化（SGD）・学習ループを、同じ題材（y≈Wx+bの線形回帰）で
  PyTorchとTensorFlowの両方の書き方を比較しながら実装。

### データ分析（pandas / matplotlib・seaborn / 時系列）

- **場所**: `data_analysis/`
- **内容**: pandas基礎（`DataFrame`操作、`groupby`、`merge`、欠損値処理、`describe`/`corr`）、
  matplotlib/seaborn（ヘッドレス環境向け`Agg`バックエンド設定込み）によるグラフ作成、
  時系列データ処理（`resample`/`rolling`/`shift`/`diff`）。
  ファイル: `pandas_basics.py`, `matplotlib_seaborn_basics.py`, `timeseries_pandas.py`。

### CAD / 3Dプリンター連携

- **場所**: `cad_3d_printing/`
- **内容**: **CadQuery**（OpenCascadeベースのパラメトリックCAD。STL/STEP出力）を
  主力サンプルとして採用し、**SolidPython2+OpenSCAD**は参考用の副サンプルとした
  （SolidPythonが依存するOpenSCADのHomebrew caskで実際にGatekeeper非推奨警告が
  出ることを確認したため、信頼性の観点からCadQueryを推奨としている）。
  採用判断の詳細は `cad_3d_printing/README.md` を参照。

### Webスクレイピング

- **場所**: `web_scraping/`
- **内容**: `requests`+`BeautifulSoup`によるスクレイピング練習用サンドボックスサイト
  （quotes.toscrape.com）からのデータ取得、`robots.txt`確認・User-Agent/タイムアウト/
  レート制限などのマナー、pandasでの集計分析（`.explode()`によるタグ分析）。
  ファイル: `scraping_and_analysis.py`。

### 自動テスト（pytest）

- **場所**: `testing/`
- **内容**: `pytest`のフィクスチャ・`@pytest.mark.parametrize`・`pytest.raises`・
  `monkeypatch`・`tmp_path`、および「今日の日付」を関数内部で取得せず引数で
  注入するなど**テストしやすい設計**の実例。ファイル: `inventory.py`, `test_inventory.py`。

### Excel / Office自動化

- **場所**: `office_automation/`
- **内容**: `openpyxl`（セル操作/数式/書式/読み戻し、pandasの`ExcelWriter`連携）、
  `python-docx`によるWord文書生成、`python-pptx`によるPowerPoint生成。

### OCR（文字認識）

- **場所**: `ocr/`
- **内容**: `pytesseract`（Tesseract 5.5.3をHomebrewで導入）によるOCR。
  フォントサイズ・解像度が認識精度に直接影響することを実際の比較画像で検証
  （10ptは誤認識、28ptは正しく認識）、日本語OCR（適切なフォント指定が必要）。

### GUI（tkinter / FreeSimpleGUI）

- **場所**: `gui/`
- **内容**: 標準ライブラリの`tkinter`（コールバック方式）と、サードパーティの
  `FreeSimpleGUI`（`window.read()`イベントループ方式）で同じアプリを実装し比較。
  **PySimpleGUI**は2023年のライセンス変更で現行版が有償化されたことを実際に
  PyPIで確認した上で、無償LGPL3の`FreeSimpleGUI`を採用した経緯を
  `gui/README.md` に記載。

### 標準ライブラリ使いこなし

- **場所**: `stdlib_mastery/`
- **内容**: サードパーティ無しで、`os`/`os.path`/`pathlib`・`sys`・`math`・`re`・
  `collections`（`Counter`/`defaultdict`/`namedtuple`）・`itertools`・
  `functools`（`lru_cache`/`reduce`/`partial`）・`json`の実務頻出パターンを網羅。

### ROS2（実現可能性調査 + 最小サンプル）

- **場所**: `ros2_basics/`
- **内容**: 公式ROS2はLinux前提でmacOS向けHomebrewパッケージが存在しないため、
  conda-forgeベースの**RoboStack**（`robostack-staging`チャンネル）経由で
  ネイティブ`osx-arm64`ビルドが利用できることを確認し、実際に`rclpy`による
  Publisher/Subscriberノード間でのメッセージ送受信を検証した。
  **他のフォルダと異なりconda環境（`conda activate ros2_test`）が必要**。
  詳細は `ros2_basics/README.md` を参照。

### 組み込み Python（MicroPython）／Python⇔C/C++／画像処理／DB

これらは前掲の各節（§組み込みPython、§Python⇔C/C++相互呼び出し、§画像処理、
§データベース）を参照。

## プロジェクト構成
```
python/
├── examples/              # PyTorch/TensorFlow/FastAPI/Django実践例
├── embedded_micropython/  # MicroPython（GPIO/通信/並行処理/メモリモデル比較）
├── interop_c_cpp/         # ctypes / pybind11 / Python C API 埋め込み
├── image_processing/      # Pillow/numpy/OpenCV/scikit-learn/torchvision
├── database/              # SQLite / PostgreSQL / スキーマ設計
├── data_analysis/         # pandas / matplotlib・seaborn / 時系列
├── cad_3d_printing/       # CadQuery / SolidPython+OpenSCAD
├── web_scraping/          # requests + BeautifulSoup
├── testing/               # pytest
├── office_automation/     # openpyxl / python-docx / python-pptx
├── ocr/                   # pytesseract
├── gui/                   # tkinter / FreeSimpleGUI
├── stdlib_mastery/        # os/sys/math/re/collections/itertools/functools/json
├── ros2_basics/           # ROS2（RoboStack, conda環境が別途必要）
├── LIBRARY_LANDSCAPE_GUIDE.md  # 分野別ライブラリの人気・需要まとめ
└── pytorch_sample.py      # PyTorch統合サンプル（長め）
```

## 学習方針
- Pythonic なコードの書き方
- 型ヒントを活用した保守性の向上
- PyTorchを使った実践的なAI開発
- データサイエンスワークフローの習得
- 詳細な日本語コメントとドキュメント