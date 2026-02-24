/**
 * @file certification.cpp
 * @brief 認証関連処理のひな形実装（機密は外部JSON想定）。
 */

#include "certification.h"

#include "log.h"

bool certificationService::initialize() {
  appLogInfo("certificationService initialized. (skeleton)");
  return true;
}

bool certificationService::authenticate() {
  // TODO: 機密情報は外部ファイルから読み込み、認証処理を実装する。
  appLogWarn("certificationService.authenticate is not implemented.");
  return false;
}
