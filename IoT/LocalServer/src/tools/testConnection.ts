/**
 * @file testConnection.ts
 * @description MQTTブローカー接続とstatus要求publishの疎通試験を行う単体スクリプト。
 * @remarks
 * - [重要] 接続・publish・subscribeの最小成功確認を目的とする。
 * - [厳守] 実行結果は `IoT/試験記録書.md` へ追記して運用履歴を残す。
 * - [禁止] 証明書検証を無効化して成功扱いにしない。
 */

import mqtt from "mqtt";
import fs from "fs";
import { loadConfig } from "../config";

const config = loadConfig();
// [重要] DNS優先だが、DNS未解決環境ではIPを併用して到達性を確保する。
const connectHost = config.mqttFallbackIp.length > 0 ? config.mqttFallbackIp : config.mqttHost;
const brokerUrl = `${config.mqttProtocol}://${connectHost}:${config.mqttPort}`;
const testTopic = "esp32lab/call/status/all";
const testPayload = JSON.stringify({
  v: 1,
  DstID: "all",
  SrcID: `${config.sourceId}-test`,
  id: `${config.sourceId}-test-${Date.now()}`,
  ts: new Date().toISOString(),
  op: "call",
  sub: "status",
  args: {
    requestType: "statusRequest",
    reason: "manualTest"
  }
});

const testClient = mqtt.connect(brokerUrl, {
  username: config.mqttUsername,
  password: config.mqttPassword,
  ca: config.mqttProtocol === "mqtts" ? fs.readFileSync(config.mqttCaPath) : undefined,
  rejectUnauthorized: config.mqttProtocol === "mqtts",
  servername: config.mqttProtocol === "mqtts" && config.mqttFallbackIp.length > 0 ? config.mqttHost : undefined
});

const timeoutHandle = setTimeout(() => {
  console.error("testConnection timeout. reason=connect/publish not completed within 15s");
  testClient.end(true, () => process.exit(1));
}, 15000);

testClient.on("connect", () => {
  console.log(`testConnection connected. brokerUrl=${brokerUrl}`);
  testClient.subscribe("esp32lab/notice/status/+", { qos: 1 }, (subscribeError) => {
    if (subscribeError !== undefined && subscribeError !== null) {
      console.error(`testConnection subscribe failed. reason=${subscribeError.message}`);
      clearTimeout(timeoutHandle);
      testClient.end(true, () => process.exit(1));
      return;
    }
    testClient.publish(testTopic, testPayload, { qos: 1 }, (publishError) => {
      if (publishError !== undefined && publishError !== null) {
        console.error(`testConnection publish failed. reason=${publishError.message} topic=${testTopic}`);
        clearTimeout(timeoutHandle);
        testClient.end(true, () => process.exit(1));
        return;
      }
      console.log(`testConnection publish success. topic=${testTopic}`);
      clearTimeout(timeoutHandle);
      testClient.end(true, () => process.exit(0));
    });
  });
});

testClient.on("error", (clientError) => {
  console.error(`testConnection mqtt error. message=${clientError.message}`);
});
