/**
 * @file certification.h
 * @brief 認証関連処理のひな形（タスク化しない）。
 */

#pragma once

class certificationService {
 public:
  /**
   * @brief 認証モジュールを初期化する。
   * @return 初期化成功時true。
   */
  bool initialize();

  /**
   * @brief 認証処理のプレースホルダ関数。
   * @return 認証成功時true。
   */
  bool authenticate();
};
