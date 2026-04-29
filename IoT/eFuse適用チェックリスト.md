# 第3段階 eFuse適用チェックリスト

[重要] 本書は `006-0001` の成果物として、第3段階の `eFuse` / `Secure Boot` / `Flash Encryption` 着手前に確認すべき項目を固定する。  
理由: 不可逆処理の実行前に、文書・実装・試験・現場観測の確認漏れを防ぐため。

[厳守] 本書の整備完了は「チェックリスト文書が作成された」ことを意味し、`006-0002` 以降や `007` 章への進入許可を意味しない。  
理由: 進入可否は `003-0024` と `7085` の判定で別管理するため。

[禁止] 本書を根拠に `eFuse` / `Secure Boot` / `Flash Encryption` の本実行を前倒ししない。  
理由: フェーズDの現行スコープは準備完了までであり、不可逆処理本体はフェーズFリハーサル以降で扱うため。

## 1. 使用タイミング
- [厳守] `006-0002` 以降へ進む直前に、本書の全項目を確認する。
- [厳守] `007-0001` / `007-0002` の本番対象機着手前にも、本書を再確認する。
- [重要] `003-0024` の総合進入判定ゲートと `7085` の結果を先に参照し、その後に本書で作業単位の確認を行う。

## 2. 文書・試験ゲート確認
- [厳守] 次の項目が 1 件でも未達なら `006-0002` 以降へ進まない。

| ID | 確認項目 | 照合元 | 判定欄 |
|---|---|---|---|
| G-001 | `003-0024` が完了し、進入判定条件が `todo.md` と `試験仕様書.md` に固定されている | `todo.md`, `試験仕様書.md` | 未記入 |
| G-002 | `7085` の最新判定が確認され、未達理由または進入可否が明文化されている | `試験記録書.md` | 未記入 |
| G-003 | `004-0010` が完了し、`7088 -> 004-0010 -> 7085` の再判定順序が守られている | `todo.md`, `試験仕様書.md`, `コマンド仕様書.md` | 未記入 |
| G-004 | `003-0012` の未達が残っていない、または残課題が進入不可理由として明記されている | `todo.md`, `試験記録書.md`, `LocalServer秘密処理別層仕様書.md` | 未記入 |
| G-005 | `009-0019` が encrypted bundle 本体送達 / 復号 / NVS 保存まで到達済みである | `todo.md`, `試験記録書.md` | 未記入 |
| G-006 | `005-0001`〜`005-0007` が完了し、`005-0008` は `todo_old20260428.md` へ退避済みである（観点・突合は `ProductionTool画面仕様書.md` 9.4.6） | `todo.md`, `ProductionTool画面仕様書.md`, `機能仕様書.md` | 未記入 |
| G-007 | `004-0001`〜`004-0005` の試験用鍵・署名素材手順が文書化済みである | `todo.md`, `コマンド仕様書.md` | 未記入 |

## 3. リハーサル前提確認
- [厳守] 次の項目が揃うまでは、試験機リハーサルも開始しない。

