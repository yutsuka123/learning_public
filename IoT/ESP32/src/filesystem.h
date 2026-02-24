/**
 * @file filesystem.h
 * @brief ファイルシステム処理のひな形（LittleFS想定、タスク化しない）。
 */

#pragma once

class filesystemService {
 public:
  /**
   * @brief ファイルシステムを初期化する。
   * @return 初期化成功時true。
   */
  bool initialize();

  /**
   * @brief 読み込み処理のプレースホルダ。
   * @return 成功時true。
   */
  bool readFile();

  /**
   * @brief 書き込み処理のプレースホルダ。
   * @return 成功時true။
   */
  bool writeFile();
};
