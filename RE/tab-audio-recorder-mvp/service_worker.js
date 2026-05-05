const OFFSCREEN_DOCUMENT = "offscreen.html";
const BRIDGE_URL = "ws://127.0.0.1:47832";

let state = {
  streaming: false,
  tabId: null
};

chrome.action.onClicked.addListener(async (tab) => {
  if (state.streaming) {
    await chrome.runtime.sendMessage({ type: "stop-stream" });
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

    const response = await chrome.runtime.sendMessage({
      type: "start-stream",
      streamId,
      bridgeUrl: BRIDGE_URL
    });
    if (!response || !response.ok) {
      throw new Error(response && response.error ? response.error : "stream start failed");
    }

    state = { streaming: true, tabId: tab.id };
    setBadge("PCM", "#0057b8");
  } catch (error) {
    state.streaming = false;
    setBadge("ERR", "#8b0000");
    console.error("Failed to start PCM stream:", error);
  }
});

chrome.runtime.onMessage.addListener((message) => {
  if (!message || typeof message.type !== "string") {
    return;
  }

  if (message.type === "stream-stopped") {
    state = { streaming: false, tabId: null };
    setBadge("OK", "#006b3c");
    clearBadgeLater();
  } else if (message.type === "stream-error") {
    state = { streaming: false, tabId: null };
    setBadge("ERR", "#8b0000");
    console.error("PCM stream error:", message.error);
  }
});

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
    justification: "Capture tab audio and stream PCM to the local DS2 bridge."
  });
}

function setBadge(text, color) {
  chrome.action.setBadgeText({ text });
  chrome.action.setBadgeBackgroundColor({ color });
}

function clearBadgeLater() {
  setTimeout(() => {
    if (!state.streaming) {
      chrome.action.setBadgeText({ text: "" });
    }
  }, 5000);
}
