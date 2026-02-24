/**
 * @file filesystem.cpp
 * @brief ファイルシステム処理のひな形実装。
 */

#include "filesystem.h"

#include "log.h"

bool filesystemService::initialize() {
  // TODO: LittleFSの初期化処理を実装する。
  appLogInfo("filesystemService initialized. (skeleton)");
  return true;
}

bool filesystemService::readFile() {
  appLogWarn("filesystemService.readFile is not implemented.");
  return false;
}

bool filesystemService::writeFile() {
  appLogWarn("filesystemService.writeFile is not implemented.");
  return false;
}
