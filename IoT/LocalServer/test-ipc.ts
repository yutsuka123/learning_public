import { SecretCoreIpcClient } from "./src/secretCoreIpcClient";
import { SecretCoreManager } from "./src/secretCoreManager";

async function main() {
  const manager = new SecretCoreManager();
  manager.start();
  // 起動完了待ち
  await new Promise(r => setTimeout(r, 1000));

  const client = new SecretCoreIpcClient();
  console.log("Checking SecretCore health...");
  const isHealthy = await client.checkHealth();
  console.log(`Health status: ${isHealthy}`);

  if (isHealthy) {
    console.log("Sending issue_k_user request...");
    const issueRes = await client.sendRequest("issue_k_user");
    console.log("Issue response:", issueRes);

    console.log("Sending encrypt request...");
    const encryptRes = await client.sendRequest("encrypt", {
      targetDeviceName: "test-device",
      plainText: "Hello Secure World!"
    });
    console.log("Encrypt response:", encryptRes);

    if (encryptRes) {
      console.log("Sending decrypt request...");
      const decryptRes = await client.sendRequest("decrypt", {
        targetDeviceName: "test-device",
        encrypted: encryptRes
      });
      console.log("Decrypt response:", decryptRes);
    }
  }

  manager.stop();
}

main().catch(console.error);
