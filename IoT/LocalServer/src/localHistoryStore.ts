/**
 * @file localHistoryStore.ts
 * @description LocalServer 用 SQLite 履歴ストア。device status 通知と MQTT コマンド publish 履歴を保持する。
 * @remarks
 * - [重要] 保持日数は `0`（無期限・パージなし）または `1`〜`99999`（その日数より古い行を削除）。**実効値**は `settings.json` の `localHistoryRetentionDays`（初期既定 **30**・非機密、`settingsStore` で固定）。
 * - [厳守] raw 鍵・トークン・復号済み秘密を `detail` に含めない。呼び出し側はキー名に応じ `<password>` 等へ置換したメタのみを渡すこと。
 * - [禁止] `SecretCore` の秘密処理結果を本モジュールへ直接流し込まない。
 * - [重要][2026-05-17] 管理者 API からフィルタ付き平文エクスポート（JSON Lines）と `exportHistory` 記録に対応。理由: DB仕様書 3章（009-0002 第2段）。
 * - [重要][2026-05-17] 各履歴行に `fromId` / `toId`・`recordedAtJst`（`Asia/Tokyo` 併記）・デバイス `status` の `sub`（返信種別）等を保持する。理由: 運用で「誰から誰へ・いつ（JST）」を一意に辿れるようにするため。
 */

import fs from "fs";
import path from "path";
import Database from "better-sqlite3";
import type { statusMessage } from "./types";
import type { appConfig } from "./config";
import { formatRecordedAtJstFromIsoUtc } from "./historyTimestamps";
import { validateLocalHistoryRetentionDays } from "./localHistoryRetention";

/**
 * @description `commandHistory` に格納する1行分のパラメータ。
 */
export interface commandHistoryRecordInput {
  /** @description MQTT payload の `id`（リクエスト識別子）。 */
  requestId: string;
  /** @description MQTT の `op` に相当（call/set/get/network）。 */
  commandName: string;
  /** @description MQTT の `sub`。 */
  subCommand: string;
  /** @description 送信先 `DstID`。 */
  targetName: string;
  /** @description 送信元 `SrcID`（LocalServer の `sourceId`）。 */
  fromId: string;
  /** @description 送信先デバイス名（`targetName` と同一でよい）。 */
  toId: string;
  /** @description 成否・段階）。例: publish 成功なら `published`。 */
  result: string;
  /** @description 公開してよいメタ情報のみを JSON 文字列等で格納。 */
  detail: string;
  /** @description ISO8601（UTC）。 */
  recordedAt: string;
  /** @description `recordedAt` と同一瞬間の JST 表記（人間可読・末尾 `(JST)`）。 */
  recordedAtJst: string;
}

/**
 * @description 平文エクスポート対象テーブル。
 */
export type historyExportSource = "deviceStatus" | "command" | "serverEvent";

/**
 * @description `command` / `subCommand` / `deviceNo` フィルタの結合方式（DB仕様書 3章）。
 */
export type historyFieldCombineMode = "AND" | "OR";

/**
 * @description 履歴エクスポート用フィルタ（日時は両テーブル共通で AND）。
 */
export interface historyExportQuery {
  /** @description 出力に含めるテーブル。既定は両方。 */
  sources: ReadonlyArray<historyExportSource>;
  /** @description command 行のみ適用。deviceStatus は常に deviceNo＋期間で AND。 */
  fieldCombine: historyFieldCombineMode;
  /** @description 任意。`commandHistory.commandName` 完全一致。 */
  commandName?: string;
  /** @description 任意。`commandHistory.subCommand` 完全一致（NULL は空文字として比較）。 */
  subCommand?: string;
  /** @description 任意。`deviceStatus`: deviceName OR publicId。`commandHistory`: targetName。 */
  deviceNo?: string;
  /** @description 任意。`recordedAt` 以上（ISO8601）。 */
  fromAt?: string;
  /** @description 任意。`recordedAt` 以下（ISO8601）。 */
  toAt?: string;
}

/**
 * @description `deviceStatusHistory` SELECT 結果行。
 */
export interface deviceStatusHistoryRow {
  id: number;
  deviceName: string;
  publicId: string | null;
  macAddr: string | null;
  onlineState: string;
  firmwareVersion: string | null;
  fromId: string | null;
  toId: string | null;
  statusSub: string | null;
  messageId: string | null;
  detail: string | null;
  recordedAt: string;
  recordedAtJst: string | null;
}