| ID | 確認項目 | 照合元 | 判定欄 |
|---|---|---|---|
| R-001 | 試験機リハーサル対象の `firmwareVersion`、書込み手順、読戻し観点が固定されている | `ProductionTool画面仕様書.md`, `機能仕様書.md`, `試験仕様書.md` | 未記入 |
| R-002 | `k-iot-secure-boot` / `k-iot-flash-encryption` は **`コマンド仕様書.md` §10.2.5** で整備した試験用鍵を、**当面そのまま本番工程にも用いる**（別ファイルの本番専用生成は不要）。将来、本番専用へ分離する場合は `todo.md` **`020-0001`** を実施する | `todo.md`, `todo_old20260429.md`, `鍵一覧仕様書.md`, `コマンド仕様書.md` | 未記入 |
| R-003 | 途中失敗時は旧パーティションから継続起動し、AP メンテナンスで復旧できる前提が維持されている | `設計概要.md`, `OTA仕様書.md` | 未記入 |
| R-004 | `005-0008` は `todo_old20260428.md` へ退避済みであり、9.4.6 と `試験記録書.md` の突合を **設計のみ**でなく証跡付きで混同していない | `todo.md`, `ProductionTool画面仕様書.md` 9.4.6, `試験記録書.md`, `機能仕様書.md` | 未記入 |
| R-005 | `7022` の非不可逆版が定義済みであり、`stage_execute` 前中断時の再判定材料と無条件再実行禁止が固定されている | `試験仕様書.md`, `試験記録書.md`, `本番セキュア化出荷準備試験計画書.md` | 未記入 |
| R-006 | **[旧仕様][2026-04-23]** NVS Encryption 設定（`CONFIG_NVS_ENCRYPTION=y` 等）と eFuse HMAC鍵投入を同一段階で完結させる案は採用見送りとする。`todo.md` では NVS暗号化をクローズ済みとして扱う | `本番セキュア化出荷準備試験計画書.md` 5.6c, `todo_old20260423.md` | 退避 |
| R-007 | **[旧仕様][2026-04-23]** `secureNvsInit.cpp` の HMAC鍵不在時 fallback 前提は採用見送りとする。`beta.29` の履歴は残すが、現行の必須確認ではない | `ESP32/src/secureNvsInit.cpp`, `ESP32/header/version.h` | 退避 |
| R-008 | **[旧仕様][2026-04-23]** PlatformIO/Arduino 環境での `CONFIG_NVS_ENCRYPTION` 取り扱い注意は、NVS暗号化クローズに伴う参考情報として残す | `本番セキュア化出荷準備試験計画書.md` 5.6c | 退避 |

## 4. ProductionTool 実行前チェック
- [厳守] `PC-001`〜`PC-008` はすべて `OK` でなければ `stage_execute` へ進まない。
- [重要] `PC-002`、`PC-007`、`PC-008` は閾値比較結果を記録する。
- [禁止] 秘密値、raw key、`eFuse` 実値をチェック記録へ書かない。

| ID | 確認項目 | 取得方法/根拠 | 判定欄 |
|---|---|---|---|
| PC-001 | 対象機識別一致 | シリアル番号、MAC、`public_id`、対象機ID を照合 | 未記入 |
| PC-002 | 電源安定確認 | 工場治具または測定値で安定電源を確認 | 未記入 |
| PC-003 | 版数確認 | `expectedFirmwareVersion` と実測 `firmwareVersion` を照合 | 未記入 |
| PC-004 | 鍵ID確認 | 鍵ID、署名素材ID、ロット情報、作業指示番号を照合 | 未記入 |
| PC-005 | 未セキュア化状態確認 | 再実行禁止条件に該当しないことを確認 | 未記入 |
| PC-006 | 作業者認証確認 | `ProductionTool` 追加認証、操作者ID、端末識別情報を照合 | 未記入 |
| PC-007 | スタック余裕確認 | `minimumStackMarginBytes` と実測 stack margin を照合 | 未記入 |
| PC-008 | 空きヒープ確認 | `minimumFreeHeapBytes` と実測 free heap を照合 | 未記入 |
| PC-009 | **[致命的][2026-03-26追加]** 段階2(NVS)実行前に、FWが `secureNvsInit` フォールバック（`beta.29`以降）を含むか確認 | `firmwareVersion` 照合、`本番セキュア化出荷準備試験計画書.md` 5.9 | 未記入 |
| PC-010 | **[致命的][2026-03-26追加]** 段階2(NVS)完了後に AP HTTP 全ロール認証テスト（`/api/health` + `/api/auth/login`×4ロール）を自動実行し、全 OK を確認 | `7041:pipeline:strict` または同等の自動テスト | 未記入 |
| PC-011 | **[重要][2026-03-26追加]** 段階間でsdkconfigだけ先行変更しeFuse未投入の状態を作っていないこと | `sdkconfig.defaults` と eFuse 実施状況の照合 | 未記入 |
| PC-012 | **[致命的][2026-04-04追加]** 今回使用する build env が `esp32s3_secure` か `esp32s3_secure_final` か明示され、その理由を説明できること | `platformio.ini`, `コマンド仕様書.md` | 未記入 |
| PC-013 | **[重要][2026-04-04追加]** 4点証跡の保存先（`ProductionTool` 判定ログ、ESP32 シリアルログ、`espefuse summary` before/after、`試験記録書.md` 追記先）が作成済みであること | `製造・セキュア化手順書.md` 8.2 | 未記入 |
| PC-014 | **[重要][2026-04-04追加]** `7095` / `7099` / `7100` / `7101` の当日追記欄を開いた状態で開始し、どの試験へ何を記録するか説明できること | `試験記録書.md`, `試験仕様書.md` | 未記入 |

