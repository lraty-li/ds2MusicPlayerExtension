let capturedStream = null;
let audioContext = null;
let sourceNode = null;
let workletNode = null;
let socket = null;
let sequence = 0n;
let sampleRate = 48000;

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

  return false;
});

async function startStream(message) {
  stopStream(false);

  capturedStream = await navigator.mediaDevices.getUserMedia({
    audio: {
      mandatory: {
        chromeMediaSource: "tab",
        chromeMediaSourceId: message.streamId
      }
    },
    video: false
  });

  socket = await openSocket(message.bridgeUrl);
  audioContext = new AudioContext({ sampleRate });
  sampleRate = audioContext.sampleRate;
  await audioContext.audioWorklet.addModule("pcm-worklet.js");

  sourceNode = audioContext.createMediaStreamSource(capturedStream);
  workletNode = new AudioWorkletNode(audioContext, "pcm-chunk-worklet", {
    numberOfInputs: 1,
    numberOfOutputs: 0,
    channelCount: 2
  });
  workletNode.port.onmessage = (event) => sendPcmChunk(event.data);
  sourceNode.connect(workletNode);
}

function openSocket(url) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error(`WebSocket failed: ${url}`));
    ws.onclose = () => {
      if (socket === ws) {
        reportError(new Error("WebSocket closed"));
      }
    };
  });
}

function sendPcmChunk(message) {
  if (!socket || socket.readyState !== WebSocket.OPEN) {
    return;
  }

  const pcm = message.pcm;
  const frameCount = message.frames;
  const headerBytes = 28;
  const packet = new ArrayBuffer(headerBytes + pcm.byteLength);
  const view = new DataView(packet);

  view.setUint32(0, 0x44533241, true);
  view.setUint16(4, 1, true);
  view.setUint16(6, 2, true);
  view.setUint32(8, sampleRate, true);
  view.setUint32(12, frameCount, true);
  view.setBigUint64(16, sequence++, true);
  view.setUint32(24, pcm.byteLength, true);
  new Uint8Array(packet, headerBytes).set(new Uint8Array(pcm));
  socket.send(packet);
}

function stopStream(notify = true) {
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
  sequence = 0n;

  if (notify) {
    chrome.runtime.sendMessage({ type: "stream-stopped" });
  }
}

function reportError(error) {
  stopStream(false);
  chrome.runtime.sendMessage({
    type: "stream-error",
    error: String(error)
  });
}
