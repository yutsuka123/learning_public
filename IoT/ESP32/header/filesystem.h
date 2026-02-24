/**
 * @file filesystem.h
 * @brief ファイルシステム処理のひな形（LittleFS想定、タスク化しない）。
 */

#pragma once

class filesystemService {
 public:
  bool initialize();
  bool readFile();
  bool writeFile();
};
