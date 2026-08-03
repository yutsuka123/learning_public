"""
ROS2 (rclpy) 最小Publisher/Subscriberサンプル。

概要:
    ROS2は「ノード」同士が「トピック」経由でメッセージをやり取りする分散システム。
    本来はPublisherノードとSubscriberノードを別プロセス（別ターミナル）で動かすが、
    ここでは自動検証しやすいように同一プロセス内でPublisherとSubscriberの両方の
    ノードを生成し、`rclpy.spin_once`でメッセージが実際に届くことを確認する。

主な仕様:
    - PublisherNode: `sensor_data`トピックへ`std_msgs/String`を1秒間隔で送信する想定だが、
      本サンプルでは検証を速くするため手動で`publish()`を複数回呼ぶ。
    - SubscriberNode: 同じ`sensor_data`トピックを購読し、受信したメッセージを
      リストへ蓄積するコールバックを登録する。
    - rclpy.spin_once(node, timeout_sec=...): ノードのイベントループを1回分だけ回す。
      本来の`rclpy.spin(node)`は無限ループするため、自動検証用に`spin_once`を使う。

環境構築（macOS。公式ROS2はLinux前提でHomebrewパッケージが無いため、
RoboStack＝conda-forgeチャンネル経由のネイティブosx-arm64ビルドを使用）:
    brew install miniforge
    conda create -n ros2_test -c robostack-staging -c conda-forge ros-humble-ros-base -y
    conda activate ros2_test
    python3 minimal_pub_sub.py

実行結果（例。実際に検証済み）:
    [Publisher] sending: temperature=25.3
    [Publisher] sending: temperature=25.7
    [Publisher] sending: temperature=26.1
    [Subscriber] received: temperature=25.3
    [Subscriber] received: temperature=25.7
    [Subscriber] received: temperature=26.1
    received count: 3 (expected 3)
"""

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


class PublisherNode(Node):
    def __init__(self):
        super().__init__("minimal_publisher")
        # [重要] create_publisher(型, トピック名, QoSプロファイルの深さ)。
        # 深さ10は「未購読側が受け取るまでにバッファできるメッセージ数」の目安。
        self.publisher = self.create_publisher(String, "sensor_data", 10)

    def sendMessage(self, text):
        msg = String()
        msg.data = text
        self.publisher.publish(msg)
        print(f"[Publisher] sending: {text}")


class SubscriberNode(Node):
    def __init__(self):
        super().__init__("minimal_subscriber")
        self.receivedMessages = []
        # [重要] create_subscriptionの第3引数はコールバック関数。
        # メッセージが届くたびに、rclpy.spin/spin_once実行中に自動で呼ばれる。
        self.subscription = self.create_subscription(
            String, "sensor_data", self.onMessageReceived, 10
        )

    def onMessageReceived(self, msg):
        print(f"[Subscriber] received: {msg.data}")
        self.receivedMessages.append(msg.data)


def main():
    rclpy.init()

    publisherNode = PublisherNode()
    subscriberNode = SubscriberNode()

    messages = [
        "temperature=25.3",
        "temperature=25.7",
        "temperature=26.1",
    ]

    for text in messages:
        publisherNode.sendMessage(text)
        # [重要] publish()は「送信キューに積む」だけで、実際の配送はexecutorが
        # イベントループを回した時に行われる。spin_onceでSubscriber側に届かせる。
        rclpy.spin_once(subscriberNode, timeout_sec=1.0)

    print(f"received count: {len(subscriberNode.receivedMessages)} (expected {len(messages)})")

    publisherNode.destroy_node()
    subscriberNode.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