/**
 * @description `commandHistory` SELECT 結果行。
 */
export interface commandHistoryRow {
  id: number;
  requestId: string;
  commandName: string;
  subCommand: string | null;
  targetName: string;
  fromId: string | null;
  toId: string | null;
  result: string;
  detail: string | null;
  recordedAt: string;
  recordedAtJst: string | null;
}

/**
 * @description `serverEventHistory` 1行（LocalServer 起動など）。
 */
export interface serverEventHistoryRow {
  id: number;
  eventType: string;
  localServerId: string;
  detail: string | null;
  recordedAt: string;
  recordedAtJst: string | null;
}

/**
 * @description `serverEventHistory` 追記入力。
 */
export interface serverEventHistoryRecordInput {
  eventType: string;
  localServerId: string;
  detail: string;
  recordedAt: string;
  recordedAtJst: string;
}

/**
 * @description `exportHistory` 追記用入力。
 */
export interface exportHistoryRecordInput {
  triggerType: "manual" | "scheduled";
  filterJson: string;
  exportPath: string;
  recordCount: number;
  executedBy: string;
  executedAt: string;
}
export class LocalHistoryStore {
  private retentionDays: number;
  private readonly dbFilePath: string;
  private db!: Database.Database;
  private insertDeviceStatusStmt!: Database.Statement;
  private insertCommandStmt!: Database.Statement;
  private deleteOldDeviceStatusStmt!: Database.Statement;
  private deleteOldCommandStmt!: Database.Statement;
  private deleteOldExportStmt!: Database.Statement;
  private insertExportStmt!: Database.Statement;
  private insertServerEventStmt!: Database.Statement;
  private deleteOldServerEventStmt!: Database.Statement;

  /**
   * @description コンストラクタ。DB ファイルおよびテーブルを初期化する。
   * @param config アプリ設定（DB パスを含む）。
   * @param initialRetentionDays 実効保持日数（`settings.json` 由来を推奨）。
   */
  public constructor(config: appConfig, initialRetentionDays: number) {
    validateLocalHistoryRetentionDays(initialRetentionDays);
    this.dbFilePath = config.localHistoryDbPath;
    this.retentionDays = initialRetentionDays;
    this.openConnectionAndPrepare();
  }

  /**
   * @description 保持日数を更新する（`settings.json` 更新後に server から呼ぶ）。
   * @param days `0` または `1`〜`99999`。
   * @returns なし。
   */
  public setRetentionDays(days: number): void {
    validateLocalHistoryRetentionDays(days);
    this.retentionDays = days;
  }

  /**
   * @description 現在の保持日数を返す。
   * @returns 保持日数。
   */
  public getRetentionDays(): number {
    return this.retentionDays;
  }

  /**
   * @description SQLite ファイル（本番・WAL・SHM）を削除し、空の DB へ再接続する。
   * @remarks
   * - [厳守] 管理者 API からのみ呼ぶこと。誤運用で証跡が失われる。
   * @returns なし。
   */
  public deleteDatabaseFilesAndReopen(): void {
    try {
      this.db.close();
    } catch {
      /* 再生成目的のため既に閉じていても続行 */
    }
    const sidecarSuffixes = ["-wal", "-shm"] as const;
    try {
      if (fs.existsSync(this.dbFilePath)) {
        fs.unlinkSync(this.dbFilePath);
      }
    } catch (unlinkError) {
      const messageText = unlinkError instanceof Error ? unlinkError.message : String(unlinkError);
      throw new Error(`deleteDatabaseFilesAndReopen failed. cannot remove main db file=${this.dbFilePath} reason=${messageText}`);
    }
    for (const suffix of sidecarSuffixes) {
      const sidecarPath = `${this.dbFilePath}${suffix}`;
      try {
        if (fs.existsSync(sidecarPath)) {
          fs.unlinkSync(sidecarPath);
        }
      } catch {
        /* WAL/SHM が無い環境もある */
      }
    }
    const dbDirectory = path.dirname(this.dbFilePath);
    if (!fs.existsSync(dbDirectory)) {
      fs.mkdirSync(dbDirectory, { recursive: true });
    }
    this.openConnectionAndPrepare();
  }

