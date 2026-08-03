# Python ライブラリ分野別ガイド

[重要] 本書はPythonの主要ライブラリを分野別に整理し、それぞれの概要・特長・人気/需要
（一般的な業界動向としての傾向）・日本/海外での使用頻度の違いをまとめたものです。
[注意] 「人気」「需要」「日本/海外での使用頻度」は、公開されている求人動向・
開発者調査（Stack Overflow Developer Survey、PyPIダウンロード統計等）や、
コミュニティで広く共有されている傾向を踏まえた**定性的な**整理であり、特定時点の
正確な統計値を保証するものではありません。意思決定の参考程度に留め、実際の採用判断では
最新の一次情報（求人票、公式ダウンロード統計等）を確認してください。
[相互参照] 各ライブラリの実行可能なサンプルコードは `技術スタック学習ガイド.md` および
本ファイル内の各リンク先を参照してください。

---

## 1. Web開発・バックエンド

### FastAPI
- **概要**: 型ヒントベースでリクエスト検証・OpenAPI仕様書を自動生成する、非同期対応の
  APIフレームワーク。
- **人気/需要**: 2018年公開と比較的新しいが、Python製Webフレームワークの中で
  最も急成長している。非同期処理の性能とドキュメント自動生成が評価され、
  新規プロジェクトでの採用が海外を中心に急増中。
- **日本/海外**: 海外では新規API開発の第一選択肢になりつつある。日本国内でも
  スタートアップ・新規サービスでの採用が増加中だが、エンタープライズでは
  まだDjangoほど実績蓄積が多くない。
- **このリポジトリのサンプル**: [`python/examples/fastapi_practice.py`](examples/fastapi_practice.py)

### Django
- **概要**: ORM・管理画面・認証・マイグレーション等を標準搭載する「フルスタック」フレームワーク。
- **人気/需要**: 2005年公開、長年の実績と安定性から、規模の大きいWebサービスや
  管理業務系システムで根強い需要。求人でも「Django経験者」は継続的に一定数存在する。
- **日本/海外**: 海外・日本問わず定番。日本語書籍・記事も豊富で、初学者が学びやすい
  フレームワークの1つとされる。
- **このリポジトリのサンプル**: [`python/examples/django_minimal/`](examples/django_minimal/)

### Flask（このリポジトリには未収録）
- **概要**: 最小構成から始められる軽量フレームワーク。必要な機能を自分で組み合わせる思想。
- **人気/需要**: FastAPI登場前は「軽量API」の代表格だった。既存資産（レガシーFlaskアプリの
  保守）の求人は今も一定数ある。新規開発ではFastAPIに流れる傾向。
- **日本/海外**: 海外・日本とも、教育用途（学習しやすさ）や小規模プロトタイプでの利用が中心。

---

## 2. データベース / ORM

### sqlite3（標準ライブラリ）
- **概要**: 追加インストール不要でPythonに同梱。ファイル1つで完結する組み込みDB。
- **人気/需要**: 「DBを直接扱う基礎」を学ぶ入口として、どの学習者もほぼ必ず触れる。
  本番のバックエンドとしての採用は限定的（PostgreSQL/MySQL等が主流）。
- **このリポジトリのサンプル**: [`python/database/sqlite_basics.py`](database/sqlite_basics.py)

### psycopg（PostgreSQL用ドライバ）
- **概要**: PythonからPostgreSQLへ接続するための定番ドライバ。3系(`psycopg`)が現行版。
- **人気/需要**: PostgreSQL自体が「機能豊富なOSS RDBMS」として海外を中心に評価が高く、
  近年は日本国内の新規開発でもMySQLからの乗り換えが進んでいる。
- **このリポジトリのサンプル**: [`python/database/postgresql_basics.py`](database/postgresql_basics.py)

### SQLAlchemy（このリポジトリには未収録）
- **概要**: Python最大手のORM/SQLツールキット。生SQLに近い操作からフルORMまで幅広く対応。
- **人気/需要**: FastAPIやFlaskと組み合わせて使われることが多く、「DjangoのORM以外」を
  選ぶ場合の事実上の標準。求人票でも頻出。
- **日本/海外**: 海外の方が採用事例の情報発信が多い印象だが、日本国内のFastAPI採用増加に
  伴い需要も増加中。

