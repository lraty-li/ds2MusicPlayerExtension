const OFFSCREEN_DOCUMENT = "offscreen.html";
const RECORD_MS = 5000;

let state = {
  recording: false,
  tabId: null,
  startedAt: 0
};

chrome.action.onClicked.addListener(async (tab) => {
  if (state.recording) {
    await chrome.runtime.sendMessage({ type: "stop-recording" });
    return;
  }

  if (!tab || typeof tab.id !== "number") {
    setBadge("ERR", "#8b0000");
    return;
  }

  try {
    await ensureOffscreenDocument();
    const streamId = await chrome.tabCapture.getMediaStreamId({
      targetTabId: tab.id
    });

    const fileName = makeFileName(tab.title || "tab-audio");
    await chrome.runtime.sendMessage({
      type: "start-recording",
      streamId,
      durationMs: RECORD_MS,
      fileName
    });

    state = {
      recording: true,
      tabId: tab.id,
      startedAt: Date.now()
    };
    setBadge("REC", "#b00020");
  } catch (error) {
    state.recording = false;
    setBadge("ERR", "#8b0000");
    console.error("Failed to start tab recording:", error);
  }
});

chrome.runtime.onMessage.addListener((message) => {
  if (!message || typeof message.type !== "string") {
    return;
  }

  if (message.type === "recording-finished") {
    finishDownload(message).catch((error) => {
      setBadge("ERR", "#8b0000");
      console.error("Failed to save tab recording:", error);
    });
    return true;
  }

  if (message.type === "recording-error") {
    state.recording = false;
    setBadge("ERR", "#8b0000");
    console.error("Tab recording error:", message.error);
  }
});

async function finishDownload(message) {
  state = {
    recording: false,
    tabId: null,
    startedAt: 0
  };

  if (!message.dataUrl || !message.bytes) {
    throw new Error("Recording is empty.");
  }

  await chrome.downloads.download({
    url: message.dataUrl,
    filename: message.fileName || "ds2-tab-audio.webm",
    saveAs: false
  });

  setBadge("OK", "#006b3c");
  clearBadgeLater();
}

async function ensureOffscreenDocument() {
  const documentUrl = chrome.runtime.getURL(OFFSCREEN_DOCUMENT);
  const contexts = await chrome.runtime.getContexts({
    contextTypes: ["OFFSCREEN_DOCUMENT"],
    documentUrls: [documentUrl]
  });

  if (contexts.length > 0) {
    return;
  }

  await chrome.offscreen.createDocument({
    url: OFFSCREEN_DOCUMENT,
    reasons: ["USER_MEDIA"],
    justification: "Capture tab audio and write a local test recording."
  });
}

function makeFileName(title) {
  const safeTitle = title
    .replace(/[\\/:*?"<>|]+/g, "_")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 80);

  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  return `ds2-tab-audio-${stamp}-${safeTitle || "tab"}.webm`;
}

function setBadge(text, color) {
  chrome.action.setBadgeText({ text });
  chrome.action.setBadgeBackgroundColor({ color });
}

function clearBadgeLater() {
  setTimeout(() => {
    if (!state.recording) {
      chrome.action.setBadgeText({ text: "" });
    }
  }, 5000);
}
