/**
 * @file secretCoreManager.ts
 * @description SecretCore 子プロセスの起動・停止と IPC セッション情報管理を担当する。
 * @remarks
 * - [重要] LocalServer 起動ごとにランダムな IPC セッション鍵を生成する。
 * - [厳守] SecretCore は LocalServer の子プロセスとして起動し、親子間だけで共有するセッション情報を持つ。
 * - [厳守] 起動時の接続情報は stdin/stdout のブートストラップで渡し、Pipe 名やセッション鍵を環境変数へ恒久露出させない。
 * - [禁止] 固定 Pipe 名・固定セッション鍵の恒久運用。
 * - [変更日 2026-05-11] ブートストラップ情報を stdin/stdout に寄せた。理由: 008-0010 の IPC ハンドシェイクを子プロセス連携へ寄せるため。
 * - [変更日 2026-05-17] stdout の **bootstrap ack**（1 行 JSON）を待ってから後続の health を叩く。理由: secret_core.exe 未更新時に Node が動的パイプへ接続し続け `iot-secret-core-ipc` 側と不一致になる起動失敗を早期に検知するため。さらに ACK 後に KeyManager 初期化で Pipe 作成が遅れる競合に備え health 待機は server 側で延長する。
 */
import { spawn, type ChildProcess } from "child_process";
import * as crypto from "crypto";
import * as os from "os";
import * as path from "path";

/** @description Rust 側 `IpcBootstrapAckEnvelope` に対応する最小形。 */
interface secretCoreBootstrapAck {
  version: number;
  status: string;
}

export class SecretCoreManager {
  private child: ChildProcess | null = null;
  private readonly exePath: string;
  private readonly pipeName: string;
  private readonly ipcSessionKeyBase64: string;
  private bootstrapAckPromise: Promise<void> | null = null;
  private resolveBootstrapAck: (() => void) | null = null;
  private rejectBootstrapAck: ((reason: Error) => void) | null = null;
  private bootstrapAckTimer: ReturnType<typeof setTimeout> | null = null;
  /** @description 1 行目の ack を処理済みか。false の間は stdout をバッファする。 */
  private bootstrapStdoutAckDone = false;
  /** @description ack 待ち前に子が死んだ場合の重複 reject 防止。 */
  private bootstrapAckSettled = false;

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

