/**
 * @file secretCoreManager.ts
 * @description SecretCore 子プロセスの起動・停止と IPC セッション情報管理を担当する。
 * @remarks
 * - [重要] LocalServer 起動ごとにランダムな IPC セッション鍵を生成する。
 * - [厳守] SecretCore は LocalServer の子プロセスとして起動し、親子間だけで共有するセッション情報を持つ。
 * - [禁止] 固定 Pipe 名・固定セッション鍵の恒久運用。
 * - 変更日: 2026-03-15 IPC 認可・改ざん防止・リプレイ防止の基盤を追加。理由: 003-0014 対応のため。
 */
import { spawn, ChildProcess } from "child_process";
import crypto from "crypto";
import os from "os";
import path from "path";

export class SecretCoreManager {
  private child: ChildProcess | null = null;
  private readonly exePath: string;
  private readonly pipeName: string;
  private readonly ipcSessionKeyBase64: string;

  constructor() {
    // 開発時は target/debug を参照、本番はルート配置の前提
    const isDev = process.env.NODE_ENV !== "production";
    if (isDev) {
      this.exePath = path.resolve(__dirname, "../../SecretCore/target/debug/secret_core.exe");
    } else {
      this.exePath = path.resolve(__dirname, "../../SecretCore/target/release/secret_core.exe");
    }
    const machineId = os.hostname().replace(/[^a-zA-Z0-9_-]/g, "_");
    const sessionId = crypto.randomBytes(8).toString("hex");
    this.pipeName = `\\\\.\\pipe\\iot-secret-core-${machineId}-${sessionId}`;
    this.ipcSessionKeyBase64 = crypto.randomBytes(32).toString("base64");
  }

  public start(): void {
    if (this.child) {
      console.log("SecretCore is already running.");
      return;
    }

    console.log(`Starting SecretCore from: ${this.exePath}`);
    this.child = spawn(this.exePath, [], {
      stdio: ["ignore", "pipe", "pipe"],
      windowsHide: true,
      env: {
        ...process.env,
        SECRET_CORE_PIPE_NAME: this.pipeName,
        SECRET_CORE_IPC_SESSION_KEY_B64: this.ipcSessionKeyBase64
      }
    });

    this.child.stdout?.on("data", (data) => {
      console.log(`[SecretCore] ${data.toString().trim()}`);
    });

    this.child.stderr?.on("data", (data) => {
      console.error(`[SecretCore ERR] ${data.toString().trim()}`);
    });

    this.child.on("close", (code) => {
      console.log(`SecretCore exited with code ${code}`);
      this.child = null;
    });

    this.child.on("error", (err) => {
      console.error(`Failed to start SecretCore: ${err.message}`);
      this.child = null;
    });
  }

  public stop(): void {
    if (this.child) {
      console.log("Stopping SecretCore...");
      this.child.kill("SIGTERM");
      this.child = null;
    }
  }

  /**
   * @description SecretCore 接続先 Pipe 名を返す。
   * @returns Pipe 名。
   */
  public getPipeName(): string {
    return this.pipeName;
  }

  /**
   * @description 現在の IPC セッション鍵（Base64）を返す。
   * @returns Base64 文字列。
   */
  public getIpcSessionKeyBase64(): string {
    return this.ipcSessionKeyBase64;
  }
}