### 4.1 当日欄整理メモ（2026-04-04）
- [整理] 本メモは「開始前に何が確定済みで、何が未充足か」を固定するための補助欄である。表の `判定欄` 自体は、実機接続直前または開始時に最終記入する。
- `PC-012`:
  - 状態: **未充足**
  - build env 候補は `esp32s3_secure_final`。
  - ただし `7099` 未クローズ、`7095` 未開始、`007-0005` / `007-0007` 未判定のため、2026-04-04 セッションでは最終確定しない。
  - 次回は `試験記録書.md` `7095` の当日欄と同時に、採用理由を記入してから `OK/NG` を判定する。
- `PC-013`:
  - 状態: **未充足**
  - 記録先の親定義は整理済み。
  - `ProductionTool` 判定ログ: `C:\ProgramData\IoT\ProductionTool\logs\audit\`
  - ESP32 シリアルログ: `IoT/LocalServer/logs/` 配下の当日採取ファイル
  - `espefuse summary` before/after: `IoT/LocalServer/logs/efuse-readback/`
  - `試験記録書.md` 追記先: `7095` / `7099` / `7100` / `7101`
  - ただし、当日ディレクトリを作成済みかの実確認までは未実施であるため、開始判定はまだ `OK` にしない。
- `PC-014`:
  - 状態: **説明可能だが開始前最終確認待ち**
  - `7095`: 4点証跡の有無と `007` クローズ可否
  - `7099`: OTA2回、APモード10分、通常モード10分の安定性確認
  - `7100`: JTAG/シリアル安全確認
  - `7101`: 不可逆開始直前の `OTA` / `STA` / `AP` / `ProductionTool` 連携健全性確認
  - 記録先の対応は `製造・セキュア化手順書.md` 8.2 と一致している。
- [判定][2026-04-04] `PC-012` または `PC-013` が未充足のため、本日時点の `007-B` 進入判定は **No-Go** とする。

### 4.2 当日欄整理メモ（2026-04-04b）— ビルド・スタック修正後の状態
- [整理] 本メモは `esp32s3_secure_final` へのスタック＋ログ強化マージ後の状態を記録する。
- `PC-012`:
  - 状態: **確定済み**（`esp32s3_secure_final`）
  - 理由: 今回のスタック・ログ修正をすべて `esp32s3_secure_final` に適用済み。ビルド成功（`SUCCESS`, Flash 99.4%）。
  - 判定欄への記入条件: `7099` をクローズした後、書き込みに `esp32s3_secure_final` を使ったことをこの欄と `7095` 当日欄の両方へ記入する。
- `PC-013`:
  - 状態: **保留**（実ディレクトリ存在確認は次回接続時に実施）
  - `ProductionTool` 判定ログ: `C:\ProgramData\IoT\ProductionTool\logs\audit\`
  - ESP32 シリアルログ: `IoT/LocalServer/logs/` 配下の当日採取ファイル
  - `espefuse summary` before/after: `IoT/LocalServer/logs/efuse-readback/`
  - `試験記録書.md` 追記先: `7095` / `7099` / `7100` / `7101`
  - 次回: `dir C:\ProgramData\IoT\ProductionTool\logs\audit` と `efuse-readback` フォルダ存在を確認してから `OK` に変更する。
- `PC-014`:
  - 状態: **確認可能**（当日欄は `試験記録書.md` に整備済み。実施直前に全欄を開くことで充足）
  - 次回: `7099`/`7101`/`7095`/`7100` の各当日追記欄が開かれていることを確認してから `OK` にする。
- [判定][2026-04-04b] `PC-012` は確定済みだが `PC-013` 実確認が残るため、`007-B` 進入判定は引き続き **No-Go**。次回 `7099` 完了＋`PC-013` 実確認で `Go` に変更できる。

### 4.3 `007-B` No-Go解除準備メモ（2026-04-29）
- [重要] 本メモは非破壊の readback と証跡保存先確認であり、`stage_execute` または不可逆 eFuse 書込みの許可ではない。
- `PC-001`:
  - 状態: **一部確認済み**。
  - `COM4` で `MAC=80:b5:4e:f9:ce:04` を readback し、`デバイス管理表.md` の `DEV-002` と一致した。
  - 未了: AP 表示 `MAC=04:CE:F9:4E:B5:80`、`deviceName=IoT_04CEF94EB580`、`public_id` の当日照合。
- `PC-005`:
  - 状態: **非破壊 readback 済み**。
  - `get_security_info` で `Secure Boot: Disabled`、`Flash Encryption: Disabled`、`SPI_BOOT_CRYPT_CNT=0x0`、`BLOCK_KEY0/1/2=USER/EMPTY` を確認した。
- `PC-013`:
  - 状態: **保存先存在と before 証跡作成を確認済み**。
  - 証跡: `IoT/LocalServer/logs/efuse-readback/get-security-info-before-007B-COM4-20260429-1113.txt`、`IoT/LocalServer/logs/efuse-readback/espefuse-summary-before-007B-COM4-20260429-1113.txt`。
- `PC-014`:
  - 状態: **追記先確認済み、最終OKは未記入**。
  - `試験記録書.md` `7095` に `DEV-002` 用の No-Go解除準備ブロックを追記した。
- [判定][更新 2026-04-29] `PC-001` のAP側照合、`PC-002`〜`PC-004`、`PC-006`〜`PC-012`、`PC-014` の当日最終OKが未完了のため、`007-B` 本体は **No-Go 継続**。`7099` 同日再確認は下記 §4.4 で **OK** へ更新済み。

### 4.4 `007-A.1` / `7099` 同日再確認結果（2026-04-29）
- [重要] 本節は非不可逆の安定性確認結果であり、eFuse 書込み許可そのものではない。
- `7099` 同日再確認:
  - `7083(iterations=2)`: **OK**。証跡: `IoT/LocalServer/logs/test-reports/test7083-2026-04-29T02-22-02-137Z.json`
  - `STA 10分`: **OK**。証跡: `IoT/LocalServer/logs/test-reports/test7099-sta-2026-04-29T02-22-18Z.txt`
  - `AP 10分`: **OK**。証跡: `IoT/LocalServer/logs/test-reports/test7099-ap-2026-04-29T02-36-07Z.txt`
  - `STA復帰`: **OK**。証跡: `IoT/LocalServer/logs/test-reports/test7099-final-sta-return-2026-04-29T03-10-28Z.txt`
- [判定] `7099` 由来の No-Go は解除可。ただし `PC-001`〜`PC-014` の最終OK、`7095` 本番別表の段階別4点証跡開始、`ProductionTool` 実ランナー未実装に対する運用判断が未完了のため、`007-B` 本体は **No-Go 継続**。

### 4.5 `7041:pipeline:strict` / ProductionTool 連携確認結果（2026-04-29）
- [重要] 本節は `ProductionTool` / `LocalServer` / `SecretCore` / AP preflight の dry-run 系確認であり、eFuse 書込みの実施記録ではない。
- 実施結果:
  - 初回: **NG**。理由: AP後に STA 復帰済みで `192.168.4.1` 経路が無く、`BLOCKED_AP_REACHABILITY`。
  - 再実行: **OK**。`maintenance-reboot` で AP モードへ遷移し、`Wi-Fi_lab` を `AP-esp32lab-04CEF94EB580` へ接続してから `npm run test:7041:pipeline:strict` を再実行。
- 証跡:
  - 失敗レポート: `IoT/LocalServer/logs/test-reports/test7041-2026-04-29T03-15-29-641Z.json`
  - 成功レポート: `IoT/LocalServer/logs/test-reports/test7041-2026-04-29T03-17-02-730Z.json`
  - gate レポート: `IoT/LocalServer/logs/test-reports/test7041-gate-2026-04-29T03-17-02-998Z.json`
  - pipeline レポート: `IoT/LocalServer/logs/test-reports/test7041-pipeline-2026-04-29T03-17-03-023Z.json`
- 成功時の観測:
  - `gateStatus=FULL_COMPLETED`
  - workflow `state=completed` / `result=OK`
  - AP production state `precheck_collected`
  - `observedFirmwareVersion=1.1.0-beta.34`
  - `observedMac=04:CE:F9:4E:B5:80`
  - `measuredFreeHeapBytes=230084`（閾値 `50000` 以上）
  - `measuredMinStackMarginBytes=9996`（閾値 `4096` 以上）
- `7041` 後の復帰:
  - AP `POST /api/system/reboot` 後、`GET /api/devices` で `onlineState=online` / `detail=Reply` を確認。
  - `ping 172.17.1.200` は 2/2 成功。
- [準備判定] `PC-001`（AP表示MAC / deviceName 照合）、`PC-006`（管理者認証 / ProductionTool dry-run 経路）、`PC-007`（stack margin）、`PC-008`（free heap）、`PC-010`（AP preflight / 認証経路）は解除材料取得済み。
- [残条件] `PC-002` 電源安定、`PC-004` 鍵ID・署名素材ID・ロット・作業指示番号、`PC-011`/`PC-012` の最終運用判断、`PC-014` の当日最終OK記録、`7095` 本番別表の段階別4点証跡開始、`ProductionTool` 実ランナー未実装に対する運用判断は未完了。

### 4.6 `007-B` No-Go解除 最終直前確認（2026-04-29）
- [重要] 本節は `007-B` 進入判定の最終確認メモであり、不可逆コマンド実行の記録ではない。
- `PC-002`:
  - 状態: **OK扱い可**。
  - 根拠: 作業者確認で `DEV-002` は安定電源として確認済み。
- `PC-004`:
  - 状態: **未解除**。
  - 根拠: 作業者確認で「鍵ID・署名素材ID・ロット・作業指示番号」は未確認。
  - 非秘密の補助確認として、以下の鍵素材ファイルの存在のみ確認済み。
    - `C:\secure-work\keys\test\k-iot-secure-boot-test.pem`（存在、サイズ `2455` bytes）
    - `C:\secure-work\keys\test\k-iot-flash-encryption-test.bin`（存在、サイズ `32` bytes）
  - [厳守] ファイル存在は `PC-004` の十分条件ではない。秘密値を出さずに、鍵ID、署名素材ID、ロット、作業指示番号を作業記録上で照合してから `OK` とする。
- `ProductionTool` 実ランナー未実装への運用判断:
  - 状態: **未解除**。
  - [方針変更][2026-04-29 12:27] `007-B` は、`ProductionTool` の最終形実装（`precheck -> stage_execute -> readback -> evidence -> stability` を `ProductionTool` 管理下で閉じる実ランナー）を実装・検証した後に進める。
  - [廃止の方針] 既存実績手順を手動でなぞって不可逆本体へ入る運用は、今回の本番候補 `DEV-002` では採用しない。理由: `ProductionTool` を不可逆工程の責任主体にする設計思想を、実行責任と監査証跡まで一致させるため。
- `PC-004` 鍵方針:
  - 状態: **鍵新規発行なしで固定**。
  - [重要][2026-04-29 12:27] `k-iot-secure-boot` / `k-iot-flash-encryption` は当面、既存の試験用素材を本番1台目にも用いる。`020-0001`（本番専用分離）を実施しない限り、新規発行は行わない。
  - [厳守] `PC-004` で確認するのは新規発行ではなく、使用鍵ID・署名素材ID・ロット・作業指示番号の照合である。秘密値、raw key、秘密鍵本文は記録しない。
- `PC-011` / `PC-012`:
  - 状態: **OK扱い可**。
  - 根拠: 今回は NVS 暗号化なし方針を維持し、build env / 手順は `本番セキュア化出荷準備試験計画書.md` §6.4 と `007_本番1台目_実行順チェックリスト.md` の既存実績手順へ合わせる最終運用判断で確認済み。
- `PC-014` / `7095` 本番別表:
  - 状態: **OK扱い可**。
  - 根拠: `7095` 本番別表の4点証跡（`ProductionTool` ログ、ESP32シリアルログ、`espefuse summary` before/after、`試験記録書.md` 追記）を開始する前提で当日最終OK扱いとする確認済み。
- [最終判定][2026-04-29 12:27] `007-B` 本体は **No-Go 継続**。残条件は、`PC-004` の鍵ID・署名素材ID・ロット・作業指示番号照合と、`ProductionTool` 最終形実ランナーの実装・検証完了。鍵の新規発行は行わない。

## 5. 停止条件
- [厳守] 次のいずれかに該当した場合は、その場で安全停止し `006-0002` 以降へ進まない。
- `7085` が未達・進入不可のままである。
- `PC-001`〜`PC-011` の 1 項目でも `NG` または未記入である。
- `PC-012`〜`PC-014` の 1 項目でも `NG` または未記入である。
- 試験機リハーサル対象の版数、鍵ID、手順書、証跡保存先のいずれかが未確定である。
- `005-0008` を、`ProductionTool画面仕様書.md` **9.4.6** の突合や `試験記録書.md` の根拠なしに完了扱いにしようとしている。
- 読戻し検証、隔離判定、監査ログ保存の責務境界が文書と実装で一致していない。
- `7022` の非不可逆版または `006-0011` の試験機リハーサル結果が未整備で、再判定材料なしに先へ進もうとしている。
- **[致命的][2026-03-26追加]** `sdkconfig.defaults` に NVS Encryption 設定が含まれるが、対応する eFuse HMAC鍵投入が同一段階内に計画されていない（`問題点記録書 #0020` の再発防止条件）。
- **[致命的][2026-03-26追加]** 段階2(NVS) 完了後の AP HTTP 全ロール認証テストが未計画または `NG` のまま次段階へ進もうとしている。