  /**
   * @description DB を開き直し、pragma・スキーマ・プリペアド文を再初期化する。
   * @returns なし。
   */
  private openConnectionAndPrepare(): void {
    const dbDirectory = path.dirname(this.dbFilePath);
    if (!fs.existsSync(dbDirectory)) {
      fs.mkdirSync(dbDirectory, { recursive: true });
    }
    this.db = new Database(this.dbFilePath);
    this.db.pragma("journal_mode = WAL");
    this.ensureSchema();
    this.migrateSchemaIfNeeded();
    this.prepareAllStatements();
  }

  /**
   * @description INSERT/DELETE 用プリペアド文を生成する。
   * @returns なし。
   */
  private prepareAllStatements(): void {
    this.insertDeviceStatusStmt = this.db.prepare(
      `INSERT INTO deviceStatusHistory (deviceName, publicId, macAddr, onlineState, firmwareVersion, fromId, toId, statusSub, messageId, detail, recordedAt, recordedAtJst)
       VALUES (@deviceName, @publicId, @macAddr, @onlineState, @firmwareVersion, @fromId, @toId, @statusSub, @messageId, @detail, @recordedAt, @recordedAtJst)`
    );
    this.insertCommandStmt = this.db.prepare(
      `INSERT INTO commandHistory (requestId, commandName, subCommand, targetName, fromId, toId, result, detail, recordedAt, recordedAtJst)
       VALUES (@requestId, @commandName, @subCommand, @targetName, @fromId, @toId, @result, @detail, @recordedAt, @recordedAtJst)`
    );
    const cutoffParam = "cutoff";
    this.deleteOldDeviceStatusStmt = this.db.prepare(
      `DELETE FROM deviceStatusHistory WHERE recordedAt < @${cutoffParam}`
    );
    this.deleteOldCommandStmt = this.db.prepare(`DELETE FROM commandHistory WHERE recordedAt < @${cutoffParam}`);
    this.deleteOldExportStmt = this.db.prepare(`DELETE FROM exportHistory WHERE executedAt < @${cutoffParam}`);
    this.deleteOldServerEventStmt = this.db.prepare(`DELETE FROM serverEventHistory WHERE recordedAt < @${cutoffParam}`);
    this.insertExportStmt = this.db.prepare(
      `INSERT INTO exportHistory (triggerType, filterJson, exportPath, recordCount, executedBy, executedAt)
       VALUES (@triggerType, @filterJson, @exportPath, @recordCount, @executedBy, @executedAt)`
    );
    this.insertServerEventStmt = this.db.prepare(
      `INSERT INTO serverEventHistory (eventType, localServerId, detail, recordedAt, recordedAtJst)
       VALUES (@eventType, @localServerId, @detail, @recordedAt, @recordedAtJst)`
    );
  }

  /**
   * @description 初期スキーマ（DB仕様書 4章）を作成する。
   * @returns なし。
   */
  private ensureSchema(): void {
    this.db.exec(`
      CREATE TABLE IF NOT EXISTS deviceStatusHistory (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        deviceName TEXT NOT NULL,
        publicId TEXT,
        macAddr TEXT,
        onlineState TEXT NOT NULL,
        firmwareVersion TEXT,
        fromId TEXT,
        toId TEXT,
        statusSub TEXT,
        messageId TEXT,
        detail TEXT,
        recordedAt TEXT NOT NULL,
        recordedAtJst TEXT
      );
      CREATE TABLE IF NOT EXISTS commandHistory (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        requestId TEXT NOT NULL,
        commandName TEXT NOT NULL,
        subCommand TEXT,
        targetName TEXT NOT NULL,
        fromId TEXT,
        toId TEXT,
        result TEXT NOT NULL,
        detail TEXT,
        recordedAt TEXT NOT NULL,
        recordedAtJst TEXT
      );
      CREATE TABLE IF NOT EXISTS exportHistory (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        triggerType TEXT NOT NULL,
        filterJson TEXT NOT NULL,
        exportPath TEXT NOT NULL,
        recordCount INTEGER NOT NULL,
        executedBy TEXT NOT NULL,
        executedAt TEXT NOT NULL
      );
      CREATE TABLE IF NOT EXISTS serverEventHistory (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        eventType TEXT NOT NULL,
        localServerId TEXT NOT NULL,
        detail TEXT,
        recordedAt TEXT NOT NULL,
        recordedAtJst TEXT
      );
      CREATE INDEX IF NOT EXISTS idx_deviceStatusHistory_recordedAt ON deviceStatusHistory(recordedAt);
      CREATE INDEX IF NOT EXISTS idx_commandHistory_recordedAt ON commandHistory(recordedAt);
      CREATE INDEX IF NOT EXISTS idx_exportHistory_executedAt ON exportHistory(executedAt);
      CREATE INDEX IF NOT EXISTS idx_serverEventHistory_recordedAt ON serverEventHistory(recordedAt);
    `);
  }

