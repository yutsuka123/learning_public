IoT/LocalServer readme

[重要]
- このフォルダは、TypeScript/Node.jsで実装するローカル管理サーバーを格納する。
- Web GUI、REST API、MQTT command送信、OTA配布用HTTPSエンドポイントを提供する。
理由: 複数ESP32の監視・制御・書換配布を単一サービスで運用するため。

[厳守]
- GUIはブラウザWebで提供する（ローカルアプリ化は将来対応）。
- MQTTブローカーはMosquittoを利用し、TLS + ID/パスワード認証を必須にする。
- 起動時に`status` callを送信し、ESP32一覧状態を初期同期する。
- PC自動起動はTask Schedulerで構成する。

[推奨]
- API仕様は `IoT/IF仕様書.md` と同期して管理する。
- 起動方法・設定方法は `README.md` と `scripts/` の手順に統一する。

[禁止]
- 開発用の暫定設定を本番設定として固定化しない。
- 機密情報を `.env`、`env.example.sample.txt`、ドキュメントへ直書きしない。
理由: 後戻り忘れと機密漏えいを防ぐため。

[変更履歴]
- 2026-03-08: `.env.example` 廃止方針に合わせ、サンプル設定ファイルを `env.example.sample.txt` 基準へ更新。
- 2026-03-07: 初期実装追加（Web UI、REST API、MQTT status/OTA command、OTA HTTPSエンドポイント、Task Scheduler導入スクリプト）。
- 2026-03-02: 新規作成。ローカルHTTPサーバー格納先として定義。