## 6. 記録必須項目
- [厳守] 実施時は次を `試験記録書.md` または作業記録へ残す。
- 実施日時
- 対象機識別子
- 作業者ID
- 対象 `firmwareVersion`
- 使用した試験用鍵IDまたは本番用鍵IDの区分
- `minimumFreeHeapBytes` / 実測値
- `minimumStackMarginBytes` / 実測値
- `7085` 判定結果
- 中止理由または続行理由

## 7. 現段階の扱い
- [重要][2026-03-22] 本書はフェーズDの「準備完了」成果物であり、作業者が実際にチェックを書き込むのはフェーズFの試験機リハーサル以降とする。
- [厳守] 現時点では空欄のまま保持してよいが、項目定義・照合元・停止条件は変更管理なしで崩さない。

## 8. [致命的教訓][2026-03-26] sdkconfig/eFuse/FW の順序依存と段階間不整合の禁止

[重要] 本セクションは `問題点記録書 #0020` の解決結果に基づく。詳細は `本番セキュア化出荷準備試験計画書.md` 5.9 参照。

### 8.1 発生した問題

段階1(FE) 実行時に `sdkconfig.defaults` へ `CONFIG_NVS_ENCRYPTION=y` を追加したが、対応する eFuse HMAC鍵投入（EF-009b / 段階2(NVS)）を同時に実施しなかった。結果:
- FW は HMAC 鍵前提で NVS を開こうとし `nvs_open failed: NOT_FOUND`
- AP モードの認証パスワードがロードできず、AP HTTP API が認証不能
- `7041:pipeline:strict` が `BLOCKED_AP_REACHABILITY` で停止