  /**
   * @description 既存 SQLite ファイルへ後追いカラム・テーブルを足す（互換維持）。
   * @returns なし。
   */
  private migrateSchemaIfNeeded(): void {
    const addColumn = (tableName: string, columnName: string, columnDdl: string): void => {
      const columns = this.db.pragma(`table_info(${tableName})`) as ReadonlyArray<{ name: string }>;
      if (columns.some((columnEntry) => columnEntry.name === columnName)) {
        return;
      }
      this.db.exec(`ALTER TABLE ${tableName} ADD COLUMN ${columnName} ${columnDdl}`);
    };

    addColumn("deviceStatusHistory", "firmwareVersion", "TEXT");
    addColumn("deviceStatusHistory", "fromId", "TEXT");
    addColumn("deviceStatusHistory", "toId", "TEXT");
    addColumn("deviceStatusHistory", "statusSub", "TEXT");
    addColumn("deviceStatusHistory", "messageId", "TEXT");
    addColumn("deviceStatusHistory", "recordedAtJst", "TEXT");

    addColumn("commandHistory", "fromId", "TEXT");
    addColumn("commandHistory", "toId", "TEXT");
    addColumn("commandHistory", "recordedAtJst", "TEXT");

    this.db.exec(`
      CREATE TABLE IF NOT EXISTS serverEventHistory (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        eventType TEXT NOT NULL,
        localServerId TEXT NOT NULL,
        detail TEXT,
        recordedAt TEXT NOT NULL,
        recordedAtJst TEXT
      );
      CREATE INDEX IF NOT EXISTS idx_serverEventHistory_recordedAt ON serverEventHistory(recordedAt);
    `);

    try {
      this.db.exec(`UPDATE deviceStatusHistory SET fromId = deviceName WHERE fromId IS NULL`);
    } catch {
      /* ベストエフォート */
    }
  }

  /**
   * @description status 通知を1件保存する。`fromId` はデバイス、`toId` は受信側（MQTT `DstID` または LocalServer の sourceId）。
   * @param status 正規化済み status。
   * @param localServerId 本プロセスの `config.sourceId`（`DstID` 欠落時の受信先表示用）。
   * @returns なし。
   */
  public recordDeviceStatus(status: statusMessage, localServerId: string): void {
    const toId = status.dstId.trim().length > 0 ? status.dstId.trim() : localServerId;
    const recordedAtJst = formatRecordedAtJstFromIsoUtc(status.receivedAt);
    this.insertDeviceStatusStmt.run({
      deviceName: status.srcId,
      publicId: status.publicId.length > 0 ? status.publicId : null,
      macAddr: status.macAddr.length > 0 ? status.macAddr : null,
      onlineState: status.onlineState,
      firmwareVersion: status.firmwareVersion.length > 0 ? status.firmwareVersion : null,
      fromId: status.srcId,
      toId,
      statusSub: status.statusSub.length > 0 ? status.statusSub : null,
      messageId: status.messageId.length > 0 ? status.messageId : null,
      detail: status.detail.length > 0 ? status.detail : null,
      recordedAt: status.receivedAt,
      recordedAtJst: recordedAtJst.length > 0 ? recordedAtJst : null
    });
  }

  /**
   * @description コマンド publish 済み履歴を1件保存する。
   * @param input コマンド行。
   * @returns なし。
   */
  public recordCommand(input: commandHistoryRecordInput): void {
    this.insertCommandStmt.run({
      requestId: input.requestId,
      commandName: input.commandName,
      subCommand: input.subCommand.length > 0 ? input.subCommand : null,
      targetName: input.targetName,
      fromId: input.fromId.length > 0 ? input.fromId : null,
      toId: input.toId.length > 0 ? input.toId : null,
      result: input.result,
      detail: input.detail.length > 0 ? input.detail : null,
      recordedAt: input.recordedAt,
      recordedAtJst: input.recordedAtJst.length > 0 ? input.recordedAtJst : null
    });
  }

