importScripts("media_control.js");

const OFFSCREEN_DOCUMENT = "offscreen.html";
const STREAM_URL = "ws://127.0.0.1:47832";
const AUTO_PAUSE_DELAY_MS = 1500;
const METADATA_POLL_MS = 2000;

let state = {
  streaming: false,
  tabId: null,
  status: "idle",
  lastControl: ""
};
let autoPauseTimer = null;
let metadataTimer = null;
let lastMetadataKey = "";

chrome.action.onClicked.addListener((tab) => {
  const tabId = tab && typeof tab.id === "number" ? tab.id : null;
  toggleStream(tabId).catch((error) => {
    state = { streaming: false, tabId: null, status: "idle", lastControl: "ERR" };
    setBadge("ERR", "#8b0000");
    console.error("Failed to toggle PCM stream:", error);
  });
});

chrome.runtime.onMessage.addListener((message) => {
  if (!message || typeof message.type !== "string") return false;
  if (message.type === "stream-stopped") {
    stopMetadataPolling();
    state = { streaming: false, tabId: null, status: "idle", lastControl: "" };
    setBadge("OK", "#006b3c");
    clearBadgeLater();
  } else if (message.type === "stream-waiting") {
    if (state.streaming) {
      state.status = "waiting";
      setBadge("WAIT", "#7a5c00");
    }
  } else if (message.type === "stream-connected") {
    if (state.streaming) {
      state.status = "streaming";
      setBadge("PCM", "#0057b8");
      lastMetadataKey = "";
      startMetadataPolling();
    }
  } else if (message.type === "browser-control") {
    scheduleBrowserControl(message);
  } else if (message.type === "stream-error") {
    stopMetadataPolling();
    state = { streaming: false, tabId: null, status: "idle", lastControl: "ERR" };
    setBadge("ERR", "#8b0000");
    console.error("PCM stream error:", message.error);
  }

  return false;
});

async function toggleStream(tabId, streamId) {
  const status = await readOffscreenStatus();
  if (state.streaming || status.active) {
    await stopActiveStream();
    return;
  }
  await startStream(tabId, streamId || null);
}

function scheduleBrowserControl(message) {
  if (message.command === "pause" && message.reason !== "auto_block") {
    clearAutoPauseTimer();
  }

  if (message.reason === "auto_block" && message.command === "pause") {
    clearAutoPauseTimer();
    autoPauseTimer = setTimeout(() => {
      autoPauseTimer = null;
      runBrowserControl(message);
    }, AUTO_PAUSE_DELAY_MS);
    return;
  }

  if (message.reason === "auto_block" && message.command === "resume") {
    if (autoPauseTimer) {
      clearAutoPauseTimer();
      return;
    }
  }
  if (message.command === "resume") {
    clearAutoPauseTimer();
  }

  runBrowserControl(message);
}

function clearAutoPauseTimer() {
  if (!autoPauseTimer) return;
  clearTimeout(autoPauseTimer);
  autoPauseTimer = null;
}

async function startStream(tabId, streamId) {
  if (typeof tabId !== "number") {
    throw new Error("missing active tab");
  }
  if (!streamId) {
    streamId = await chrome.tabCapture.getMediaStreamId({ targetTabId: tabId });
  }

  try {
    await ensureOffscreenDocument();
    state = { streaming: true, tabId, status: "waiting", lastControl: "" };
    setBadge("WAIT", "#7a5c00");
    const response = await chrome.runtime.sendMessage({
      type: "start-stream",
      tabId,
      streamId,
      streamUrl: STREAM_URL
    });
    if (!response || !response.ok) {
      throw new Error(response && response.error ? response.error : "stream start failed");
    }
  } catch (error) {
    state = { streaming: false, tabId: null, status: "idle", lastControl: "ERR" };
    setBadge("ERR", "#8b0000");
    console.error("Failed to start PCM stream:", error);
    throw error;
  }
}

async function runBrowserControl(message) {
  try {
    const result = await handleBrowserControl(message, state.tabId);
    if (!result.ok) {
      state.lastControl = result.error || "CTRL";
      setBadge(state.lastControl, "#8b0000");
      return { ok: false, result, state };
    }

    state.lastControl = message.command === "pause" ? "PAUS" : "PLAY";
    setBadge(state.lastControl, "#4b4b4b");
    setTimeout(() => {
      if (state.status === "streaming") setBadge("PCM", "#0057b8");
    }, 700);
    return { ok: true, result, state };
  } catch (error) {
    state.lastControl = "CTRL";
    setBadge("CTRL", "#8b0000");
    console.error("Failed to control tab media:", error);
    return { ok: false, error: String(error), state };
  }
}

async function readOffscreenStatus() {
  const documentUrl = chrome.runtime.getURL(OFFSCREEN_DOCUMENT);
  const contexts = await chrome.runtime.getContexts({
    contextTypes: ["OFFSCREEN_DOCUMENT"],
    documentUrls: [documentUrl]
  });
  if (contexts.length === 0) {
    return { exists: false, active: false, connected: false, tabId: null };
  }

  try {
    const status = await chrome.runtime.sendMessage({ type: "get-status" });
    return Object.assign({ exists: true }, status);
  } catch (_) {
    return { exists: true, active: false, connected: false, tabId: null };
  }
}

async function stopActiveStream() {
  clearAutoPauseTimer();
  stopMetadataPolling();
  try {
    await chrome.runtime.sendMessage({ type: "stop-stream" });
    state = { streaming: false, tabId: null, status: "idle", lastControl: "" };
  } catch (_) {
    state = { streaming: false, tabId: null, status: "idle", lastControl: "" };
    setBadge("OK", "#006b3c");
    clearBadgeLater();
  }
}

function startMetadataPolling() {
  if (metadataTimer) return;
  collectAndSendMetadata();
  metadataTimer = setInterval(collectAndSendMetadata, METADATA_POLL_MS);
}

function stopMetadataPolling() {
  if (!metadataTimer) return;
  clearInterval(metadataTimer);
  metadataTimer = null;
  lastMetadataKey = "";
}

async function collectAndSendMetadata() {
  if (!state.streaming || typeof state.tabId !== "number") return;
  const result = await readBrowserMetadata(state.tabId);
  if (!result.ok || !result.metadata || !result.metadata.title) return;

  const metadata = result.metadata;
  const key = `${metadata.title}\n${metadata.artist || ""}`;
  if (key === lastMetadataKey) return;
  lastMetadataKey = key;

  chrome.runtime.sendMessage({
    type: "metadata-update",
    metadata: {
      title: metadata.title,
      artist: metadata.artist || "",
      adapter: metadata.adapter || "",
      host: metadata.host || ""
    }
  });
}

async function ensureOffscreenDocument() {
  const status = await readOffscreenStatus();
  if (status.exists) return;

  await chrome.offscreen.createDocument({
    url: OFFSCREEN_DOCUMENT,
    reasons: ["USER_MEDIA"],
    justification: "Capture tab audio and stream PCM to the local DS2 runtime plugin."
  });
}

function setBadge(text, color) {
  chrome.action.setBadgeText({ text });
  chrome.action.setBadgeBackgroundColor({ color });
}

function clearBadgeLater() {
  setTimeout(() => {
    if (!state.streaming) chrome.action.setBadgeText({ text: "" });
  }, 5000);
}
