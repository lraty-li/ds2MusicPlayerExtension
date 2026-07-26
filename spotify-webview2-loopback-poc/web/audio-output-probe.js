import {
  buildProbeState,
  formatFrameDetails,
  formatFrameLog,
  frameFingerprint,
  maximum
} from "./audio-probe-state.js";

const CHANNEL = "ds2-audio-frame-probe-v1";
const byId = (id) => document.getElementById(id);
const frameReports = new Map();
const reportFingerprints = new Map();
const directLogTimes = new Map();
let desiredMode = "default";
let commandSequence = 0;
let logMessage = () => {};
let installed = false;
let automaticHelperMode = false;
let automaticAttachScheduled = false;

export function initializeAudioOutputProbe(log) {
  if (installed) return;
  installed = true;
  logMessage = log;
  bindActions();
  window.addEventListener("message", handleFrameMessage);
  publishState();
  postCommand("report");
  window.setTimeout(() => logSnapshot("原生 frame 探针已安装"), 500);
}

export function enableAutomaticHelperAudio() {
  automaticHelperMode = true;
  setDesiredMode("none");
  scheduleAutomaticAttach();
  logMessage("FRAME_PROBE helper mode enabled");
}

function bindActions() {
  byId("set-silent-sink").addEventListener("click", () => {
    setDesiredMode("none");
  });
  byId("attach-media-graph").addEventListener("click", () => {
    postCommand("attach-media");
    logMessage(`FRAME_PROBE attach-media knownFrames=${frameReports.size}`);
    window.setTimeout(() => logSnapshot("媒体接管回报"), 1000);
  });
  byId("restore-page-sink").addEventListener("click", () => {
    setDesiredMode("default");
  });
  byId("refresh-audio-probe").addEventListener("click", () => {
    postCommand("report");
    window.setTimeout(() => logSnapshot("已刷新全部 frame"), 300);
  });
}

function handleFrameMessage(event) {
  const message = event.data;
  if (message?.channel !== CHANNEL || message.type !== "frame-report") return;
  const state = message.state;
  if (!state?.frameId) return;
  if (state.closed) {
    frameReports.delete(state.frameId);
    reportFingerprints.delete(state.frameId);
    logMessage(
      `FRAME_PROBE closed ${state.isTop ? "top" : "child"} ${state.origin}`
    );
    publishState();
    return;
  }

  frameReports.set(state.frameId, state);
  const fingerprint = frameFingerprint(state);
  if (reportFingerprints.get(state.frameId) !== fingerprint) {
    reportFingerprints.set(state.frameId, fingerprint);
    logMessage(formatFrameLog(state));
  }
  logDirectMetrics(state);
  if (desiredMode === "none" && state.desiredMode !== "none") {
    postCommandTo(event.source, "set-mode", "none");
  }
  scheduleAutomaticAttach();
  publishState();
}

function scheduleAutomaticAttach() {
  if (!automaticHelperMode ||
      automaticAttachScheduled ||
      !hasUnattachedMedia()) {
    return;
  }
  automaticAttachScheduled = true;
  window.setTimeout(() => {
    automaticAttachScheduled = false;
    if (!hasUnattachedMedia()) return;
    postCommand("attach-media");
    logMessage(
      `FRAME_PROBE auto attach-media knownFrames=${frameReports.size}`
    );
  }, 50);
}

function hasUnattachedMedia() {
  return [...frameReports.values()].some((state) =>
    state.media?.some(
      ({ graphState }) => graphState === "none"
    )
  );
}

function setDesiredMode(mode) {
  desiredMode = mode;
  publishState();
  postCommand("set-mode", mode);
  logMessage(
    `FRAME_PROBE command mode=${mode} ` +
    `knownFrames=${frameReports.size}`
  );
  window.setTimeout(
    () => logSnapshot(mode === "none" ? "silent sink 回报" : "默认输出回报"),
    700
  );
}

function postCommand(action, mode) {
  window.postMessage(createCommand(action, mode), "*");
}

function postCommandTo(target, action, mode) {
  try {
    target?.postMessage(createCommand(action, mode), "*");
  } catch (error) {
    logMessage(`FRAME_PROBE command failed: ${error.message}`);
  }
}

function createCommand(action, mode) {
  return {
    channel: CHANNEL,
    type: "command",
    action,
    mode,
    sequence: ++commandSequence
  };
}

function buildState() {
  return buildProbeState(frameReports, desiredMode);
}