  /**
   * @description LocalServer 自身のイベント（起動など）を1件保存する。
   * @param input 1行分。
   * @returns なし。
   */
  public recordServerEvent(input: serverEventHistoryRecordInput): void {
    this.insertServerEventStmt.run({
      eventType: input.eventType,
      localServerId: input.localServerId,
      detail: input.detail.length > 0 ? input.detail : null,
      recordedAt: input.recordedAt,
      recordedAtJst: input.recordedAtJst.length > 0 ? input.recordedAtJst : null
    });
  }

  /**
   * @description フィルタ条件に一致する `deviceStatusHistory` 行を取得する。
   * @param query エクスポート条件。
   * @returns 行配列（時刻・id 昇順）。
   */
  public queryDeviceStatusForExport(query: historyExportQuery): deviceStatusHistoryRow[] {
    const built = this.buildDeviceStatusExportQuery(query);
    return this.db.prepare(built.sql).all(built.params) as deviceStatusHistoryRow[];
  }

  /**
   * @description フィルタ条件に一致する `commandHistory` 行を取得する。
   * @param query エクスポート条件。
   * @returns 行配列（時刻・id 昇順）。
   */
  public queryCommandForExport(query: historyExportQuery): commandHistoryRow[] {
    const built = this.buildCommandExportQuery(query);
    return this.db.prepare(built.sql).all(built.params) as commandHistoryRow[];
  }

  /**
   * @description エクスポート実行を `exportHistory` に記録する。
   * @param input 1行分。
   * @returns なし。
   */
  public recordExportHistory(input: exportHistoryRecordInput): void {
    this.insertExportStmt.run({
      triggerType: input.triggerType,
      filterJson: input.filterJson,
      exportPath: input.exportPath,
      recordCount: input.recordCount,
      executedBy: input.executedBy,
      executedAt: input.executedAt
    });
  }

  /**
   * @description deviceStatus 向け WHERE 句とバインドを組み立てる。
   * @param query フィルタ。
   * @returns SQL とパラメータ。
   */
  private buildDeviceStatusExportQuery(query: historyExportQuery): {
    sql: string;
    params: Record<string, unknown>;
  } {
    const params: Record<string, unknown> = {};
    const whereParts: string[] = ["1=1"];
    const fromTrimmed = query.fromAt?.trim() ?? "";
    if (fromTrimmed.length > 0) {
      whereParts.push("recordedAt >= @fromAt");
      params.fromAt = fromTrimmed;
    }
    const toTrimmed = query.toAt?.trim() ?? "";
    if (toTrimmed.length > 0) {
      whereParts.push("recordedAt <= @toAt");
      params.toAt = toTrimmed;
    }
    const deviceTrimmed = query.deviceNo?.trim() ?? "";
    if (deviceTrimmed.length > 0) {
      whereParts.push("(deviceName = @deviceNo OR publicId = @deviceNo)");
      params.deviceNo = deviceTrimmed;
    }
    const sql = `SELECT id, deviceName, publicId, macAddr, onlineState, firmwareVersion, fromId, toId, statusSub, messageId, detail, recordedAt, recordedAtJst FROM deviceStatusHistory WHERE ${whereParts.join(
      " AND "
    )} ORDER BY recordedAt ASC, id ASC`;
    return { sql, params };
  }

