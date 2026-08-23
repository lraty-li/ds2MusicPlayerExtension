const STREAM_URL = "ws://127.0.0.1:47832";
const CONTENT_CAPTURE_FILES = [
  "jacket_transfer.js",
  "metadata_transfer.js",
  "content_capture.js"
];
const PAGE_CAPTURE_FILES = ["page_capture.js"];
const FIREFOX_BUILD = "1.1-csp-https-ytmusic";

let state = { streaming: false, tabId: null, frameId: null, status: "idle" };
let autoPauseTimer = null;

logFirefoxBackground(`loaded build=${FIREFOX_BUILD}`);

chrome.action.onClicked.addListener((tab) => {
  const tabId = tab && typeof tab.id === "number" ? tab.id : null;
  logFirefoxBackground(`action clicked tab=${tabId}`);
  toggleStream(tabId).catch((error) => {
    logFirefoxBackground(`toggle failed: ${error}`);
    resetState("ERR");
    console.error("Failed to toggle Firefox PCM stream:", error);
  });
});

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || typeof message.type !== "string") return false;
  if (message.type === "firefox-stream-stopped") {
    stopFirefoxStreamHost();
    resetState("OK");
    clearBadgeLater();
  } else if (message.type === "stream-waiting") {
    restoreState(sender, "waiting");
    if (state.streaming) setStateBadge("waiting", "WAIT", "#7a5c00");
  } else if (message.type === "stream-connected") {
    restoreState(sender, "streaming");
    if (state.streaming) setStateBadge("streaming", "PCM", "#0057b8");
  } else if (message.type === "read-metadata") {
    const tabId = typeof message.tabId === "number" ? message.tabId : state.tabId;
    readBrowserMetadata(tabId).then(sendResponse)
      .catch((error) => sendResponse({ ok: false, error: String(error) }));
    return true;
  } else if (message.type === "browser-control") {
    scheduleBrowserControl(message);
  } else if (message.type === "stream-error") {
    stopFirefoxStreamHost();
    resetState("ERR");
    console.error("Firefox PCM stream error:", message.error);
  } else if (message.type === "firefox-audio-chunk") {
    if (isActiveCaptureSender(sender)) sendFirefoxStreamAudio(message);
  } else if (message.type === "firefox-stream-json") {
    if (isActiveCaptureSender(sender)) sendFirefoxStreamJson(message.payload);
  }
  return false;
});

async function toggleStream(tabId) {
  if (state.streaming) {
    await stopActiveStream();
    return;
  }
  const activeFrameId = await findActiveCaptureFrame(tabId);
  if (typeof activeFrameId === "number") {
    state = { streaming: true, tabId, frameId: activeFrameId, status: "streaming" };
    await stopActiveStream();
    return;
  }
  await startStream(tabId);
}

async function startStream(tabId) {
  if (typeof tabId !== "number") throw new Error("missing active tab");
  await injectPageControl(tabId, { allFrames: true });
  const probes = await callPageControl(tabId, { allFrames: true }, "captureProbe", "");
  const frame = selectCaptureFrame(probes);
  logFirefoxBackground(`capture probes=${JSON.stringify(summarizeCaptureProbes(probes))}`);
  if (!frame) throw new Error("no adapter capture source");

  state = { streaming: true, tabId, frameId: frame.frameId, status: "waiting" };
  setBadge("WAIT", "#7a5c00");
  logFirefoxBackground(`inject content capture tab=${tabId} frame=${frame.frameId}`);
  await injectContentCapture(tabId, frame.frameId);
  logFirefoxBackground("content capture injected");
  await callContentCapture(tabId, frame.frameId, "start", { tabId, streamUrl: STREAM_URL });
  logFirefoxBackground("content capture started");
  startFirefoxStreamHost({
    streamUrl: STREAM_URL,
    onWaiting: handleFirefoxHostWaiting,
    onConnected: handleFirefoxHostConnected,
    onControl: handleFirefoxHostControl
  });
  logFirefoxBackground("host start requested");
  try {
    logFirefoxBackground("inject page capture");
    await injectPageCapture(tabId, frame.frameId);
    logFirefoxBackground("page capture injected");
    await callPageCapture(tabId, frame.frameId, "start");
    logFirefoxBackground(`page capture started tab=${tabId} frame=${frame.frameId}`);
  } catch (error) {
    stopFirefoxStreamHost();
    await callContentCapture(tabId, frame.frameId, "stop", {}).catch(() => {});
    throw error;
  }
}

async function stopActiveStream() {
  clearAutoPauseTimer();
  stopFirefoxStreamHost();
  notifyContentHostStatus(false);
  if (typeof state.tabId === "number" && typeof state.frameId === "number") {
    await callPageCapture(state.tabId, state.frameId, "stop").catch(() => {});
    await callContentCapture(state.tabId, state.frameId, "stop", {}).catch(() => {});
  }
  resetState("OK");
  clearBadgeLater();
}

function selectCaptureFrame(results) {
  let best = null;
  for (const item of results || []) {
    const result = item && item.result;
    const score = result && result.score || 0;
    if (score > 0 && (!best || score > best.score)) {
      best = { frameId: item.frameId, score, result };
    }
  }
  return best;
}

async function findActiveCaptureFrame(tabId) {
  if (typeof tabId !== "number") return null;
  let results = [];
  try {
    results = await chrome.scripting.executeScript({
      target: { tabId, allFrames: true },
      func: () => {
        const capture = window.__ds2FirefoxCapture;
        return !!(capture && typeof capture.status === "function" && capture.status().active);
      }
    });
  } catch (_) {
    return null;
  }
  for (const item of results || []) if (item && item.result) return item.frameId;
  return null;
}

