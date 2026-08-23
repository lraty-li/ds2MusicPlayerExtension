const FIREFOX_PACKET_VERSION_FLOAT32 = 2;
const FIREFOX_SAMPLE_FORMAT_FLOAT32 = 2;
const FIREFOX_AUDIO_HEADER_BYTES = 32;
const FIREFOX_MAX_AUDIO_BYTES = 4 * 1024 * 1024;

let firefoxSocket = null;
let firefoxConnectTimer = null;
let firefoxStreamUrl = "";
let firefoxStreamActive = false;
let firefoxStreamToken = 0;
let firefoxPacketSequence = 0n;
let firefoxHostCallbacks = {};

function startFirefoxStreamHost(options) {
  stopFirefoxStreamHost();
  firefoxStreamUrl = options.streamUrl || "";
  firefoxStreamActive = true;
  firefoxStreamToken++;
  firefoxPacketSequence = 0n;
  logFirefoxHost(`start url=${firefoxStreamUrl}`);
  firefoxHostCallbacks = {
    waiting: options.onWaiting,
    connected: options.onConnected,
    control: options.onControl
  };
  reportFirefoxHostWaiting();
  scheduleFirefoxHostConnect(0, firefoxStreamToken);
}

function stopFirefoxStreamHost() {
  if (firefoxStreamActive || firefoxSocket) logFirefoxHost("stop");
  firefoxStreamToken++;
  firefoxStreamActive = false;
  firefoxStreamUrl = "";
  firefoxHostCallbacks = {};
  firefoxPacketSequence = 0n;
  if (firefoxConnectTimer) {
    clearTimeout(firefoxConnectTimer);
    firefoxConnectTimer = null;
  }
  closeFirefoxHostSocket();
}

function getFirefoxStreamHostStatus() {
  return {
    active: firefoxStreamActive,
    connected: isFirefoxHostSocketOpen()
  };
}

function sendFirefoxStreamAudio(message) {
  if (!isFirefoxHostSocketOpen()) return false;
  const audio = message && message.audio;
  const frameCount = Number(message && message.frames || 0);
  const sampleRate = Number(message && message.sampleRate || 48000);
  if (!audio || !frameCount || audio.byteLength > FIREFOX_MAX_AUDIO_BYTES) {
    logFirefoxHost("drop audio chunk before send");
    return false;
  }

  const packet = new ArrayBuffer(FIREFOX_AUDIO_HEADER_BYTES + audio.byteLength);
  const view = new DataView(packet);
  view.setUint32(0, 0x44533241, true);
  view.setUint16(4, FIREFOX_PACKET_VERSION_FLOAT32, true);
  view.setUint16(6, 2, true);
  view.setUint32(8, sampleRate, true);
  view.setUint32(12, frameCount, true);
  view.setBigUint64(16, firefoxPacketSequence++, true);
  view.setUint32(24, audio.byteLength, true);
  view.setUint16(28, FIREFOX_SAMPLE_FORMAT_FLOAT32, true);
  view.setUint16(30, FIREFOX_AUDIO_HEADER_BYTES, true);
  new Uint8Array(packet, FIREFOX_AUDIO_HEADER_BYTES).set(new Uint8Array(audio));
  return sendFirefoxHostRaw(packet);
}

function sendFirefoxStreamJson(payload) {
  if (!payload || !isFirefoxHostSocketOpen()) return false;
  return sendFirefoxHostRaw(JSON.stringify(payload));
}

function scheduleFirefoxHostConnect(delayMs, token = firefoxStreamToken) {
  if (firefoxConnectTimer || !firefoxStreamActive || !firefoxStreamUrl) return;
  firefoxConnectTimer = setTimeout(() => {
    firefoxConnectTimer = null;
    connectFirefoxHostSocket(token);
  }, delayMs);
}

function connectFirefoxHostSocket(token) {
  if (token !== firefoxStreamToken || firefoxSocket || !firefoxStreamActive) return;
  reportFirefoxHostWaiting();
  logFirefoxHost(`connect attempt url=${firefoxStreamUrl}`);
  let ws = null;
  try {
    ws = new WebSocket(firefoxStreamUrl);
    ws.binaryType = "arraybuffer";
  } catch (_) {
    logFirefoxHost("connect constructor failed");
    scheduleFirefoxHostConnect(1000, token);
    return;
  }

  ws.onopen = () => {
    if (token !== firefoxStreamToken || !firefoxStreamActive) {
      try {
        ws.close();
      } catch (_) {
      }
      return;
    }
    firefoxSocket = ws;
    logFirefoxHost("websocket open");
    if (typeof firefoxHostCallbacks.connected === "function") {
      firefoxHostCallbacks.connected();
    }
  };
  ws.onmessage = (event) => handleFirefoxHostMessage(event.data);
  ws.onerror = () => {
    logFirefoxHost("websocket error");
    handleFirefoxHostClosed(ws);
  };
  ws.onclose = (event) => {
    logFirefoxHost(`websocket close code=${event.code} clean=${event.wasClean}`);
    handleFirefoxHostClosed(ws);
  };
}

function handleFirefoxHostClosed(ws) {
  if (firefoxSocket && firefoxSocket !== ws) return;
  if (firefoxSocket === ws) firefoxSocket = null;
  clearFirefoxSocketHandlers(ws);
  try {
    ws.close();
  } catch (_) {
  }
  if (!firefoxStreamActive) return;
  reportFirefoxHostWaiting();
  scheduleFirefoxHostConnect(1000);
}

function handleFirefoxHostMessage(data) {
  if (typeof data !== "string") return;
  let message = null;
  try {
    message = JSON.parse(data);
  } catch (_) {
    return;
  }
  if (message && message.type === "control" &&
    typeof firefoxHostCallbacks.control === "function") {
    firefoxHostCallbacks.control(message);
  }
}

function sendFirefoxHostRaw(payload) {
  if (!isFirefoxHostSocketOpen()) return false;
  try {
    firefoxSocket.send(payload);
    return true;
  } catch (_) {
    logFirefoxHost("send failed");
    handleFirefoxHostClosed(firefoxSocket);
    return false;
  }
}

function isFirefoxHostSocketOpen() {
  return !!firefoxSocket && firefoxSocket.readyState === WebSocket.OPEN;
}

function closeFirefoxHostSocket() {
  if (!firefoxSocket) return;
  const ws = firefoxSocket;
  firefoxSocket = null;
  clearFirefoxSocketHandlers(ws);
  try {
    ws.close();
  } catch (_) {
  }
}

function clearFirefoxSocketHandlers(ws) {
  if (!ws) return;
  ws.onopen = null;
  ws.onmessage = null;
  ws.onerror = null;
  ws.onclose = null;
}

function reportFirefoxHostWaiting() {
  if (typeof firefoxHostCallbacks.waiting === "function") {
    firefoxHostCallbacks.waiting();
  }
}

function logFirefoxHost(text) {
  console.log(`[DS2 Firefox Host] ${text}`);
}