function publishState() {
  const state = buildState();
  window.__pocAudioOutputState = state;
  setText(
    "audio-sink-support",
    `AudioContext ${state.contextSetSinkSupported ? "支持" : "未报告"} · ` +
    `MediaElement ${state.mediaSetSinkSupported ? "支持" : "未报告"}`
  );
  setText("audio-context-count", state.contexts.length);
  setText(
    "media-element-count",
    `${state.mediaCount} · 播放中 ${state.playingMedia}`
  );
  setText(
    "media-graph-state",
    `${state.attachedMedia} 已接管 · ${state.graphErrors} 失败`
  );
  setText("direct-rms", state.directRms.toFixed(7));
  setText("direct-peak", state.directPeak.toFixed(7));
  setText("direct-nonzero", `${(state.directNonzero * 100).toFixed(3)}%`);
  setText(
    "child-frame-count",
    `${state.frames.length}/${state.expectedFrames} 已回报`
  );
  setText(
    "audio-sink-mode",
    state.desiredMode === "none" ? "silent / none" : "默认"
  );
  setText("audio-context-detail", formatFrameDetails(state.frames));
  byId("attach-media-graph").disabled =
    state.attachedMedia > 0 || state.graphErrors > 0;
  renderResult(state);
  window.dispatchEvent(
    new CustomEvent("poc-audio-output-state", { detail: state })
  );
}

function renderResult(state) {
  const result = byId("audio-probe-result");
  if (state.trackedSilent) {
    result.textContent = state.directHasPcm
      ? "silent sink 已生效，直接 Web Audio PCM 仍连续非零"
      : "silent sink 已生效，正在检查直接 Web Audio PCM";
    result.className = "status ok";
    return;
  }
  if (state.desiredMode !== "none") {
    result.textContent = "全部 frame 当前使用默认输出";
    result.className = "status neutral";
    return;
  }

  result.className = "status error";
  if (!state.coverageComplete) {
    result.textContent = "尚有 frame 未回报，不能判定已静音";
  } else if (state.contexts.length === 0 && state.mediaCount === 0) {
    result.textContent = "全部 frame 均未发现 AudioContext 或媒体元素";
  } else if (state.playingMedia > 0) {
    result.textContent = state.attachedMedia > 0
      ? "媒体已接入 Web Audio，正在等待上下文切换"
      : "先将播放中的媒体元素接入 Web Audio";
  } else if (state.contextErrors > 0) {
    result.textContent = "至少一个 AudioContext 设置 silent sink 失败";
  } else {
    result.textContent = "正在等待各 frame 应用 silent sink";
  }
}

function logDirectMetrics(state) {
  const attached = state.media.filter(
    ({ graphState }) => graphState === "attached"
  );
  if (attached.length === 0) return;
  const now = Date.now();
  if (now - (directLogTimes.get(state.frameId) || 0) < 1000) return;
  directLogTimes.set(state.frameId, now);
  const rms = maximum(attached.map(({ directRms }) => directRms));
  const peak = maximum(attached.map(({ directPeak }) => directPeak));
  const ratio = maximum(attached.map(({ directNonzero }) => directNonzero));
  const windows = maximum(attached.map(({ directWindows }) => directWindows));
  logMessage(
    `FRAME_PCM origin=${state.origin} rms=${rms.toFixed(7)} ` +
    `peak=${peak.toFixed(7)} nonzero=${ratio.toFixed(7)} ` +
    `windows=${windows} desired=${state.desiredMode}`
  );
}

function logSnapshot(prefix) {
  const state = buildState();
  logMessage(
    `${prefix}：frames=${state.frames.length}/${state.expectedFrames}, ` +
    `contexts=${state.contexts.length}, media=${state.mediaCount}, ` +
    `graphs=${state.attachedMedia}/${state.graphErrors}, ` +
    `directRms=${state.directRms.toFixed(7)}, ` +
    `directPeak=${state.directPeak.toFixed(7)}, ` +
    `pcm=${state.pcmBridgeState}/${state.pcmBridgeMethod || "none"}, ` +
    `transport=${state.pcmTransport || "none"}, ` +
    `ringFrames=${state.sharedRingFrames}, ringDrops=${state.pcmRingDrops}, ` +
    `pcmChunks=${state.pcmChunks}, ` +
    `desired=${state.desiredMode}, trackedSilent=${state.trackedSilent}`
  );
}

function setText(id, value) {
  byId(id).textContent = String(value);
}
