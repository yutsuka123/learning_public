const { keyService } = require("./dist/keyService.js");
const { SecretCoreIpcClient } = require("./dist/secretCoreIpcClient.js");

async function main() {
  const secretCoreClient = new SecretCoreIpcClient();
  const USE_SECRET_CORE = true;
  // dummy config
  const config = { kUserAppIdentifier: "test", sourceId: "test-src" };
  const service = new keyService(config, secretCoreClient, USE_SECRET_CORE);

  console.log("Checking SecretCore health...");
  const isHealthy = await secretCoreClient.checkHealth();
  console.log(`Health status: ${isHealthy}`);

  if (isHealthy) {
    console.log("=== Testing issueKUser ===");
    const issueKUserResult = await service.issueKUser();
    console.log(issueKUserResult);

    console.log("=== Testing encryptByKDevice ===");
    const encryptRes = await service.encryptByKDevice("test-device", "Hello SecretCore AES-GCM!");
    console.log(encryptRes);

    if (encryptRes) {
      console.log("=== Testing decryptByKDevice ===");
      const decryptRes = await service.decryptByKDevice("test-device", {
        ivBase64: encryptRes.ivBase64,
        cipherBase64: encryptRes.cipherBase64,
        tagBase64: encryptRes.tagBase64
      });
      console.log(`Decrypted plainText: ${decryptRes}`);
    }
  }
}

main().catch(console.error);