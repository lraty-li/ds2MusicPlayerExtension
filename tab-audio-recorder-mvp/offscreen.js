let capturedStream = null;
let audioContext = null;
let sourceNode = null;
let workletNode = null;
let socket = null;
let sequence = 0n;
let sampleRate = 48000;
let streamUrl = null;
let targetTabId = null;
let connectTimer = null;
let streamToken = 0;
const PACKET_VERSION_FLOAT32 = 2;
const SAMPLE_FORMAT_FLOAT32 = 2;
const AUDIO_HEADER_BYTES = 32;
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || typeof message.type !== "string") {
    return false;
  }

  if (message.type === "start-stream") {
    startStream(message)
      .then(() => sendResponse({ ok: true }))
      .catch((error) => {
        reportError(error);
        sendResponse({ ok: false, error: String(error) });
      });
    return true;
  }

  if (message.type === "stop-stream") {
    stopStream();
    sendResponse({ ok: true });
    return true;
  }

  if (message.type === "get-status") {
    sendResponse({
      active: !!capturedStream,
      connected: !!socket && socket.readyState === WebSocket.OPEN,
      tabId: targetTabId
    });
    return true;
  }

  if (message.type === "metadata-update") {
    sendResponse(sendMetadata(message.metadata));
    return true;
  }

  return false;
});

async function startStream(message) {
  stopStream(false);
  const token = ++streamToken;
  streamUrl = message.streamUrl;
  targetTabId = message.tabId;

  capturedStream = await navigator.mediaDevices.getUserMedia({
    audio: {
      mandatory: {
        chromeMediaSource: "tab",
        chromeMediaSourceId: message.streamId
      }
    },
    video: false
  });

  audioContext = new AudioContext({ sampleRate });
  sampleRate = audioContext.sampleRate;
  await audioContext.audioWorklet.addModule("pcm-worklet.js");

  sourceNode = audioContext.createMediaStreamSource(capturedStream);
  workletNode = new AudioWorkletNode(audioContext, "pcm-chunk-worklet", {
    numberOfInputs: 1,
    numberOfOutputs: 0,
    channelCount: 2
  });
  workletNode.port.onmessage = (event) => sendAudioChunk(event.data);
  sourceNode.connect(workletNode);
  reportWaiting();
  scheduleConnect(0, token);
}

function openSocketOnce(url) {
  return new Promise((resolve, reject) => {
    let settled = false;
    const ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";

    const fail = () => {
      if (settled) return;
      settled = true;
      ws.onopen = null;
      ws.onerror = null;
      ws.onclose = null;
      try {
        ws.close();
      } catch (_) {
      }
      reject(new Error(`WebSocket failed: ${url}`));
    };

    ws.onopen = () => {
      if (settled) return;
      settled = true;
      resolve(ws);
    };
    ws.onerror = fail;
    ws.onclose = fail;
  });
}

function scheduleConnect(delayMs, token = streamToken) {
  if (connectTimer || !capturedStream || !streamUrl) {
    return;
  }

  connectTimer = setTimeout(() => {
    connectTimer = null;
    connectSocket(token);
  }, delayMs);
}

async function connectSocket(token) {
  if (token !== streamToken || socket || !capturedStream) {
    return;
  }

  reportWaiting();
  try {
    const ws = await openSocketOnce(streamUrl);
    if (token !== streamToken || !capturedStream) {
      ws.close();
      return;
    }

    socket = ws;
    socket.onmessage = (event) => handleSocketMessage(event.data);
    socket.onclose = () => handleSocketClosed(ws);
    socket.onerror = () => handleSocketClosed(ws);
    chrome.runtime.sendMessage({ type: "stream-connected" });
  } catch (_) {
    if (token === streamToken && capturedStream) {
      scheduleConnect(1000, token);
    }
  }
}

function handleSocketClosed(ws) {
  if (socket !== ws) {
    return;
  }

  socket.onclose = null;
  socket.onerror = null;
  socket.onmessage = null;
  try {
    ws.close();
  } catch (_) {
  }
  socket = null;
  reportWaiting();
  scheduleConnect(1000);
}

function handleSocketMessage(data) {
  if (typeof data !== "string") {
    return;
  }

  let message = null;
  try {
    message = JSON.parse(data);
  } catch (_) {
    return;
  }

  if (message && message.type === "control") {
    chrome.runtime.sendMessage({
      type: "browser-control",
      tabId: targetTabId,
      command: message.command,
      reason: message.reason || ""
    });
  }
}

function sendMetadata(metadata) {
  if (!socket || socket.readyState !== WebSocket.OPEN || !metadata) {
    return { ok: false, sent: false, error: "socket closed" };
  }

  const payload = {
    type: "metadata",
    title: String(metadata.title || "").slice(0, 512),
    artist: String(metadata.artist || "").slice(0, 512),
    adapter: String(metadata.adapter || ""),
    host: String(metadata.host || "")
  };
  try {
    socket.send(JSON.stringify(payload));
    return { ok: true, sent: true, metadata: payload };
  } catch (_) {
    handleSocketClosed(socket);
    return { ok: false, sent: false, error: "send failed" };
  }
}

function sendAudioChunk(message) {
  if (!socket || socket.readyState !== WebSocket.OPEN) {
    return;
  }

  const audio = message.audio;
  const frameCount = message.frames;
  const packet = new ArrayBuffer(AUDIO_HEADER_BYTES + audio.byteLength);
  const view = new DataView(packet);

  view.setUint32(0, 0x44533241, true);
  view.setUint16(4, PACKET_VERSION_FLOAT32, true);
  view.setUint16(6, 2, true);
  view.setUint32(8, sampleRate, true);
  view.setUint32(12, frameCount, true);
  view.setBigUint64(16, sequence++, true);
  view.setUint32(24, audio.byteLength, true);
  view.setUint16(28, SAMPLE_FORMAT_FLOAT32, true);
  view.setUint16(30, AUDIO_HEADER_BYTES, true);
  new Uint8Array(packet, AUDIO_HEADER_BYTES).set(new Uint8Array(audio));
  try {
    socket.send(packet);
  } catch (_) {
    handleSocketClosed(socket);
  }
}

function stopStream(notify = true) {
  streamToken++;
  if (connectTimer) {
    clearTimeout(connectTimer);
    connectTimer = null;
  }
  if (workletNode) {
    workletNode.port.onmessage = null;
    workletNode.disconnect();
  }
  if (sourceNode) {
    sourceNode.disconnect();
  }
  if (audioContext) {
    audioContext.close();
  }
  if (capturedStream) {
    for (const track of capturedStream.getTracks()) {
      track.stop();
    }
  }
  if (socket) {
    socket.onclose = null;
    socket.close();
  }

  capturedStream = null;
  audioContext = null;
  sourceNode = null;
  workletNode = null;
  socket = null;
  streamUrl = null;
  targetTabId = null;
  sequence = 0n;

  if (notify) {
    chrome.runtime.sendMessage({ type: "stream-stopped" });
  }
}

function reportWaiting() {
  chrome.runtime.sendMessage({ type: "stream-waiting" });
}

function reportError(error) {
  stopStream(false);
  chrome.runtime.sendMessage({
    type: "stream-error",
    error: String(error)
  });
}
