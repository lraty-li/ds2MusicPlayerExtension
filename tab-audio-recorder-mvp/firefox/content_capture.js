let targetTabId = null;
let active = false;
let hostConnected = false;

window.__ds2FirefoxCapture = {
  async start(options) {
    stopFirefoxCapture(false);
    targetTabId = options.tabId;
    active = true;
    hostConnected = false;
    window.addEventListener("message", onPageCaptureMessage);
    logFirefoxContent("capture content started");
    reportWaiting();
    return { ok: true };
  },
  stop() {
    stopFirefoxCapture(true);
    return { ok: true };
  },
  status() {
    return { active, connected: hostConnected };
  }
};

chrome.runtime.onMessage.addListener((message) => {
  if (!message || message.type !== "firefox-stream-host-status") return false;
  if (!active) return false;
  hostConnected = !!message.connected;
  logFirefoxContent(`host status connected=${hostConnected}`);
  if (hostConnected) {
    startMetadataPolling();
    sendRuntimeMessage({ type: "stream-connected" });
  } else {
    stopMetadataPolling();
    reportWaiting();
  }
  return false;
});

function stopFirefoxCapture(notify) {
  hostConnected = false;
  stopMetadataPolling();
  window.removeEventListener("message", onPageCaptureMessage);
  active = false;
  targetTabId = null;
  if (notify) sendRuntimeMessage({ type: "firefox-stream-stopped" });
}

function onPageCaptureMessage(event) {
  if (event.source !== window || !event.data || typeof event.data.type !== "string") return;
  if (event.data.type === "ds2-firefox-audio-chunk") {
    if (!hostConnected) return;
    sendAudioChunk({ audio: event.data.audio, frames: event.data.frames },
      event.data.sampleRate || 48000);
  } else if (event.data.type === "ds2-firefox-capture-status") {
    if (event.data.stage === "stopped" && active) stopFirefoxCapture(true);
    if (event.data.stage === "error") {
      sendRuntimeMessage({ type: "stream-error", error: event.data.error || "" });
    }
  }
}

function reportWaiting() {
  sendRuntimeMessage({ type: "stream-waiting" });
}

function getTargetTabId() {
  return targetTabId;
}

function isStreamSocketOpen() {
  return hostConnected;
}

function sendJsonPayload(payload) {
  if (!hostConnected) return false;
  sendRuntimeMessage({
    type: "firefox-stream-json",
    payload
  });
  return true;
}

function sendAudioChunk(message, sampleRate) {
  if (!hostConnected || !message || !message.audio) return;
  sendRuntimeMessage({
    type: "firefox-audio-chunk",
    audio: message.audio,
    frames: message.frames,
    sampleRate
  });
}

function sendRuntimeMessage(message) {
  try {
    const result = chrome.runtime.sendMessage(message);
    if (result && typeof result.catch === "function") result.catch(() => {});
  } catch (error) {
    logFirefoxContent(`runtime message failed: ${error}`);
  }
}

function logFirefoxContent(text) {
  console.log(`[DS2 Firefox Content] ${text}`);
}
