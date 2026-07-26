const fs = require("fs");
const vm = require("vm");

const sent = [];
const notifications = [];
const socket = {
  readyState: 1,
  send(value) {
    sent.push(value);
  },
  close() {}
};
const context = vm.createContext({
  ArrayBuffer,
  DataView,
  Float32Array,
  Math,
  Set,
  BigInt,
  WebSocket: { OPEN: 1 },
  chrome: {
    runtime: {
      sendMessage(message) {
        notifications.push(message);
      }
    }
  }
});

vm.runInContext(
  fs.readFileSync("stream_socket.js", "utf8"),
  context
);
context.attachStreamSocket(socket, () => {});

const hello = JSON.parse(sent[0]);
const initialClaim = JSON.parse(sent[1]);
if (hello.type !== "source_hello" ||
    hello.sourceKind !== "tab_capture") {
  throw new Error("source hello missing");
}
if (initialClaim.type !== "source_claim" ||
    initialClaim.reason !== "capture_started") {
  throw new Error("initial source claim missing");
}

context.markStreamSourcePreempted();
context.closeStreamSocket();
context.attachStreamSocket(socket, () => {});
const reconnectHello = JSON.parse(sent[2]);
if (reconnectHello.type !== "source_hello" ||
    sent.length !== 3) {
  throw new Error("standby reconnect reclaimed the source");
}
const silent = new Float32Array(960).buffer;
for (let index = 0; index < 20; index++) {
  context.observeStreamAudio(silent);
}
const audible = new Float32Array(960);
audible[0] = 0.25;
context.observeStreamAudio(audible.buffer);

const resumedClaim = JSON.parse(sent[3]);
if (resumedClaim.type !== "source_claim" ||
    resumedClaim.reason !== "audio_resumed") {
  throw new Error("audio-resume claim missing");
}
if (!notifications.some((item) => item.type === "source-preempted") ||
    !notifications.some((item) => item.type === "source-active")) {
  throw new Error("source state notification missing");
}
console.log("TAB_SOURCE_CLAIM_TEST_OK");
