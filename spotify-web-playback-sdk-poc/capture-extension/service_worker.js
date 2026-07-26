const OFFSCREEN_DOCUMENT = "offscreen.html";
const EVENT_NAME = "ds2-spotify-capture-metrics";
let toggleInFlight = false;
let creatingOffscreen = null;

chrome.action.onClicked.addListener((tab) => {
  if (toggleInFlight || !Number.isInteger(tab.id)) return;
  toggleInFlight = true;
  toggleCapture(tab)
    .catch((error) => reportStartError(tab.id, error))
    .finally(() => {
      toggleInFlight = false;
    });
});

chrome.runtime.onMessage.addListener((message, sender) => {
  if (!isOffscreenMessage(message, sender)) return false;

  if (message.type === "metrics") {
    const detail = normalizeDetail(message.detail);
    updateBadge(message.tabId, detail);
    dispatchToPage(message.tabId, detail);
  } else if (message.type === "ended") {
    const detail = normalizeDetail(message.detail);
    updateBadge(message.tabId, detail);
    dispatchToPage(message.tabId, detail);
    closeOffscreenDocument();
  }
  return false;
});

chrome.tabs.onRemoved.addListener((tabId) => {
  stopIfCapturedTab(tabId);
});

async function toggleCapture(clickedTab) {
  const status = await readOffscreenStatus();
  if (status.active) {
    await stopCapture(status.tabId);
    return;
  }
  await startCapture(clickedTab.id);
}

async function startCapture(tabId) {
  await ensureOffscreenDocument();
  const streamId = await chrome.tabCapture.getMediaStreamId({
    targetTabId: tabId
  });

  const response = await chrome.runtime.sendMessage({
    scope: "capture-offscreen",
    type: "start",
    tabId,
    streamId
  });
  if (!response || !response.ok) {
    const message = response && response.error
      ? response.error
      : "failed to start capture";
    throw new Error(message);
  }

  const detail = normalizeDetail(response.detail);
  updateBadge(tabId, detail);
  await dispatchToPage(tabId, detail);
}

async function stopCapture(fallbackTabId) {
  const response = await chrome.runtime.sendMessage({
    scope: "capture-offscreen",
    type: "stop"
  });
  const tabId = Number.isInteger(response && response.tabId)
    ? response.tabId
    : fallbackTabId;
  const detail = normalizeDetail(response && response.detail);
  await dispatchToPage(tabId, detail);
  await clearBadge(tabId);
  await closeOffscreenDocument();
}

async function stopIfCapturedTab(closedTabId) {
  try {
    const status = await readOffscreenStatus();
    if (!status.active || status.tabId !== closedTabId) return;
    await chrome.runtime.sendMessage({
      scope: "capture-offscreen",
      type: "stop"
    });
    await closeOffscreenDocument();
  } catch (_) {
  }
}

async function readOffscreenStatus() {
  const contexts = await chrome.runtime.getContexts({
    contextTypes: ["OFFSCREEN_DOCUMENT"],
    documentUrls: [chrome.runtime.getURL(OFFSCREEN_DOCUMENT)]
  });
  if (contexts.length === 0) {
    return { active: false, tabId: null };
  }

  try {
    const response = await chrome.runtime.sendMessage({
      scope: "capture-offscreen",
      type: "status"
    });
    return response || { active: false, tabId: null };
  } catch (_) {
    return { active: false, tabId: null };
  }
}

async function ensureOffscreenDocument() {
  const status = await readOffscreenStatus();
  if (status.ok) return;
  if (creatingOffscreen) return creatingOffscreen;

  creatingOffscreen = chrome.offscreen.createDocument({
    url: OFFSCREEN_DOCUMENT,
    reasons: ["USER_MEDIA"],
    justification: "Measure captured tab audio without storing or forwarding PCM."
  });
  try {
    await creatingOffscreen;
  } finally {
    creatingOffscreen = null;
  }
}

async function closeOffscreenDocument() {
  try {
    const contexts = await chrome.runtime.getContexts({
      contextTypes: ["OFFSCREEN_DOCUMENT"],
      documentUrls: [chrome.runtime.getURL(OFFSCREEN_DOCUMENT)]
    });
    if (contexts.length > 0) await chrome.offscreen.closeDocument();
  } catch (_) {
  }
}

async function reportStartError(tabId, error) {
  const detail = normalizeDetail({
    active: false,
    timestamp: Date.now(),
    audioContextState: "closed",
    error: error instanceof Error ? error.message : String(error)
  });
  updateBadge(tabId, detail);
  await dispatchToPage(tabId, detail);
}

function updateBadge(tabId, detail) {
  if (!Number.isInteger(tabId)) return;

  let text = "";
  let color = "#5f6368";
  if (detail.error) {
    text = "ERR";
    color = "#b3261e";
  } else if (detail.active && detail.nonzeroRatio > 0) {
    text = "PCM";
    color = "#137333";
  } else if (detail.active) {
    text = "SIL";
  }

  chrome.action.setBadgeBackgroundColor({ tabId, color }).catch(() => {});
  chrome.action.setBadgeText({ tabId, text }).catch(() => {});
  const title = detail.error
    ? `捕获失败：${detail.error}`
    : detail.active
      ? `${text}: RMS ${detail.rms.toFixed(6)}, peak ${detail.peak.toFixed(6)}`
      : "开始或停止检测此标签页的 PCM";
  chrome.action.setTitle({ tabId, title }).catch(() => {});
}

async function clearBadge(tabId) {
  if (!Number.isInteger(tabId)) return;
  await chrome.action.setBadgeText({ tabId, text: "" }).catch(() => {});
  await chrome.action.setTitle({
    tabId,
    title: "开始或停止检测此标签页的 PCM"
  }).catch(() => {});
}

async function dispatchToPage(tabId, detail) {
  if (!Number.isInteger(tabId)) return;
  try {
    await chrome.scripting.executeScript({
      target: { tabId },
      world: "MAIN",
      func: (eventName, eventDetail) => {
        window.dispatchEvent(new CustomEvent(eventName, {
          detail: eventDetail
        }));
      },
      args: [EVENT_NAME, normalizeDetail(detail)]
    });
  } catch (_) {
  }
}

function normalizeDetail(value = {}) {
  return {
    active: Boolean(value.active),
    sampleRate: finiteNumber(value.sampleRate),
    channels: finiteNumber(value.channels),
    rms: finiteNumber(value.rms),
    peak: finiteNumber(value.peak),
    nonzeroRatio: finiteNumber(value.nonzeroRatio),
    frames: finiteNumber(value.frames),
    elapsedMs: finiteNumber(value.elapsedMs),
    nonFiniteSamples: finiteNumber(value.nonFiniteSamples),
    timestamp: finiteNumber(value.timestamp) || Date.now(),
    audioContextState: String(value.audioContextState || "closed"),
    error: String(value.error || "")
  };
}

function finiteNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
}

function isOffscreenMessage(message, sender) {
  return Boolean(
    message &&
    message.scope === "capture-worker" &&
    sender &&
    sender.url === chrome.runtime.getURL(OFFSCREEN_DOCUMENT)
  );
}
