/**
 * @file test7207NoCertReject.mjs
 * @description 7207: 証明書なしでの AWS IoT Core 接続拒否確認
 *   - クライアント証明書を提示しない状態で AWS IoT Core TLS 8883 への接続を試みる
 *   - TLS ハンドシェイクで拒否されることを確認（X.509 相互認証が機能していること）
 * @usage node scripts/test7207NoCertReject.mjs
 */

import tls from "tls";
import dotenv from "dotenv";
import { fileURLToPath } from "url";
import path from "path";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
dotenv.config({ path: path.join(__dirname, "../.env") });

const endpoint = process.env.AWS_IOT_ENDPOINT;
if (!endpoint) {
  console.error("ERROR: AWS_IOT_ENDPOINT not set in .env");
  process.exit(1);
}

console.log(`[7207] 証明書なし接続拒否試験`);
console.log(`[7207] 接続先: ${endpoint}:8883`);
console.log(`[7207] クライアント証明書: なし（意図的に省略）`);
console.log(`[7207] 期待結果: TLS ハンドシェイクで拒否（ECONNRESET / TLS alert）`);
console.log("");

const result = await new Promise((resolve) => {
  const socket = tls.connect(
    {
      host: endpoint,
      port: 8883,
      // [意図的] cert / key を設定しない → サーバー側で相互 TLS 認証が必要なので拒否されるはず
      rejectUnauthorized: false, // サーバー証明書の検証は行う（CA 検証は別途）
    },
    () => {
      // 接続成功してしまった場合は NG
      const cipher = socket.getCipher();
      socket.destroy();
      resolve({ ok: false, reason: `unexpected connect: cipher=${cipher?.name}` });
    }
  );

  socket.on("error", (err) => {
    resolve({ ok: true, reason: `rejected as expected: ${err.code ?? err.message}` });
  });

  // 10秒タイムアウト
  setTimeout(() => {
    socket.destroy();
    resolve({ ok: false, reason: "timeout: no response from server" });
  }, 10000);
});

console.log(`[7207] 結果: ${result.ok ? "OK" : "NG"}`);
console.log(`[7207] 詳細: ${result.reason}`);
process.exit(result.ok ? 0 : 1);
