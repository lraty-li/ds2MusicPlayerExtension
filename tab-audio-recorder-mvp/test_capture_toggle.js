const fs = require("fs");
const vm = require("vm");

let captureStatus = null;
let stopCount = 0;
let claimCount = 0;

const context = vm.createContext({
  console,
  importScripts() {},
  clearTimeout() {},
  setTimeout() {
    return 1;
  },
  readCaptureHostStatus: async () => captureStatus,
  stopCaptureHost: async () => {
    stopCount++;
  },
  claimCaptureHost: async () => {
    claimCount++;
    return true;
  },
  startCaptureHost: async () => {},
  handleBrowserControl: async () => ({ ok: true }),
  chrome: {
    action: {
      onClicked: { addListener() {} },
      setBadgeText() {},
      setBadgeBackgroundColor() {}
    },
    runtime: {
      onMessage: { addListener() {} }
    }
  }
});

vm.runInContext(fs.readFileSync("service_worker.js", "utf8"), context);

async function run() {
  captureStatus = {
    active: true,
    connected: false,
    owned: false,
    preempted: false,
    tabId: 42
  };
  await context.toggleStream(42);
  if (stopCount !== 1 || claimCount !== 0) {
    throw new Error("waiting capture did not stop");
  }

  captureStatus = {
    active: true,
    connected: true,
    owned: false,
    preempted: true,
    tabId: 42
  };
  await context.toggleStream(42);
  if (stopCount !== 1 || claimCount !== 1) {
    throw new Error("preempted capture was not reclaimed");
  }

  console.log("CAPTURE_TOGGLE_TEST_OK");
}

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