### 8.2 確立された順序依存ルール

| 順序 | 作業 | タイミング |
|---|---|---|
| 1 | `sdkconfig.defaults` に NVS Encryption 関連設定を追加 | 段階2(NVS) の直前ビルドで |
| 2 | `secureNvsInit.cpp` のフォールバック実装を含む FW をビルド | 同上 |
| 3 | OTA またはシリアルで FW 反映 | eFuse 投入**前** |
| 4 | `espefuse burn_key BLOCK_KEY2 ... HMAC_UP`（EF-009b） | FW 反映**後** |
| 5 | 再起動 → NVS読書き確認 → AP HTTP 全ロール認証確認 | 投入**直後** |

[厳守] sdkconfig 変更と eFuse 投入は「同一段階内」で完結させること。段階間に sdkconfig だけ先行して eFuse 未投入の状態を**絶対に**作らない。

### 8.3 PlatformIO/Arduino 環境の注意

- `sdkconfig.defaults` に `CONFIG_NVS_ENCRYPTION=y` を記載しても、PlatformIO/Arduino ビルドでは C プリプロセッサマクロ `CONFIG_NVS_ENCRYPTION` が有効にならないケースがある
- `secureNvsInit.cpp` の `#ifdef CONFIG_NVS_ENCRYPTION` 分岐は実態として plaintext パスが常に実行される
- ESP-IDF ネイティブビルドへ移行する場合は、secure パスの再検証が必須