  /**
   * @description commandHistory 向け WHERE 句とバインドを組み立てる。
   * @param query フィルタ。
   * @returns SQL とパラメータ。
   */
  private buildCommandExportQuery(query: historyExportQuery): {
    sql: string;
    params: Record<string, unknown>;
  } {
    const params: Record<string, unknown> = {};
    const whereParts: string[] = ["1=1"];
    const fromTrimmed = query.fromAt?.trim() ?? "";
    if (fromTrimmed.length > 0) {
      whereParts.push("recordedAt >= @fromAt");
      params.fromAt = fromTrimmed;
    }
    const toTrimmed = query.toAt?.trim() ?? "";
    if (toTrimmed.length > 0) {
      whereParts.push("recordedAt <= @toAt");
      params.toAt = toTrimmed;
    }

    const cmdTrimmed = query.commandName?.trim() ?? "";
    const subTrimmed = query.subCommand?.trim() ?? "";
    const devTrimmed = query.deviceNo?.trim() ?? "";
    const fieldConditions: string[] = [];
    if (cmdTrimmed.length > 0) {
      fieldConditions.push("commandName = @commandName");
      params.commandName = cmdTrimmed;
    }
    if (subTrimmed.length > 0) {
      fieldConditions.push("COALESCE(subCommand, '') = @subCommand");
      params.subCommand = subTrimmed;
    }
    if (devTrimmed.length > 0) {
      fieldConditions.push("targetName = @targetName");
      params.targetName = devTrimmed;
    }

    if (fieldConditions.length > 0) {
      if (query.fieldCombine === "OR") {
        whereParts.push(`(${fieldConditions.join(" OR ")})`);
      } else {
        for (const conditionText of fieldConditions) {
          whereParts.push(`(${conditionText})`);
        }
      }
    }

    const sql = `SELECT id, requestId, commandName, subCommand, targetName, fromId, toId, result, detail, recordedAt, recordedAtJst FROM commandHistory WHERE ${whereParts.join(
      " AND "
    )} ORDER BY recordedAt ASC, id ASC`;
    return { sql, params };
  }

  /**
   * @description `serverEventHistory` 向け WHERE 句とバインドを組み立てる。
   * @param query フィルタ（期間のみ・`deviceNo` は無視）。
   * @returns SQL とパラメータ。
   */
  private buildServerEventExportQuery(query: historyExportQuery): {
    sql: string;
    params: Record<string, unknown>;
  } {
    const params: Record<string, unknown> = {};
    const whereParts: string[] = ["1=1"];
    const fromTrimmed = query.fromAt?.trim() ?? "";
    if (fromTrimmed.length > 0) {
      whereParts.push("recordedAt >= @fromAt");
      params.fromAt = fromTrimmed;
    }
    const toTrimmed = query.toAt?.trim() ?? "";
    if (toTrimmed.length > 0) {
      whereParts.push("recordedAt <= @toAt");
      params.toAt = toTrimmed;
    }
    const sql = `SELECT id, eventType, localServerId, detail, recordedAt, recordedAtJst FROM serverEventHistory WHERE ${whereParts.join(
      " AND "
    )} ORDER BY recordedAt ASC, id ASC`;
    return { sql, params };
  }

  /**
   * @description フィルタ条件に一致する `serverEventHistory` 行を取得する。
   * @param query エクスポート条件。
   * @returns 行配列。
   */
  public queryServerEventForExport(query: historyExportQuery): serverEventHistoryRow[] {
    const built = this.buildServerEventExportQuery(query);
    return this.db.prepare(built.sql).all(built.params) as serverEventHistoryRow[];
  }

  /**
   * @description 保持日数を超えた行を削除する。`retentionDays=0` のときは何もしない。
   * @returns なし。
   */
  public purgeExpired(): void {
    if (this.retentionDays <= 0) {
      return;
    }
    const cutoffMillis = Date.now() - this.retentionDays * 24 * 60 * 60 * 1000;
    const cutoffIso = new Date(cutoffMillis).toISOString();
    const payload = { cutoff: cutoffIso };
    const deviceDeleted = this.deleteOldDeviceStatusStmt.run(payload).changes;
    const commandDeleted = this.deleteOldCommandStmt.run(payload).changes;
    const exportDeleted = this.deleteOldExportStmt.run(payload).changes;
    const serverEventDeleted = this.deleteOldServerEventStmt.run(payload).changes;
    if (deviceDeleted + commandDeleted + exportDeleted + serverEventDeleted > 0) {
      console.info(
        `localHistoryStore: purgeExpired completed. retentionDays=${this.retentionDays} cutoff=${cutoffIso} deviceRows=${deviceDeleted} commandRows=${commandDeleted} exportRows=${exportDeleted} serverEventRows=${serverEventDeleted}`
      );
    }
  }

  /**
   * @description DB 接続を閉じる（主にテスト・Graceful shutdown 用）。
   * @returns なし。
   */
  public close(): void {
    this.db.close();
  }
}
