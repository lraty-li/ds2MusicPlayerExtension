let capturedTabId = null;
let capturedStream = null;
let audioContext = null;
let sourceNode = null;
let metricsNode = null;
let stopping = false;
let captureStartedAt = 0;
let lastDetail = createDetail();

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (!message || message.scope !== "capture-offscreen") return false;

  handleMessage(message)
    .then(sendResponse)
    .catch((error) => {
      sendResponse({ ok: false, error: describeError(error) });
    });
  return true;
});

async function handleMessage(message) {
  if (message.type === "status") {
    return {
      ok: true,
      active: capturedStream !== null,
      tabId: capturedTabId,
      detail: lastDetail
    };
  }
  if (message.type === "start") {
    return startCapture(message);
  }
  if (message.type === "stop") {
    return stopCapture("");
  }
  throw new Error(`unknown offscreen message: ${message.type}`);
}

async function startCapture(message) {
  if (!Number.isInteger(message.tabId) || !message.streamId) {
    throw new Error("missing tab id or tabCapture stream id");
  }

  if (capturedStream) await stopCapture("");
  capturedTabId = message.tabId;
  stopping = false;

  try {
    capturedStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        mandatory: {
          chromeMediaSource: "tab",
          chromeMediaSourceId: message.streamId
        }
      },
      video: false
    });

    audioContext = new AudioContext({ sampleRate: 48000 });
    await audioContext.audioWorklet.addModule("capture-metrics-worklet.js");

    sourceNode = audioContext.createMediaStreamSource(capturedStream);
    metricsNode = new AudioWorkletNode(
      audioContext,
      "capture-metrics-processor",
      { numberOfInputs: 1, numberOfOutputs: 0 }
    );
    metricsNode.port.onmessage = handleMetrics;
    sourceNode.connect(metricsNode);

    const track = capturedStream.getAudioTracks()[0];
    if (!track) throw new Error("captured stream has no audio track");
    track.addEventListener("ended", handleTrackEnded, { once: true });

    try {
      await audioContext.resume();
    } catch (_) {
      // 状态会通过 audioContextState 暴露给宿主页。
    }

    captureStartedAt = performance.now();
    lastDetail = createDetail({
      active: true,
      sampleRate: audioContext.sampleRate,
      channels: readInitialChannelCount(track),
      audioContextState: audioContext.state
    });
    return { ok: true, tabId: capturedTabId, detail: lastDetail };
  } catch (error) {
    const failedTabId = capturedTabId;
    const detail = await tearDown(describeError(error));
    return {
      ok: false,
      tabId: failedTabId,
      detail,
      error: detail.error
    };
  }
}

function handleMetrics(event) {
  if (!capturedStream || !audioContext) return;

  const metrics = event.data || {};
  lastDetail = createDetail({
    active: true,
    sampleRate: audioContext.sampleRate,
    channels: metrics.channels,
    rms: metrics.rms,
    peak: metrics.peak,
    nonzeroRatio: metrics.nonzeroRatio,
    frames: metrics.frames,
    elapsedMs: captureStartedAt > 0
      ? performance.now() - captureStartedAt
      : metrics.elapsedMs,
    nonFiniteSamples: metrics.nonFiniteSamples,
    audioContextState: audioContext.state,
    error: finiteNumber(metrics.nonFiniteSamples) > 0
      ? `AudioWorklet observed ${finiteNumber(metrics.nonFiniteSamples)} non-finite samples`
      : ""
  });
  notifyWorker("metrics", capturedTabId, lastDetail);
}

async function stopCapture(errorMessage) {
  const stoppedTabId = capturedTabId;
  const detail = await tearDown(errorMessage);
  return { ok: errorMessage === "", tabId: stoppedTabId, detail };
}

async function tearDown(errorMessage) {
  stopping = true;
  const previous = lastDetail;
  const elapsedMs = captureStartedAt > 0
    ? performance.now() - captureStartedAt
    : previous.elapsedMs;

  if (metricsNode) {
    metricsNode.port.onmessage = null;
    metricsNode.disconnect();
  }
  if (sourceNode) sourceNode.disconnect();
  if (capturedStream) {
    for (const track of capturedStream.getTracks()) track.stop();
  }
  if (audioContext) {
    try {
      await audioContext.close();
    } catch (_) {
    }
  }

  capturedStream = null;
  audioContext = null;
  sourceNode = null;
  metricsNode = null;
  capturedTabId = null;
  captureStartedAt = 0;
  lastDetail = createDetail({
    ...previous,
    active: false,
    elapsedMs,
    timestamp: Date.now(),
    audioContextState: "closed",
    error: errorMessage
  });
  stopping = false;
  return lastDetail;
}

async function handleTrackEnded() {
  if (stopping || !capturedStream) return;
  const stoppedTabId = capturedTabId;
  const detail = await tearDown("captured audio track ended");
  notifyWorker("ended", stoppedTabId, detail);
}

function notifyWorker(type, tabId, detail) {
  chrome.runtime.sendMessage({
    scope: "capture-worker",
    type,
    tabId,
    detail
  }).catch(() => {});
}

function readInitialChannelCount(track) {
  const count = Number(track.getSettings().channelCount);
  return Number.isFinite(count) && count >= 0 ? count : 0;
}

function createDetail(overrides = {}) {
  return {
    active: Boolean(overrides.active),
    sampleRate: finiteNumber(overrides.sampleRate),
    channels: finiteNumber(overrides.channels),
    rms: finiteNumber(overrides.rms),
    peak: finiteNumber(overrides.peak),
    nonzeroRatio: finiteNumber(overrides.nonzeroRatio),
    frames: finiteNumber(overrides.frames),
    elapsedMs: finiteNumber(overrides.elapsedMs),
    nonFiniteSamples: finiteNumber(overrides.nonFiniteSamples),
    timestamp: finiteNumber(overrides.timestamp) || Date.now(),
    audioContextState: String(overrides.audioContextState || "closed"),
    error: String(overrides.error || "")
  };
}

function finiteNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
}

function describeError(error) {
  return error instanceof Error ? error.message : String(error);
}
