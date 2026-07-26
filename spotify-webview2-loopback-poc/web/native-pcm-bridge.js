const CHANNEL = "ds2-audio-frame-probe-v1";
const byId = (id) => document.getElementById(id);
let frameState = {};
let nativeMetrics = {};
let logMessage = () => {};
let lastVerdict = "";
let installed = false;

export function initializeNativePcmBridge(log) {
  if (installed) return;
  installed = true;
  logMessage = log;
  window.addEventListener("message", relayPcmChunk);
  window.addEventListener("poc-audio-output-state", ({ detail }) => {
    frameState = detail || {};
    renderFrameState();
    renderVerdict();
  });
  window.chrome?.webview?.addEventListener("message", ({ data }) => {
    if (data?.type !== "pcm-bridge-metrics") return;
    nativeMetrics = data;
    renderNativeMetrics();
    renderVerdict();
  });
}

function relayPcmChunk({ data }) {
  if (data?.channel !== CHANNEL || data.type !== "pcm-chunk") return;
  if (!validChunkEnvelope(data) || !window.chrome?.webview) return;
  window.chrome.webview.postMessage([
    "pcm-v1",
    data.streamId,
    data.sequence,
    data.sampleRate,
    data.channels,
    data.frames,
    data.payload
  ].join("|"));
}

function validChunkEnvelope(data) {
  const integer = (value, minimum, maximum) =>
    Number.isSafeInteger(value) && value >= minimum && value <= maximum;
  return (
    typeof data.streamId === "string" &&
    /^[a-z0-9._-]{1,96}$/i.test(data.streamId) &&
    integer(data.sequence, 0, Number.MAX_SAFE_INTEGER) &&
    integer(data.sampleRate, 8000, 192000) &&
    integer(data.channels, 1, 8) &&
    integer(data.frames, 1, 48000) &&
    typeof data.payload === "string" &&
    data.payload.length > 0 &&
    data.payload.length <= 1048576
  );
}

function renderFrameState() {
  const state = frameState.pcmBridgeState || "idle";
  const labels = {
    idle: "等待媒体接管",
    starting: "正在创建",
    ready: "已就绪，等待 PCM",
    streaming: "持续发送中",
    error: "失败"
  };
  setText("pcm-frame-state", labels[state] || state);
  setText(
    "pcm-method",
    frameState.pcmBridgeMethod
      ? `${frameState.pcmBridgeMethod}` +
        `${frameState.pcmBridgeNote ? "（已回退）" : ""}`
      : "—"
  );
  setText(
    "pcm-web-chunks",
    `${Number(frameState.pcmChunks || 0).toLocaleString()} 块 · ` +
    `${Number(frameState.pcmFrames || 0).toLocaleString()} 帧`
  );
}

function renderNativeMetrics() {
  const metrics = nativeMetrics;
  setText(
    "pcm-native-state",
    metrics.active ? "原生接收中" : "等待数据"
  );
  setText(
    "pcm-native-format",
    metrics.sampleRate
      ? `${metrics.sampleRate} Hz · ${metrics.channels} ch · PCM16`
      : "—"
  );
  setText(
    "pcm-native-total",
    `${Number(metrics.totalChunks || 0).toLocaleString()} 块 · ` +
    `${Number(metrics.totalFrames || 0).toLocaleString()} 帧`
  );
  setText(
    "pcm-throughput",
    `${Number(metrics.framesPerSecond || 0).toFixed(1)} 帧/s · ` +
    `${formatBytes(metrics.bytesPerSecond || 0)}/s`
  );
  setText(
    "pcm-continuity",
    `缺口 ${metrics.sequenceGaps || 0} · ` +
    `乱序 ${metrics.outOfOrder || 0} · ` +
    `无效 ${metrics.invalidChunks || 0}`
  );
  setText("pcm-native-rms", Number(metrics.rms || 0).toFixed(7));
  setText("pcm-native-peak", Number(metrics.peak || 0).toFixed(7));
  setText("pcm-checksum", metrics.checksum || "—");
}

function renderVerdict() {
  if (frameState.pcmBridgeState === "error") {
    verdict(
      "Web PCM 桥接失败",
      "fail",
      frameState.pcmBridgeError || "未报告错误"
    );
    return;
  }
  if (!nativeMetrics.active) {
    verdict(
      "等待连续 PCM 到达原生 C++",
      "idle",
      "播放 Spotify 后点击“接管媒体并启动 PCM 桥接”。"
    );
    return;
  }
  if (nativeMetrics.error ||
      Number(nativeMetrics.invalidChunks || 0) > 0) {
    verdict(
      "原生 PCM 分块校验失败",
      "fail",
      nativeMetrics.error ||
        `无效块 ${nativeMetrics.invalidChunks}`
    );
    return;
  }
  if (Number(nativeMetrics.sequenceGaps || 0) > 0 ||
      Number(nativeMetrics.outOfOrder || 0) > 0) {
    verdict(
      "PCM 流发生不连续",
      "fail",
      `缺口 ${nativeMetrics.sequenceGaps}，乱序 ${nativeMetrics.outOfOrder}`
    );
    return;
  }
  if (Number(nativeMetrics.totalChunks || 0) < 10) {
    verdict(
      "原生已收到 PCM，正在积累连续窗口",
      "running",
      "至少收到 10 个分块后再判定。"
    );
    return;
  }
  if (nativeMetrics.continuous &&
      nativeMetrics.throughputOk &&
      nativeMetrics.hasPcm) {
    verdict(
      "关键通过：原生 C++ 持续收到完整 PCM",
      "pass",
      "序号连续、吞吐量符合 48 kHz 双声道 PCM16，且样本非零。"
    );
    return;
  }
  if (nativeMetrics.continuous && nativeMetrics.throughputOk) {
    verdict(
      "PCM 传输连续，当前窗口为静音",
      "running",
      "保持曲目播放，等待非零音频窗口。"
    );
    return;
  }
  verdict(
    "PCM 桥接已建立，吞吐量尚未稳定",
    "running",
    `当前 ${Number(nativeMetrics.framesPerSecond || 0).toFixed(1)} 帧/s`
  );
}

function verdict(title, kind, detail) {
  const element = byId("pcm-bridge-verdict");
  element.textContent = title;
  element.className = `verdict ${kind}`;
  setText("pcm-bridge-detail", detail);
  const fingerprint = `${kind}:${title}`;
  if (lastVerdict !== fingerprint) {
    lastVerdict = fingerprint;
    logMessage(`NATIVE_PCM ${title}：${detail}`);
  }
}

function formatBytes(value) {
  const bytes = Number(value || 0);
  return bytes >= 1024
    ? `${(bytes / 1024).toFixed(1)} KiB`
    : `${bytes.toFixed(0)} B`;
}

function setText(id, value) {
  byId(id).textContent = String(value);
}