### 8.4 ProductionTool 自動化フローへの反映

[重要] 今回の知見を ProductionTool の自動化設計（`005-0008` / `006-0013`）へ以下のように反映する。

**段階実行の内部フロー（自動化時の順番チェック）**:

```
[段階1(FE)]
  precheck → FE鍵投入(EF-001) → FE有効化(EF-002〜006) → 暗号化バイナリ書込み
  → readback → reboot → 安定性確認（OTA×2 / AP / STA）
  → [go/no-go判定]

[段階2(NVS)] ★ sdkconfig/FW/eFuse の三位一体を同一段階内で完結
  [前提チェック]
    ☑ FWバージョンが secureNvsInit フォールバック対応版（beta.29以降）であること
    ☑ sdkconfig.defaults に CONFIG_NVS_ENCRYPTION=y が含まれていること
    ☑ EF-009b（BLOCK_KEY2 / HMAC_UP）が未投入であること（重複投入防止）
  precheck → HMAC鍵投入(EF-009b) → readback(KEY_PURPOSE_2=HMAC_UP, RD_DIS)
  → reboot → NVS読書き確認
  → AP HTTP 全ロール認証テスト（/api/health + /api/auth/login×4ロール） ★必須
  → 安定性確認（OTA×2 / AP / STA）
  → [go/no-go判定]

[段階3(SB)]
  precheck → SBダイジェスト投入(EF-007) → SB有効化(EF-008) → 未使用slot revoke(EF-009)
  → RD_DIS write-protect(EF-015) ★すべての burn-key 完了後
  → 署名済みバイナリ書込み → readback → reboot
  → 安定性確認（OTA×2 / AP / STA）
  → [go/no-go判定]

[段階4(封鎖)]
  precheck → JTAG無効化(EF-010/011) → ENABLE_SECURITY_DOWNLOAD(EF-014) ★最後
  → readback → 最終起動確認 → 安定性確認
  → [最終go/no-go判定]
```