---

## 3. データ分析

### pandas（このリポジトリには未収録）
- **概要**: 表形式データ（DataFrame）を扱うための定番ライブラリ。Excel感覚でのデータ加工・
  集計・欠損値処理等が可能。
- **人気/需要**: データ分析職種では「Python=pandas」と言えるほど普及。求人票で
  「pandas経験」は非常に頻出するキーワード。
- **日本/海外**: 世界共通で事実上の標準。日本語の学習コンテンツも非常に豊富。

### numpy
- **概要**: 多次元配列（ndarray）と数値計算の基盤ライブラリ。pandas/PyTorch/scikit-learn等、
  多くのライブラリが内部でnumpyに依存する。
- **人気/需要**: Pythonでの数値計算をする以上、直接使わなくても間接的に必須のライブラリ。
- **このリポジトリのサンプル**: [`python/image_processing/numpy_pixel_manipulation.py`](image_processing/numpy_pixel_manipulation.py)

### Matplotlib / Seaborn（このリポジトリには未収録）
- **概要**: グラフ描画ライブラリ。Matplotlibは柔軟だが記述量が多め、Seabornは
  統計的なグラフをより簡潔に描ける（Matplotlibの上に構築されている）。
- **人気/需要**: データ分析・研究発表でのグラフ作成に広く使われる。近年はPlotly等
  インタラクティブな可視化ライブラリとの併用も増加。

---

## 4. AI / 機械学習

### PyTorch
- **概要**: Meta（旧Facebook）発のディープラーニングフレームワーク。研究分野で特に強く、
  近年は本番運用（プロダクション）向け機能も充実してきている。
- **人気/需要**: AI研究論文の実装で最も使われるフレームワークとされ、AI関連求人でも
  頻出。生成AIブームでさらに需要拡大。
- **日本/海外**: 海外（特に研究コミュニティ）での採用がやや先行するが、日本国内の
  AI/ML求人でも標準的なスキルとして扱われる。
- **このリポジトリのサンプル**: [`python/examples/pytorch_practice.py`](examples/pytorch_practice.py)、
  [`python/image_processing/semantic_segmentation_pytorch.py`](image_processing/semantic_segmentation_pytorch.py)

### TensorFlow（このリポジトリには未収録）
- **概要**: Google発のディープラーニングフレームワーク。PyTorchと並ぶ2大巨頭。
- **人気/需要**: 一時期はPyTorchより主流だったが、近年は研究分野を中心にPyTorchへ
  シェアが移りつつあると言われる。産業界・モバイル/エッジ展開（TensorFlow Lite）では
  今も強い。
- **日本/海外**: Google関連製品・エッジデバイス向け開発で根強い需要がある。

### scikit-learn
- **概要**: ディープラーニング以外の「古典的な」機械学習（分類/回帰/クラスタリング等）の
  定番ライブラリ。シンプルで統一されたAPIが特長。
- **人気/需要**: 「まずは古典的な手法から検証する」実務フローで頻繁に使われ、
  データ分析職種で広く求められるスキル。
- **このリポジトリのサンプル**: [`python/image_processing/kmeans_color_segmentation.py`](image_processing/kmeans_color_segmentation.py)

---

## 5. 画像処理 / コンピュータビジョン

### Pillow (PIL)
- **概要**: 画像の読み込み・保存・リサイズ・フィルタ等、画像「編集」寄りの定番ライブラリ。
- **人気/需要**: 画像を扱うPythonプロジェクトのほぼすべてで、直接または間接的に使われる
  基礎的なライブラリ。
- **このリポジトリのサンプル**: [`python/image_processing/pillow_basics.py`](image_processing/pillow_basics.py)

### OpenCV
- **概要**: コンピュータビジョン（画像・映像からの情報抽出）の定番ライブラリ。C++実装を
  Pythonから呼び出す構成（`cv2`）。
- **人気/需要**: 製造業の外観検査、防犯カメラ解析、自動運転の要素技術など、実応用の
  現場で広く使われる。CV関連求人での必須スキルの1つ。
- **このリポジトリのサンプル**: [`python/image_processing/opencv_basics.py`](image_processing/opencv_basics.py)

---

## 6. 組み込み

### MicroPython
- **概要**: マイコン向けの軽量Python処理系。CPythonのサブセット + ハードウェア制御用
  `machine`モジュール。