  /**
   * @description SecretCore 子プロセスを起動し、stdin ブートストラップを送信する。
   * @remarks `waitForBootstrapAck()` で ack を確認してから `checkHealth` を呼ぶこと。
   */
  public start(): void {
    if (this.child) {
      console.log("SecretCore is already running.");
      return;
    }

    this.bootstrapAckPromise = new Promise<void>((resolve, reject) => {
      this.resolveBootstrapAck = () => resolve();
      this.rejectBootstrapAck = reject;
    });
    this.bootstrapStdoutAckDone = false;
    this.bootstrapAckSettled = false;

    console.log(`Starting SecretCore from: ${this.exePath}`);
    this.child = spawn(this.exePath, [], {
      // [重要] stdin ブートストラップを成立させるため、子プロセス stdin を pipe で保持する。
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
      env: {
        ...process.env,
        SECRET_CORE_IPC_BOOTSTRAP_MODE: "stdin"
      }
    });

    let stdoutBuffer = "";

    const ackTimeoutMs = 15000;
    this.bootstrapAckTimer = setTimeout(() => {
      this.failBootstrapAck(
        new Error(
          `SecretCore bootstrap ack timeout after ${ackTimeoutMs}ms. ` +
            `対処: IoT/SecretCore で cargo build を実行し ${this.exePath} を更新してください（stdin ブートストラップ非対応の古い exe では、Node は動的パイプ名へ接続する一方、SecretCore は既定パイプのみ待受になり不一致となります）。`
        )
      );
    }, ackTimeoutMs);

    this.child.stdout?.on("data", (data: Buffer) => {
      const text = data.toString("utf8");
      if (!this.bootstrapStdoutAckDone) {
        stdoutBuffer += text;
        // 複数行が一度に来る場合、最初の非空行を ack とみなす
        while (!this.bootstrapStdoutAckDone) {
          const nl = stdoutBuffer.indexOf("\n");
          if (nl < 0) {
            return;
          }
          const line = stdoutBuffer.slice(0, nl).trim();
          stdoutBuffer = stdoutBuffer.slice(nl + 1);
          if (line.length === 0) {
            continue;
          }
          this.bootstrapStdoutAckDone = true;
          try {
            const ack = JSON.parse(line) as secretCoreBootstrapAck;
            if (ack.version === 1 && ack.status === "ok") {
              this.clearBootstrapAckTimer();
              if (this.resolveBootstrapAck) {
                this.resolveBootstrapAck();
                this.resolveBootstrapAck = null;
                this.rejectBootstrapAck = null;
                this.bootstrapAckSettled = true;
              }
            } else {
              this.failBootstrapAck(
                new Error(`SecretCore bootstrap ack invalid. version=${ack.version} status=${ack.status} line=${line}`)
              );
            }
          } catch (parseError) {
            const detail = parseError instanceof Error ? parseError.message : String(parseError);
            this.failBootstrapAck(new Error(`SecretCore bootstrap ack parse failed. line=${line} reason=${detail}`));
          }
          if (stdoutBuffer.length > 0) {
            console.log(`[SecretCore] ${stdoutBuffer.trimEnd()}`);
            stdoutBuffer = "";
          }
          return;
        }
      } else {
        console.log(`[SecretCore] ${text.trimEnd()}`);
      }
    });

    this.child.stderr?.on("data", (data) => {
      console.error(`[SecretCore ERR] ${data.toString().trim()}`);
    });

    this.child.on("close", (code) => {
      console.log(`SecretCore exited with code ${code}`);
      if (!this.bootstrapAckSettled) {
        this.failBootstrapAck(
          new Error(`SecretCore exited before bootstrap ack completed. exitCode=${code ?? "null"}`)
        );
      }
      this.child = null;
    });

    this.child.on("error", (err) => {
      console.error(`Failed to start SecretCore: ${err.message}`);
      this.failBootstrapAck(new Error(`Failed to start SecretCore. message=${err.message}`));
      this.child = null;
    });

    this.sendBootstrapHandshake();
  }

  /**
   * @description SecretCore が stdin 受領後に返す ack（stdout 1 行目）を待つ。
   * @returns Promise。ack 不達・不正・子プロセス先行終了時は reject。
   */
  public async waitForBootstrapAck(): Promise<void> {
    if (this.bootstrapAckPromise === null) {
      throw new Error("SecretCoreManager.waitForBootstrapAck failed. start() が未呼び出しです。");
    }
    await this.bootstrapAckPromise;
  }

  public stop(): void {
    this.clearBootstrapAckTimer();
    if (this.child) {
      console.log("Stopping SecretCore...");
      this.child.kill("SIGTERM");
      this.child = null;
    }
    this.bootstrapAckPromise = null;
    this.resolveBootstrapAck = null;
    this.rejectBootstrapAck = null;
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

  private clearBootstrapAckTimer(): void {
    if (this.bootstrapAckTimer !== null) {
      clearTimeout(this.bootstrapAckTimer);
      this.bootstrapAckTimer = null;
    }
  }

  /**
   * @description bootstrap の Promise を一度だけ reject する。
   * @param reason 失敗理由。
   */
  private failBootstrapAck(reason: Error): void {
    if (this.bootstrapAckSettled) {
      return;
    }
    this.bootstrapAckSettled = true;
    this.clearBootstrapAckTimer();
    if (this.rejectBootstrapAck) {
      this.rejectBootstrapAck(reason);
      this.rejectBootstrapAck = null;
      this.resolveBootstrapAck = null;
    }
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
      this.failBootstrapAck(new Error("sendBootstrapHandshake skipped. child process is unavailable."));
      return;
    }
    if (this.child.stdin === null) {
      console.error("sendBootstrapHandshake failed. child stdin is unavailable.");
      this.failBootstrapAck(new Error("sendBootstrapHandshake failed. child stdin is unavailable."));
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
        this.failBootstrapAck(
          new Error(`sendBootstrapHandshake failed. reason=${writeError.message} pipeHint=${this.pipeName}`)
        );
        return;
      }
      this.child?.stdin?.end();
    });
  }
}