**自動化時の必須チェック項目**:
1. 各段階の precheck で `espefuse summary` を自動取得し、前段階の eFuse 状態が期待通りであることを機械的に確認
2. 段階2(NVS) の precheck では `firmwareVersion` が `secureNvsInit` 対応版であることを API 経由で自動確認
3. 段階2(NVS) の完了判定には AP HTTP 全ロール認証テストを**必須**で含める（省略不可）
4. `RD_DIS` write-protect（EF-015）は段階3(SB) の **最後**で実行。段階2(NVS) の前に実行すると HMAC鍵の read-protect が設定できなくなる
5. `ENABLE_SECURITY_DOWNLOAD`（EF-014）は全段階の**最後の最後**で実行。以降 `espefuse` 追加書込み不可

[追記][2026-04-29] `007-0011` の文書再確認観点として、次の 5 点を毎回照合する。
1. `BLOCK_KEY0=Flash Encryption`、`BLOCK_KEY1=Secure Boot digest` の割当てが各文書で一致していること
2. `RD_DIS` write-protect は `SB` 段階末尾であり、旧 `NVS(HMAC)` 由来の手順が現行本番手順へ混入していないこと
3. `DIS_DOWNLOAD_MODE` は**未適用保持**、`ENABLE_SECURITY_DOWNLOAD` を代替として採用していること
4. `ENABLE_SECURITY_DOWNLOAD` は封鎖段階の**最後**にのみ実行すること
5. `DIS_USB_SERIAL_JTAG` は**未適用保持**であり、シリアルログ制御は FW 方針と一致していること