- **人気/需要**: ESP32/Raspberry Pi Picoの普及とともに、ホビー〜プロトタイピング用途で
  採用が拡大。C言語より学習コストが低く、IoT教育でも使われる。
- **日本/海外**: 海外のメイカームーブメント（Arduino/Raspberry Pi文化）と親和性が高く
  海外での情報量がやや多いが、日本国内でも技術書典等のコミュニティで着実に情報が増えている。
- **このリポジトリのサンプル**: [`python/embedded_micropython/README.md`](embedded_micropython/README.md)

---

## 7. テスト

### pytest（このリポジトリには未収録）
- **概要**: Python最大手のテストフレームワーク。標準の`unittest`より簡潔な記法で書ける。
- **人気/需要**: 新規プロジェクトの単体テストでは事実上の標準。求人票でも
  「pytestでのテスト経験」は頻出。
- **日本/海外**: 世界共通でデファクトスタンダード。

---

## 8. 非同期処理・HTTP通信

### asyncio（標準ライブラリ）
- **概要**: Python標準の非同期I/O・並行処理フレームワーク。FastAPIやMicroPythonの
  `asyncio`（`embedded_micropython/concurrency_examples.py`参照）もこの思想を踏襲。
- **人気/需要**: I/O待ちの多い処理（Web API、ネットワーク通信）を効率化する手段として
  必須級。async対応フレームワークの普及とともに重要度が上昇。
- **このリポジトリのサンプル**: [`python/embedded_micropython/concurrency_examples.py`](embedded_micropython/concurrency_examples.py)（MicroPython版だが考え方は同じ）

### requests / httpx（このリポジトリには未収録）
- **概要**: HTTP通信を行うためのライブラリ。`requests`は同期専用の定番、`httpx`は
  `requests`と似たAPIで非同期にも対応した後発ライブラリ。
- **人気/需要**: 外部APIを呼ぶあらゆるPythonプロジェクトで使われる基礎ライブラリ。
  非同期フレームワーク（FastAPI等）との組み合わせでは`httpx`の採用が増加中。

---

## 9. 型・データ検証

### Pydantic（このリポジトリではFastAPI経由で使用）
- **概要**: 型ヒントを使ったデータ検証・シリアライズライブラリ。FastAPIの入力検証の
  中核（`fastapi_practice.py`の`ItemCreateRequest`等）。
- **人気/需要**: FastAPIの普及とともに採用が急増。型安全性を重視する開発チームでの
  採用も増えている。
- **このリポジトリのサンプル**: [`python/examples/fastapi_practice.py`](examples/fastapi_practice.py)

### mypy（このリポジトリには未収録）
- **概要**: Pythonの型ヒントを静的に検査するツール。実行前に型の不整合を検出できる。
- **人気/需要**: 大規模なPythonコードベースの保守性向上のため、型ヒント+mypyの組み合わせは
  実務で徐々に標準化しつつある。

---

## 10. まとめ表（分野別・主要ライブラリ一覧）

| 分野 | 主要ライブラリ | このリポジトリでの実装 |
|---|---|---|
| Web/バックエンド | FastAPI, Django, (Flask) | ✅ FastAPI, Django |
| DB/ORM | sqlite3, psycopg, (SQLAlchemy) | ✅ sqlite3, psycopg |
| データ分析 | (pandas), numpy, (Matplotlib) | ✅ numpy（画像処理経由） |
| AI/機械学習 | PyTorch, (TensorFlow), scikit-learn | ✅ PyTorch, scikit-learn |
| 画像処理/CV | Pillow, OpenCV | ✅ 両方 |
| 組み込み | MicroPython | ✅ |
| テスト | (pytest) | 未実装 |
| 非同期/通信 | asyncio, (requests/httpx) | ✅ asyncio（MicroPython版） |
| 型/検証 | Pydantic, (mypy) | ✅ Pydantic（FastAPI経由） |

（カッコ書き = このリポジトリには未収録。今後追加したい場合はリクエストしてください。）

## 変更履歴

- 2026-08-03: 新規作成。Web/DB/データ分析/AI/画像処理/組み込み/テスト/非同期/型検証の
  9分野について、概要・人気/需要・日本海外での傾向を整理。
