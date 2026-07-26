let streamUrl = null;
let targetTabId = null;
let connectTimer = null;
let streamToken = 0;
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

  if (message.type === "get-status") {
    sendResponse({
      active: isAudioGraphActive(),
      connected: isStreamSocketOpen(),
      owned: isStreamSourceOwned(),
      tabId: targetTabId
    });
    return true;
  }

  if (message.type === "claim-source") {
    sendResponse({ ok: claimStreamSource("capture_reselected") });
    return true;
  }

  if (message.type === "metadata-update") {
    sendResponse(sendMetadata(message.metadata));
    return true;
  }

  if (message.type === "jacket-update") {
    sendJacket(message.jacket).then(sendResponse);
    return true;
  }

  return false;
});

async function startStream(message) {
  stopStream(false);
  beginStreamSource();
  const token = ++streamToken;
  streamUrl = message.streamUrl;
  targetTabId = message.tabId;

  await startAudioGraph(message, (chunk) => sendAudioChunk(chunk, getAudioGraphSampleRate()));
  setStreamSocketClosedCallback(handleStreamSocketClosed);
  reportWaiting();
  scheduleConnect(0, token);
}

function scheduleConnect(delayMs, token = streamToken) {
  if (connectTimer || !isAudioGraphActive() || !streamUrl) {
    return;
  }

  connectTimer = setTimeout(() => {
    connectTimer = null;
    connectSocket(token);
  }, delayMs);
}

async function connectSocket(token) {
  if (token !== streamToken || getStreamSocket() || !isAudioGraphActive()) {
    return;
  }

  reportWaiting();
  try {
    const ws = await openSocketOnce(streamUrl);
    if (token !== streamToken || !isAudioGraphActive()) {
      ws.close();
      return;
    }

    attachStreamSocket(ws, handleSocketMessage);
    startMetadataPolling();
    chrome.runtime.sendMessage({ type: "stream-connected" });
  } catch (_) {
    if (token === streamToken && isAudioGraphActive()) {
      scheduleConnect(1000, token);
    }
  }
}

function handleStreamSocketClosed() {
  stopMetadataPolling();
  reportWaiting();
  scheduleConnect(1000);
}

function handleSocketMessage(data) {
  if (typeof data !== "string") {
    return;
  }

  let message = null;
  try {
    message = JSON.parse(data);
  } catch (_) {
    return;
  }

  if (message && message.type === "control") {
    if (message.reason === "source_preempted") {
      markStreamSourcePreempted();
    }
    chrome.runtime.sendMessage({
      type: "browser-control",
      tabId: targetTabId,
      command: message.command,
      reason: message.reason || ""
    });
  }
}

function stopStream(notify = true) {
  streamToken++;
  setStreamSocketClosedCallback(null);
  stopMetadataPolling();
  if (connectTimer) {
    clearTimeout(connectTimer);
    connectTimer = null;
  }
  stopAudioGraph();
  closeStreamSocket();
  resetStreamSourceState(true);

  streamUrl = null;
  targetTabId = null;
  resetStreamPacketSequence();

  if (notify) {
    chrome.runtime.sendMessage({ type: "stream-stopped" });
  }
}

function reportWaiting() {
  chrome.runtime.sendMessage({ type: "stream-waiting" });
}

function reportError(error) {
  stopStream(false);
  chrome.runtime.sendMessage({
    type: "stream-error",
    error: String(error)
  });
}

function getTargetTabId() {
  return targetTabId;
}
