/**
 * @file exportKUserBackupFromSecretCore.mjs
 * @description SecretCore の export_k_user IPC を呼び出し、k-user のパスワード暗号化バックアップファイルを出力する。
 * @remarks
 * - [重要] raw k-user は一切表示しない。fingerprint のみ表示する。
 * - [厳守] 実行前に SecretCore が起動していること。
 * - [厳守] backupPassword は十分に長く推測困難な値を使用すること。
 * - [禁止] backupPassword をリポジトリやログへ保存しないこと。
 * - 変更日: 2026-03-15 新規作成。理由: Rust側で k-user バックアップを直接運用するため。
 *
 * 使用例:
 *   node scripts/exportKUserBackupFromSecretCore.mjs --password "YourStrongPassword123!"
 *   node scripts/exportKUserBackupFromSecretCore.mjs --password "YourStrongPassword123!" --out "data/k_user_backup_custom.enc.json"
 */

import net from "net";
import path from "path";

const pipeName = "\\\\.\\pipe\\iot-secret-core-ipc";

/**
 * @description SecretCore IPC へコマンドを送信する。
 * @param {string} command コマンド名。
 * @param {object} payload ペイロード。
 * @returns {Promise<any>} レスポンスデータ。
 */
function sendSecretCoreRequest(command, payload) {
  return new Promise((resolve, reject) => {
    const client = net.createConnection(pipeName, () => {
      client.write(JSON.stringify({ command, payload }));
    });
    let responseText = "";
    client.on("data", (chunk) => {
      responseText += chunk.toString();
      client.end();
    });
    client.on("end", () => {
      try {
        const parsed = JSON.parse(responseText);
        if (parsed.status === "ok") {
          resolve(parsed.data);
          return;
        }
        reject(new Error(parsed.error || "unknown ipc error"));
      } catch (parseError) {
        reject(new Error(`sendSecretCoreRequest parse failed. error=${parseError}`));
      }
    });
    client.on("error", (error) => {
      reject(new Error(`sendSecretCoreRequest connection failed. error=${error.message}`));
    });
  });
}

/**
 * @description コマンドライン引数を解析する。
 * @param {string[]} args 引数配列。
 * @returns {{password: string; outPath: string | undefined}} 解析結果。
 */
function parseArguments(args) {
  let password = "";
  /** @type {string | undefined} */
  let outPath;
  for (let index = 0; index < args.length; index += 1) {
    const currentArg = args[index];
    if (currentArg === "--password" && index + 1 < args.length) {
      password = args[index + 1];
      index += 1;
      continue;
    }
    if (currentArg === "--out" && index + 1 < args.length) {
      outPath = args[index + 1];
      index += 1;
      continue;
    }
  }
  return { password, outPath };
}

async function main() {
  const { password, outPath } = parseArguments(process.argv.slice(2));
  if (password.length === 0) {
    console.error("ERROR: --password が未指定です。");
    process.exit(1);
  }
  if (password.length < 12) {
    console.error("ERROR: --password は12文字以上を推奨します。");
    process.exit(1);
  }

  const payload = {
    backupPassword: password,
    backupFilePath: outPath ? path.resolve(process.cwd(), outPath) : undefined
  };
  const result = await sendSecretCoreRequest("export_k_user", payload);
  console.log("k-user バックアップ出力に成功しました。");
  console.log(`backupFilePath=${result.backupFilePath}`);
  console.log(`keyFingerprint=${result.keyFingerprint}`);
  console.log(`source=${result.source}`);
}

main().catch((error) => {
  console.error(`ERROR: exportKUserBackupFromSecretCore failed. error=${error.message}`);
  process.exit(1);
});

