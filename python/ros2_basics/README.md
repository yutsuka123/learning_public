# ROS2 (Robot Operating System 2) macOSでの実現可能性調査 + 最小サンプル

[重要] 「調査」の依頼だったため、まず**macOSで本当に動かせるか**を確認することを優先した。
結論：**公式パッケージではなくRoboStack（conda-forge）経由なら、実機無しでもネイティブに動く。**

## 結論サマリ

| 項目 | 結果 |
|---|---|
| Homebrewで直接インストール | **不可**。公式formula/caskが存在しない（ROS/ROS2はLinux前提） |
| Dockerで動かす | 可能だが、今回はネイティブ実行を優先したため未使用 |
| **RoboStack (conda-forge)** | **可能**。`osx-arm64`向けのネイティブconformバイナリが配布されている |

## RoboStackとは

ROS/ROS2をconda-forgeのパッケージとして再配布しているコミュニティプロジェクト
（`robostack-staging`チャンネル）。Linux専用が前提のROS公式ビルドと異なり、
macOS（Intel/Apple Silicon）・Windows向けにもネイティブビルドを提供している。

## 環境構築手順（実際に実行し成功した手順）

```sh
# 1. 軽量なconda配布（miniforge）をHomebrewで導入
brew install miniforge

# 2. RoboStack chanelからROS2 Humble ros-baseを新しいconda環境にインストール
#    （事前に--dry-runでパッケージ解決を確認してから本実行した）
conda create -n ros2_test -c robostack-staging -c conda-forge ros-humble-ros-base --dry-run
conda create -n ros2_test -c robostack-staging -c conda-forge ros-humble-ros-base -y

# 3. 環境を有効化
conda activate ros2_test
```

## 動作確認（実際に検証済み）

```sh
conda activate ros2_test
ros2 pkg list | wc -l   # → 189（利用可能なROS2パッケージ数）
python3 minimal_pub_sub.py
```

`minimal_pub_sub.py`実行結果（実際の出力。抜粋）:
```
[Publisher] sending: temperature=25.3
[Subscriber] received: temperature=25.3
[Publisher] sending: temperature=25.7
[Subscriber] received: temperature=25.7
[Publisher] sending: temperature=26.1
[Subscriber] received: temperature=26.1
received count: 3 (expected 3)
```

`rclpy`（ROS2のPythonクライアントライブラリ）によるPublisher/Subscriberノード間で
実際にメッセージが送受信できることを確認した。

## サンプルの内容

- `minimal_pub_sub.py`: `PublisherNode`と`SubscriberNode`を同一プロセス内に生成し、
  `sensor_data`トピック経由で`std_msgs/String`メッセージを送受信する最小例。
  本来は別プロセス（別ターミナル、`ros2 run`経由）で動かすものだが、
  自動検証しやすいよう1ファイルにまとめた。

## 学習メモ・注意点

- **`conda activate`が必須**: 通常のPython（`.venv-examples`等）には`rclpy`は無い。
  必ず`conda activate ros2_test`してから実行する（本フォルダのサンプルは他のPython
  フォルダとは異なる独立した実行環境を必要とする、唯一の例外）。
- **`ros-humble-ros-base`は最小構成**: RViz等のGUIツールを含む`ros-humble-desktop`は
  さらに容量が大きいため、今回は基本ライブラリ（`ros-base`）のみをインストールした。
- **実機無しでの検証範囲**: 今回確認したのはPub/Subの基本メカニズムのみ。
  実際のロボット制御（`turtlesim`によるシミュレーション、実センサ/アクチュエータとの
  連携）は未検証。興味があれば`ros-humble-desktop`＋`turtlesim`パッケージの追加が次のステップ。

## 検証環境

- macOS (Apple Silicon, osx-arm64)
- miniforge (conda) + RoboStackチャンネル (`robostack-staging` + `conda-forge`)
- ROS2 Humble, `ros-humble-ros-base`, Python 3.12.13（conda環境内蔵、システムPythonとは別）
