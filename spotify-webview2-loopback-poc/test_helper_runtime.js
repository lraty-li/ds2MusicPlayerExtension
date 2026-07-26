const messages = [];
let connectCount = 0;

class MemoryStorage {
  constructor() {
    this.values = new Map();
  }

  getItem(key) {
    return this.values.has(key) ? this.values.get(key) : null;
  }

  setItem(key, value) {
    this.values.set(key, String(value));
  }

  removeItem(key) {
    this.values.delete(key);
  }
}

class MockSpotifyPlayer {
  constructor() {
    this.listeners = new Map();
  }

  addListener(type, callback) {
    this.listeners.set(type, callback);
  }

  async connect() {
    connectCount++;
    this.listeners.get("ready")?.({ device_id: "test-device" });
    return true;
  }

  disconnect() {
  }
}

globalThis.window = globalThis;
globalThis.location = {
  search: "?client_id=0123456789abcdef0123456789abcdef"
};
globalThis.localStorage = new MemoryStorage();
globalThis.sessionStorage = new MemoryStorage();
globalThis.localStorage.setItem(
  "ds2.spotify.webview2Poc.token",
  JSON.stringify({
    clientId: "0123456789abcdef0123456789abcdef",
    refreshToken: "test-refresh-token"
  })
);
globalThis.chrome = {
  webview: {
    postMessage(message) {
      messages.push(String(message));
    }
  }
};
globalThis.addEventListener = () => {};
globalThis.Spotify = { Player: MockSpotifyPlayer };
Object.defineProperty(globalThis, "navigator", {
  configurable: true,
  value: {
    requestMediaKeySystemAccess() {
      return Promise.reject(
        new DOMException("temporary failure", "NotSupportedError")
      );
    }
  }
});

await import("./web/helper-runtime.js");
await new Promise((resolve) => setTimeout(resolve, 20));

if (connectCount !== 1) {
  throw new Error(`Spotify Player did not connect: ${connectCount}`);
}
if (!messages.some((message) =>
  message.includes("widevine=unavailable:NotSupportedError")
)) {
  throw new Error("Widevine failure status was not reported");
}
if (!messages.some((message) => message.includes("device=ready"))) {
  throw new Error("Spotify Player did not reach ready after EME failure");
}

console.log("HELPER_RUNTIME_WIDEVINE_FALLBACK_TEST_OK");
