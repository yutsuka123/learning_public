/**
 * @file secretCoreManager.ts
 * @description SecretCore 子プロセスの起動・停止と IPC セッション情報管理を担当する。
 * @remarks
 * - [重要] LocalServer 起動ごとにランダムな IPC セッション鍵を生成する。
 * - [厳守] SecretCore は LocalServer の子プロセスとして起動し、親子間だけで共有するセッション情報を持つ。
 * - [厳守] 起動時の接続情報は stdin/stdout のブートストラップで渡し、Pipe 名やセッション鍵を環境変数へ恒久露出させない。
 * - [禁止] 固定 Pipe 名・固定セッション鍵の恒久運用。
 * - 変更日: 2026-05-11 ブートストラップ情報を stdin/stdout に寄せた。理由: 008-0010 の IPC ハンドシェイクを子プロセス連携へ寄せるため。
 */
import { spawn, type ChildProcess } from "child_process";
import * as crypto from "crypto";
import * as os from "os";
import * as path from "path";

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
        SECRET_CORE_IPC_BOOTSTRAP_MODE: "stdin"
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

    this.sendBootstrapHandshake();
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

  /**
   * @description SecretCore へブートストラップ情報を stdin で送信する。
   * @remarks
   * - [重要] 起動時の Pipe 名と IPC セッション鍵は、この 1 回の handshake でのみ渡す。
   * - [厳守] 送信後は stdin を閉じ、以後の業務 IPC に使わない。
   */
  private sendBootstrapHandshake(): void {
    if (this.child === null) {
      console.error("sendBootstrapHandshake skipped. child process is unavailable.");
      return;
    }
    if (this.child.stdin === null) {
      console.error("sendBootstrapHandshake failed. child stdin is unavailable.");
      return;
    }

    const bootstrapEnvelope = {
      version: 1,
      pipeName: this.pipeName,
      ipcSessionKeyBase64: this.ipcSessionKeyBase64
    };
    const bootstrapText = `${JSON.stringify(bootstrapEnvelope)}\n`;

    this.child.stdin.write(bootstrapText, (writeError) => {
      if (writeError) {
        console.error(`sendBootstrapHandshake failed. reason=${writeError.message}`);
        return;
      }
      this.child?.stdin?.end();
    });
  }
}
