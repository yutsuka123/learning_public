/**
 * @file settingsStore.ts
 * @description LocalServerの永続設定（時刻表示/OTAファームウェア設定）をJSONで管理する。
 * @remarks
 * - [重要] 設定は `data/settings.json` へ保存し、再起動後も維持する。
 * - [厳守] 設定更新時は入力を検証し、不正値で既存設定を破壊しない。
 * - [将来対応] 設定項目増加に備えて、部分更新(merge)を基本とする。
 */

import fs from "fs";
import path from "path";
import { appConfig } from "./config";
import { localServerSettings, firmwareSourceType } from "./types";

/**
 * @description 永続設定ストア。
 */
export class SettingsStore {
  private readonly settingsFilePath: string;
  private currentSettings: localServerSettings;

  /**
   * @description コンストラクタ。
   * @param config 起動設定。
   */
  public constructor(config: appConfig) {
    const dataDirectoryPath = path.resolve(process.cwd(), "data");
    this.settingsFilePath = path.join(dataDirectoryPath, "settings.json");
    this.currentSettings = this.loadOrCreateDefaultSettings(config);
  }

  /**
   * @description 現在設定を返す。
   * @returns 設定。
   */
  public getSettings(): localServerSettings {
    return { ...this.currentSettings };
  }

  /**
   * @description 設定を部分更新して保存する。
   * @param partialSettings 更新項目。
   * @returns 更新後設定。
   */
  public updateSettings(partialSettings: Partial<localServerSettings>): localServerSettings {
    const mergedSettings: localServerSettings = {
      ...this.currentSettings,
      ...partialSettings
    };
    this.validateSettings(mergedSettings, "updateSettings");
    this.writeSettings(mergedSettings);
    this.currentSettings = mergedSettings;
    return this.getSettings();
  }

  /**
   * @description アップロード済みファームウェアファイル名を保存する。
   * @param fileName 保存するファイル名。
   * @returns 更新後設定。
   */
  public setUploadedFirmwareFileName(fileName: string): localServerSettings {
    return this.updateSettings({
      firmwareUploadedFileName: fileName,
      firmwareSource: "uploadedFile"
    });
  }

  /**
   * @description 設定から有効なOTAファームウェアパスを解決する。
   * @returns 絶対パス。
   */
  public resolveActiveFirmwarePath(): string {
    if (this.currentSettings.firmwareSource === "uploadedFile") {
      if (this.currentSettings.firmwareUploadedFileName.length <= 0) {
        throw new Error("resolveActiveFirmwarePath failed. firmwareUploadedFileName is empty.");
      }
      return path.resolve(process.cwd(), "uploads", this.currentSettings.firmwareUploadedFileName);
    }
    return path.isAbsolute(this.currentSettings.firmwareLocalPath)
      ? this.currentSettings.firmwareLocalPath
      : path.resolve(process.cwd(), this.currentSettings.firmwareLocalPath);
  }

  /**
   * @description 設定ファイル初期化/読込を行う。
   * @param config 起動設定。
   * @returns 読み込んだ設定。
   */
  private loadOrCreateDefaultSettings(config: appConfig): localServerSettings {
    const defaultSettings: localServerSettings = {
      timeZone: Intl.DateTimeFormat().resolvedOptions().timeZone || "Asia/Tokyo",
      firmwareSource: "localPath",
      firmwareLocalPath: config.otaFirmwarePath,
      firmwareUploadedFileName: "",
      otaFirmwareVersion: config.otaFirmwareVersion,
      wifiUsbInterfaceName: config.wifiUsbInterfaceName
    };

    const settingsDirectoryPath = path.dirname(this.settingsFilePath);
    if (!fs.existsSync(settingsDirectoryPath)) {
      fs.mkdirSync(settingsDirectoryPath, { recursive: true });
    }

    if (!fs.existsSync(this.settingsFilePath)) {
      this.writeSettings(defaultSettings);
      return defaultSettings;
    }

    const fileText = fs.readFileSync(this.settingsFilePath, "utf-8");
    const parsedJson = JSON.parse(fileText) as Partial<localServerSettings>;
    const mergedSettings: localServerSettings = {
      ...defaultSettings,
      ...parsedJson
    };
    this.validateSettings(mergedSettings, "loadOrCreateDefaultSettings");
    this.writeSettings(mergedSettings);
    return mergedSettings;
  }

  /**
   * @description 設定値の妥当性を検証する。
   * @param settings 検証対象設定。
   * @param functionName 呼び出し元関数名。
   */
  private validateSettings(settings: localServerSettings, functionName: string): void {
    if (settings.timeZone.length <= 0) {
      throw new Error(`${functionName} failed. timeZone is empty.`);
    }
    try {
      new Intl.DateTimeFormat("ja-JP", { timeZone: settings.timeZone }).format(new Date());
    } catch (validationError) {
      throw new Error(`${functionName} failed. invalid timeZone=${settings.timeZone} reason=${String(validationError)}`);
    }

    if (settings.otaFirmwareVersion.length <= 0) {
      throw new Error(`${functionName} failed. otaFirmwareVersion is empty.`);
    }

    this.validateFirmwareSource(settings.firmwareSource, functionName);
    if (settings.firmwareSource === "localPath" && settings.firmwareLocalPath.length <= 0) {
      throw new Error(`${functionName} failed. firmwareLocalPath is empty for localPath mode.`);
    }
    if (settings.firmwareSource === "uploadedFile" && settings.firmwareUploadedFileName.length <= 0) {
      throw new Error(`${functionName} failed. firmwareUploadedFileName is empty for uploadedFile mode.`);
    }
  }

  /**
   * @description ファームウェアソース種別を検証する。
   * @param firmwareSource ソース種別。
   * @param functionName 呼び出し元関数名。
   */
  private validateFirmwareSource(firmwareSource: firmwareSourceType, functionName: string): void {
    if (firmwareSource !== "localPath" && firmwareSource !== "uploadedFile") {
      throw new Error(`${functionName} failed. invalid firmwareSource=${firmwareSource}`);
    }
  }

  /**
   * @description 設定をJSONへ保存する。
   * @param settings 保存対象設定。
   */
  private writeSettings(settings: localServerSettings): void {
    fs.writeFileSync(this.settingsFilePath, `${JSON.stringify(settings, null, 2)}\n`, "utf-8");
  }
}
