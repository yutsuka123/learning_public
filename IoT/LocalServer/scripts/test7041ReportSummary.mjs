/**
 * @file test7041ReportSummary.mjs
 * @description 7041 レポートの直近比較サマリを生成するユーティリティ。
 *
 * 主仕様:
 * - `logs/test-reports` から `test7041-*.json` を新しい順に取得する。
 * - 直近2件（または指定件数）の `failureCategory` / `failureRecommendation` / `diagnosis` を比較する。
 * - 比較結果を標準出力し、`test7041-summary-*.json` を保存する。
 *
 * [重要] AP未接続期間でも、失敗レポート差分の比較を継続できるようにする。
 * [厳守] 比較対象は `test7041-summary-*.json` を除外し、実行レポートのみとする。
 * [制限事項] `test7041-*.json` が2件未満の場合は差分判定を行わず、存在分のみを出力する。
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createTimestampText } from "./testCommon.mjs";

const scriptDirectoryPath = path.dirname(fileURLToPath(import.meta.url));
const projectRootPath = path.resolve(scriptDirectoryPath, "..");
const reportsDirectoryPath = path.join(projectRootPath, "logs", "test-reports");

/**
 * @param {string[]} argv
 * @returns {{ count: number }}
 */
function parseArguments(argv) {
  const count = readNumberOption(argv, "--count", 2);
  return { count };
}

/**
 * @param {string[]} argv
 * @param {string} optionName
 * @param {number} defaultValue
 * @returns {number}
 */
function readNumberOption(argv, optionName, defaultValue) {
  const optionIndex = argv.indexOf(optionName);
  if (optionIndex < 0 || !argv[optionIndex + 1]) {
    return defaultValue;
  }
  const parsedValue = Number(argv[optionIndex + 1]);
  if (!Number.isFinite(parsedValue) || parsedValue <= 0) {
    throw new Error(`readNumberOption failed. optionName=${optionName} rawValue=${String(argv[optionIndex + 1])}`);
  }
  return Math.floor(parsedValue);
}

/**
 * @returns {Array<{ filePath: string; fileName: string; mtimeMs: number }>}
 */
function list7041ReportFiles() {
  if (!fs.existsSync(reportsDirectoryPath)) {
    return [];
  }
  const allFileNameList = fs.readdirSync(reportsDirectoryPath, "utf-8");
  const executionReportPattern = /^test7041-\d{4}-\d{2}-\d{2}T/;
  return allFileNameList
    .filter((fileName) => executionReportPattern.test(fileName) && fileName.endsWith(".json"))
    .map((fileName) => {
      const filePath = path.join(reportsDirectoryPath, fileName);
      const stats = fs.statSync(filePath);
      return {
        filePath,
        fileName,
        mtimeMs: stats.mtimeMs
      };
    })
    .sort((leftItem, rightItem) => rightItem.mtimeMs - leftItem.mtimeMs);
}

/**
 * @param {string} filePath
 * @returns {any}
 */
function readJsonFile(filePath) {
  const fileText = fs.readFileSync(filePath, "utf-8");
  return JSON.parse(fileText);
}

/**
 * @param {any} latestReport
 * @param {any | null} previousReport
 * @returns {Record<string, { latest: string; previous: string; changed: boolean }>}
 */
function buildComparisonMap(latestReport, previousReport) {
  const previousValue = previousReport ?? {};
  const flatten = (reportObject, keyPath, defaultValue = "") => String(keyPath.split(".").reduce((acc, key) => acc?.[key], reportObject) ?? defaultValue);
  const keyPathList = [
    "result",
    "mode",
    "failureCategory",
    "failureRecommendation",
    "diagnosis.targetHost",
    "diagnosis.localIpv4",
    "diagnosis.routeHint",
    "diagnosis.arpHint",
    "diagnosis.nextAction"
  ];
  const comparisonMap = {};
  for (const keyPath of keyPathList) {
    const latestText = flatten(latestReport, keyPath, "");
    const previousText = flatten(previousValue, keyPath, "");
    comparisonMap[keyPath] = {
      latest: latestText,
      previous: previousText,
      changed: latestText !== previousText
    };
  }
  return comparisonMap;
}

/**
 * @param {any} summaryObject
 * @returns {string}
 */
function writeSummaryReport(summaryObject) {
  if (!fs.existsSync(reportsDirectoryPath)) {
    fs.mkdirSync(reportsDirectoryPath, { recursive: true });
  }
  const fileName = `test7041-summary-${createTimestampText()}.json`;
  const filePath = path.join(reportsDirectoryPath, fileName);
  fs.writeFileSync(filePath, `${JSON.stringify(summaryObject, null, 2)}\n`, "utf-8");
  return filePath;
}

/**
 * @param {Record<string, { latest: string; previous: string; changed: boolean }>} comparisonMap
 * @returns {void}
 */
function printComparison(comparisonMap) {
  for (const [keyPath, valueObject] of Object.entries(comparisonMap)) {
    const mark = valueObject.changed ? "CHANGED" : "SAME";
    console.log(`[7041-summary] ${mark} ${keyPath}`);
    console.log(`  latest  : ${valueObject.latest}`);
    console.log(`  previous: ${valueObject.previous}`);
  }
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const reportFileList = list7041ReportFiles().slice(0, options.count);
  if (reportFileList.length === 0) {
    throw new Error("main failed. no test7041 report files found.");
  }
  const reportObjectList = reportFileList.map((fileItem) => ({
    ...fileItem,
    report: readJsonFile(fileItem.filePath)
  }));
  const latestItem = reportObjectList[0];
  const previousItem = reportObjectList.length >= 2 ? reportObjectList[1] : null;
  const comparisonMap = buildComparisonMap(latestItem.report, previousItem?.report ?? null);
  const summaryObject = {
    testId: "7041-summary",
    generatedAt: new Date().toISOString(),
    sourceReportCount: reportObjectList.length,
    latestFileName: latestItem.fileName,
    previousFileName: previousItem?.fileName ?? "",
    comparison: comparisonMap
  };
  const summaryReportFilePath = writeSummaryReport(summaryObject);
  console.log(`[7041-summary] latest=${latestItem.fileName} previous=${previousItem?.fileName ?? "(none)"}`);
  printComparison(comparisonMap);
  console.log(`[7041-summary] summaryReportFilePath=${summaryReportFilePath}`);
}

main().catch((error) => {
  console.error(`[7041-summary] failed: ${String(error?.message ?? error)}`);
  process.exitCode = 1;
});
