importScripts("media_control.js", "background_chromium_host.js");

const STREAM_URL = "ws://127.0.0.1:47832";
const AUTO_PAUSE_DELAY_MS = 1500;

let state = {
  streaming: false,
  tabId: null,
  status: "idle",
  lastControl: ""
};
let autoPauseTimer = null;

chrome.action.onClicked.addListener((tab) => {
  const tabId = tab && typeof tab.id === "number" ? tab.id : null;
  toggleStream(tabId).catch((error) => {
    state = { streaming: false, tabId: null, status: "idle", lastControl: "ERR" };
    setBadge("ERR", "#8b0000");
    console.error("Failed to toggle PCM stream:", error);
  });
});

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || typeof message.type !== "string") return false;
  if (message.type === "stream-stopped") {
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
    }
  } else if (message.type === "source-preempted") {
    state.streaming = true;
    state.tabId = typeof message.tabId === "number" ? message.tabId : state.tabId;
    state.status = "standby";
    setBadge("STBY", "#6b4f8a");
  } else if (message.type === "source-active") {
    state.streaming = true;
    state.tabId = typeof message.tabId === "number" ? message.tabId : state.tabId;
    state.status = "streaming";
    setBadge("PCM", "#0057b8");
  } else if (message.type === "read-metadata") {
    const tabId = typeof message.tabId === "number" ? message.tabId : state.tabId;
    readBrowserMetadata(tabId)
      .then(sendResponse)
      .catch((error) => sendResponse({ ok: false, error: String(error) }));
    return true;
  } else if (message.type === "browser-control") {
    scheduleBrowserControl(message);
  } else if (message.type === "stream-error") {
    state = { streaming: false, tabId: null, status: "idle", lastControl: "ERR" };
    setBadge("ERR", "#8b0000");
    console.error("PCM stream error:", message.error);
  }

  return false;
});

async function toggleStream(tabId) {
  const status = await readCaptureHostStatus();
  if (status.active && status.preempted === true) {
    await reclaimActiveStream(status.tabId);
    return;
  }
  if (state.streaming || status.active) {
    await stopActiveStream();
    return;
  }
  await startStream(tabId);
}

async function reclaimActiveStream(tabId) {
  if (!await claimCaptureHost()) {
    throw new Error("source claim failed");
  }
  state.streaming = true;
  state.tabId = typeof tabId === "number" ? tabId : state.tabId;
  state.status = "streaming";
  await runBrowserControl({
    command: "resume",
    reason: "source_reselected",
    tabId: state.tabId
  });
  setBadge("PCM", "#0057b8");
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

async function startStream(tabId) {
  if (typeof tabId !== "number") {
    throw new Error("missing active tab");
  }

  try {
    state = { streaming: true, tabId, status: "waiting", lastControl: "" };
    setBadge("WAIT", "#7a5c00");
    await startCaptureHost(tabId, STREAM_URL);
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

async function stopActiveStream() {
  clearAutoPauseTimer();
  try {
    await stopCaptureHost();
    state = { streaming: false, tabId: null, status: "idle", lastControl: "" };
    setBadge("OK", "#006b3c");
    clearBadgeLater();
  } catch (_) {
    state = { streaming: false, tabId: null, status: "idle", lastControl: "" };
    setBadge("OK", "#006b3c");
    clearBadgeLater();
  }
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