async function injectContentCapture(tabId, frameId) {
  const injected = await chrome.scripting.executeScript({
    target: { tabId, frameIds: [frameId] },
    func: () => !!window.__ds2FirefoxCapture
  });
  if (injected && injected[0] && injected[0].result) return;
  await chrome.scripting.executeScript({
    target: { tabId, frameIds: [frameId] },
    files: CONTENT_CAPTURE_FILES
  });
}

async function injectPageCapture(tabId, frameId) {
  await injectPageControl(tabId, { frameIds: [frameId] });
  await chrome.scripting.executeScript({
    target: { tabId, frameIds: [frameId] },
    world: "MAIN",
    files: PAGE_CAPTURE_FILES
  });
}

async function callContentCapture(tabId, frameId, method, payload) {
  const results = await chrome.scripting.executeScript({
    target: { tabId, frameIds: [frameId] },
    func: (name, data) => {
      const capture = window.__ds2FirefoxCapture;
      if (!capture || typeof capture[name] !== "function") {
        return { ok: false, error: "missing content capture" };
      }
      return capture[name](data);
    },
    args: [method, payload]
  });
  return unwrapCallResult(results, "content capture failed");
}

async function callPageCapture(tabId, frameId, method) {
  const results = await chrome.scripting.executeScript({
    target: { tabId, frameIds: [frameId] },
    world: "MAIN",
    func: (name) => {
      const capture = window.__ds2FirefoxPageCapture;
      if (!capture || typeof capture[name] !== "function") {
        return { ok: false, error: "missing page capture" };
      }
      return capture[name]();
    },
    args: [method]
  });
  return unwrapCallResult(results, "page capture failed");
}

function unwrapCallResult(results, fallback) {
  const first = results && results[0];
  if (first && first.error) throw new Error(first.error.message || String(first.error));
  const result = first && first.result;
  if (!result || !result.ok) throw new Error(result && result.error || fallback);
  return result;
}

function scheduleBrowserControl(message) {
  if (message.command === "pause" && message.reason !== "auto_block") clearAutoPauseTimer();
  if (message.reason === "auto_block" && message.command === "pause") {
    clearAutoPauseTimer();
    autoPauseTimer = setTimeout(() => {
      autoPauseTimer = null;
      runBrowserControl(message);
    }, 1500);
    return;
  }
  if (message.reason === "auto_block" && message.command === "resume" && autoPauseTimer) {
    clearAutoPauseTimer();
    return;
  }
  if (message.command === "resume") clearAutoPauseTimer();
  runBrowserControl(message);
}

function clearAutoPauseTimer() {
  if (!autoPauseTimer) return;
  clearTimeout(autoPauseTimer);
  autoPauseTimer = null;
}

async function runBrowserControl(message) {
  try {
    const result = await handleBrowserControl(message, state.tabId, state.frameId);
    if (!result.ok) {
      setBadge(result.error || "CTRL", "#8b0000");
      return;
    }
    setBadge(message.command === "pause" ? "PAUS" : "PLAY", "#4b4b4b");
    setTimeout(() => {
      if (state.status === "streaming") setBadge("PCM", "#0057b8");
    }, 700);
  } catch (error) {
    setBadge("CTRL", "#8b0000");
    console.error("Failed to control Firefox tab media:", error);
  }
}

function setStateBadge(status, text, color) {
  state.status = status;
  setBadge(text, color);
}

function restoreState(sender, status) {
  if (state.streaming || !sender || !sender.tab) return;
  if (typeof sender.tab.id !== "number" || typeof sender.frameId !== "number") return;
  state = { streaming: true, tabId: sender.tab.id, frameId: sender.frameId, status };
}

function isActiveCaptureSender(sender) {
  return state.streaming && sender && sender.tab &&
    sender.tab.id === state.tabId && sender.frameId === state.frameId;
}

function handleFirefoxHostWaiting() {
  if (!state.streaming) return;
  logFirefoxBackground("host waiting");
  setStateBadge("waiting", "WAIT", "#7a5c00");
  notifyContentHostStatus(false);
}

function handleFirefoxHostConnected() {
  if (!state.streaming) return;
  logFirefoxBackground("host connected");
  setStateBadge("streaming", "PCM", "#0057b8");
  notifyContentHostStatus(true);
}

function handleFirefoxHostControl(message) {
  scheduleBrowserControl({
    tabId: state.tabId,
    command: message.command,
    reason: message.reason || ""
  });
}

function notifyContentHostStatus(connected) {
  if (typeof state.tabId !== "number" || typeof state.frameId !== "number") return;
  sendTabMessage(state.tabId, {
    type: "firefox-stream-host-status",
    connected: !!connected
  }, { frameId: state.frameId });
}

function resetState(text) {
  state = { streaming: false, tabId: null, frameId: null, status: "idle" };
  setBadge(text, text === "ERR" ? "#8b0000" : "#006b3c");
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

function summarizeCaptureProbes(results) {
  return (results || []).map((item) => ({
    frameId: item && item.frameId,
    result: item && item.result
  }));
}

function sendTabMessage(tabId, message, options) {
  try {
    const result = chrome.tabs.sendMessage(tabId, message, options);
    if (result && typeof result.catch === "function") result.catch(() => {});
  } catch (_) {
  }
}

function logFirefoxBackground(text) {
  console.log(`[DS2 Firefox] ${text}`);
}
