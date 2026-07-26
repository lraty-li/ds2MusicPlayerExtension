const PACKET_VERSION_FLOAT32 = 2;
const SAMPLE_FORMAT_FLOAT32 = 2;
const AUDIO_HEADER_BYTES = 32;
const SOURCE_ID = "chrome-tab-capture";
const SOURCE_KIND = "tab_capture";
const RECLAIM_SILENCE_CHUNKS = 20;

let socket = null;
let sequence = 0n;
let streamSocketClosedCallback = null;
let sourceOwned = false;
let sourcePreempted = false;
let reclaimSilentChunks = 0;
let reclaimArmed = false;

function setStreamSocketClosedCallback(callback) {
  streamSocketClosedCallback = typeof callback === "function" ? callback : null;
}

function getStreamSocket() {
  return socket;
}

function resetStreamPacketSequence() {
  sequence = 0n;
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

function attachStreamSocket(ws, onTextMessage) {
  socket = ws;
  socket.onmessage = (event) => {
    if (typeof event.data === "string") onTextMessage(event.data);
  };
  socket.onclose = () => handleSocketClosed(ws);
  socket.onerror = () => handleSocketClosed(ws);
  sendSourceHello();
  if (sourcePreempted) {
    notifySourceState("source-preempted");
  } else {
    claimStreamSource("capture_started");
  }
}

function closeStreamSocket() {
  if (!socket) return;
  const ws = socket;
  socket = null;
  ws.onclose = null;
  ws.onerror = null;
  ws.onmessage = null;
  try {
    ws.close();
  } catch (_) {
  }
  resetStreamSourceState();
}

function handleSocketClosed(ws) {
  if (socket !== ws) {
    return;
  }

  socket.onclose = null;
  socket.onerror = null;
  socket.onmessage = null;
  try {
    socket.close();
  } catch (_) {
  }
  socket = null;
  resetStreamSourceState();
  if (streamSocketClosedCallback) {
    streamSocketClosedCallback();
  }
}

function isStreamSocketOpen() {
  return !!socket && socket.readyState === WebSocket.OPEN;
}

function sendJsonPayload(payload) {
  if (!isStreamSocketOpen()) {
    return false;
  }
  try {
    socket.send(JSON.stringify(payload));
    return true;
  } catch (_) {
    handleSocketClosed(socket);
    return false;
  }
}

function sendSourceHello() {
  return sendJsonPayload({
    type: "source_hello",
    sourceId: SOURCE_ID,
    sourceKind: SOURCE_KIND,
    label: "Chrome tabCapture"
  });
}

function claimStreamSource(reason) {
  const sent = sendJsonPayload({
    type: "source_claim",
    sourceId: SOURCE_ID,
    sourceKind: SOURCE_KIND,
    reason: String(reason || "explicit_playback")
  });
  if (!sent) return false;
  sourceOwned = true;
  sourcePreempted = false;
  reclaimSilentChunks = 0;
  reclaimArmed = false;
  notifySourceState("source-active");
  if (typeof refreshMetadataForSourceClaim === "function") {
    refreshMetadataForSourceClaim();
  }
  return true;
}

function markStreamSourcePreempted() {
  sourceOwned = false;
  sourcePreempted = true;
  reclaimSilentChunks = 0;
  reclaimArmed = false;
  notifySourceState("source-preempted");
}

function resetStreamSourceState(clearPreempted = false) {
  sourceOwned = false;
  if (clearPreempted) sourcePreempted = false;
  reclaimSilentChunks = 0;
  reclaimArmed = false;
}

function beginStreamSource() {
  resetStreamSourceState(true);
}

function isStreamSourceOwned() {
  return sourceOwned;
}

function isStreamSourcePreempted() {
  return sourcePreempted;
}

function notifySourceState(type) {
  try {
    const tabId = typeof getTargetTabId === "function"
      ? getTargetTabId()
      : null;
    chrome.runtime.sendMessage({ type, tabId });
  } catch (_) {
  }
}

function observeStreamAudio(audio) {
  if (sourceOwned || !(audio instanceof ArrayBuffer)) return;
  const samples = new Float32Array(audio);
  let peak = 0;
  for (let index = 0; index < samples.length; index++) {
    peak = Math.max(peak, Math.abs(samples[index]));
  }
  if (peak <= 0.0001) {
    reclaimSilentChunks++;
    if (reclaimSilentChunks >= RECLAIM_SILENCE_CHUNKS) reclaimArmed = true;
    return;
  }
  if (reclaimArmed) claimStreamSource("audio_resumed");
  reclaimSilentChunks = 0;
}

function sendMetadata(metadata) {
  if (!metadata) {
    return { ok: false, sent: false, error: "missing metadata" };
  }

  const payload = {
    type: "metadata",
    title: String(metadata.title || "").slice(0, 512),
    artist: String(metadata.artist || "").slice(0, 512),
    adapter: String(metadata.adapter || ""),
    host: String(metadata.host || "")
  };
  if (typeof metadata.paused === "boolean") {
    payload.paused = metadata.paused;
  }
  if (!sendJsonPayload(payload)) {
    return { ok: false, sent: false, error: "socket closed" };
  }
  return { ok: true, sent: true, metadata: payload };
}

function sendAudioChunk(message, sampleRate) {
  if (!isStreamSocketOpen()) {
    return;
  }

  const audio = message.audio;
  observeStreamAudio(audio);
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
