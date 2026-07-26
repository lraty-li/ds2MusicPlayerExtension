const OFFSCREEN_DOCUMENT = "offscreen.html";

async function readCaptureHostStatus() {
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

async function startCaptureHost(tabId, streamUrl) {
  const streamId = await chrome.tabCapture.getMediaStreamId({ targetTabId: tabId });
  await ensureOffscreenDocument();
  const response = await chrome.runtime.sendMessage({
    type: "start-stream",
    tabId,
    streamId,
    streamUrl
  });
  if (!response || !response.ok) {
    throw new Error(response && response.error ? response.error : "stream start failed");
  }
}

async function stopCaptureHost() {
  try {
    await chrome.runtime.sendMessage({ type: "stop-stream" });
  } catch (_) {
  }
}

async function claimCaptureHost() {
  const response = await chrome.runtime.sendMessage({ type: "claim-source" });
  return !!(response && response.ok);
}

async function ensureOffscreenDocument() {
  const status = await readCaptureHostStatus();
  if (status.exists) return;

  await chrome.offscreen.createDocument({
    url: OFFSCREEN_DOCUMENT,
    reasons: ["USER_MEDIA"],
    justification: "Capture tab audio and stream PCM to the local DS2 runtime plugin."
  });
}
