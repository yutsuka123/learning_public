/**
 * @file importKUserBackupToSecretCore.mjs
 * @description SecretCore の import_k_user_backup IPC を呼び出し、パスワード暗号化バックアップファイルから k-user を復元する。
 * @remarks
 * - [重要] 復元後の k-user は SecretCore 内部でのみ保持し、raw 値は表示しない。
 * - [厳守] 実行前に SecretCore が起動していること。
 * - [厳守] --password と --in を正しく指定すること。
 * - [禁止] バックアップ復元のために平文 k-user ファイルを作成しないこと。
 * - 変更日: 2026-03-15 新規作成。理由: バックアップ受け渡しを Rust側で完結させるため。
 *
 * 使用例:
 *   node scripts/importKUserBackupToSecretCore.mjs --password "YourStrongPassword123!" --in "data/k_user_backup.enc.json"
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
 * @returns {{password: string; inPath: string | undefined}} 解析結果。
 */
function parseArguments(args) {
  let password = "";
  /** @type {string | undefined} */
  let inPath;
  for (let index = 0; index < args.length; index += 1) {
    const currentArg = args[index];
    if (currentArg === "--password" && index + 1 < args.length) {
      password = args[index + 1];
      index += 1;
      continue;
    }
    if (currentArg === "--in" && index + 1 < args.length) {
      inPath = args[index + 1];
      index += 1;
      continue;
    }
  }
  return { password, inPath };
}

async function main() {
  const { password, inPath } = parseArguments(process.argv.slice(2));
  if (password.length === 0) {
    console.error("ERROR: --password が未指定です。");
    process.exit(1);
  }
  if (!inPath || inPath.length === 0) {
    console.error("ERROR: --in が未指定です。");
    process.exit(1);
  }

  const payload = {
    backupPassword: password,
    backupFilePath: path.resolve(process.cwd(), inPath)
  };
  const result = await sendSecretCoreRequest("import_k_user_backup", payload);
  console.log("k-user バックアップ復元に成功しました。");
  console.log(`keyFingerprint=${result.keyFingerprint}`);
  console.log(`source=${result.source}`);
}

main().catch((error) => {
  console.error(`ERROR: importKUserBackupToSecretCore failed. error=${error.message}`);
  process.exit(1);
});