## 9. [重要][2026-04-04追加] 実機接続前の開始条件
- [厳守] 実機を物理接続する前に、次の 4 点を口頭確認する。
  1. 対象機は `DEV-001` であり、`MAC` / `COM` / `firmwareVersion` の照合先を開いている
  2. 使用 build env は `esp32s3_secure` か `esp32s3_secure_final` のどちらかに固定済みである
  3. 4点証跡の保存先を作成済みで、after 証跡の採取担当が決まっている
  4. `7095` / `7099` / `7100` / `7101` のどれへ記録するか説明できる
- [禁止] 上記 4 点のうち 1 点でも曖昧なまま接続し、その流れで不可逆コマンドを打たない。

### 9.1 [重要][2026-04-29追加] `007-0011` / `7099` 当日最短順
- [目的] `007-B` へ入る前に、文書確認と実機確認を同一日に閉じる。
- 1. 文書確認: `BLOCK_KEY0/1` 割当て、`RD_DIS` 順序、`DIS_DOWNLOAD_MODE` 未適用、`ENABLE_SECURITY_DOWNLOAD` 最終実行、`DIS_USB_SERIAL_JTAG` 未適用保持を照合する。
- 2. 接続確認: 対象機、`COM`、`firmwareVersion`、build env、4点証跡保存先、記録先試験IDを口頭確認する。
- 3. 実機確認: `STA` 通常モード、`AP` 到達性、`OTA` 継続更新可否を確認する。
- 4. 安定性確認: `7099` に従い `OTA x2`、`AP 10分`、`STA 10分` を観測する。
- 5. 停止条件: いずれかで異常、記録先未確定、証跡欠落がある場合は `007-B` へ進まない。

## 10. 変更履歴
- 2026-04-29 12:27: `007-B` の方針を「鍵新規発行なし」「不可逆本体は `ProductionTool` 最終形実ランナー実装・検証後」へ変更。理由: 鍵変更と実行主体変更を同時に動かさず、`ProductionTool` を責任主体とする設計思想を実行責任まで一致させるため。
- 2026-04-29: **`R-002`** を更新。当面は試験用 `k-iot` を本番にも流用し、本番専用未生成の前提をやめた。分離は **`020-0001`**。理由: `todo_old20260429.md` と整合
<!-- @import "[TOC]" {cmd="toc" depthFrom=1 depthTo=6 orderedList=false} -->
するため。
- 2026-04-04: `PC-012`〜`PC-014` と「実機接続前の開始条件」を追加。理由: build env 取り違え、証跡保存先未作成、記録先混乱のまま作業を始める事故を防ぎ、文鎮化防止を接続前から担保するため。
- 2026-03-26: `R-006`〜`R-008`（sdkconfig/eFuse同一段階完結、secureNvsInitフォールバック確認、PlatformIO/Arduino環境注意）を追加。`PC-009`〜`PC-011`（段階2前FW版数確認、段階2後AP全ロール認証、段階間不整合禁止）を追加。停止条件に2項目追加。セクション8（致命的教訓・順序依存ルール・ProductionTool自動化フロー）を新設。理由: `問題点記録書 #0020`（sdkconfig先行/eFuse未投入によるAP HTTP不能）の教訓を量産自動化・チェックリストに恒久的に反映するため。
- 2026-03-25: `R-005` と停止条件を追加し、`7022` の非不可逆版と `006-0011` の試験機リハーサル結果が揃わない限り先へ進まないことを明記した。理由: `006` の出口条件をチェックリスト側でも同じ基準で扱い、不可逆前の判定ブレを防ぐため。
- 2026-03-22: 新規作成。理由: `006-0001` として、第3段階 `eFuse` 適用前の確認項目を専用文書へ分離し、`003-0024` の進入判定ゲートと作業単位のチェックリストを混同しないようにするため。
