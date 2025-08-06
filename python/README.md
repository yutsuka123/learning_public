# Python 学習・復習フォルダ

## 目的
Pythonの基礎から最新機能まで、そしてPyTorchを活用した機械学習・深層学習の実装を目的とします。データサイエンス、AI開発に必要なスキルを総合的に学習します。

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

### PyTorch連携
- **テンソル操作**: tensor、autograd、backward
- **ニューラルネットワーク**: nn.Module、Layer定義
- **データローダー**: Dataset、DataLoader、前処理
- **モデル訓練**: 損失関数、最適化、バックプロパゲーション
- **深層学習**: CNN、RNN、Transformer

### データサイエンス
- **NumPy**: 配列操作、数値計算、ブロードキャスト
- **Pandas**: データフレーム、データ分析、前処理
- **Matplotlib/Seaborn**: データ可視化、グラフ作成
- **Scikit-learn**: 機械学習、前処理、評価指標

## プロジェクト構成
```
python/
├── basics/            # 基本的なPython機能
├── oop/              # オブジェクト指向プログラミング
├── modern-features/  # 最新のPython機能
├── data-science/     # データサイエンス（NumPy、Pandas）
├── visualization/    # データ可視化
├── pytorch-basics/   # PyTorch基本操作
├── deep-learning/    # 深層学習実装
├── machine-learning/ # 機械学習アルゴリズム
└── projects/         # 実践プロジェクト
```

## 学習方針
- Pythonic なコードの書き方
- 型ヒントを活用した保守性の向上
- PyTorchを使った実践的なAI開発
- データサイエンスワークフローの習得
- 詳細な日本語コメントとドキュメント