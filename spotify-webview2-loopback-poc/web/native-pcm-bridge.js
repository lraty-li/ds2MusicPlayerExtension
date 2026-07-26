import { initializeNativePcmRelay } from "./pcm-relay.js";

const byId = (id) => document.getElementById(id);
let frameState = {};
let nativeMetrics = {};
let hostState = {};
let logMessage = () => {};
let lastVerdict = "";
let installed = false;

export function initializeNativePcmBridge(log) {
  if (installed) return;
  installed = true;
  logMessage = log;
  initializeNativePcmRelay();
  window.addEventListener("poc-audio-output-state", ({ detail }) => {
    frameState = detail || {};
    renderFrameState();
    renderVerdict();
  });
  window.chrome?.webview?.addEventListener("message", ({ data }) => {
    if (data?.type === "host-state") {
      hostState = data;
      renderHostRingState();
      renderVerdict();
    }
    if (data?.type !== "pcm-bridge-metrics") return;
    nativeMetrics = data;
    renderNativeMetrics();
    renderVerdict();
  });
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
    "pcm-frame-ring",
    frameState.sharedRingFrames > 0
      ? `${frameState.sharedRingFrames} 个 frame 已映射`
      : frameState.sharedRingError || "尚未映射"
  );
  setText("pcm-transport", frameState.pcmTransport || "—");
  setText("pcm-ring-drops", frameState.pcmRingDrops || 0);
  setText(
    "pcm-web-chunks",
    `${Number(frameState.pcmChunks || 0).toLocaleString()} 块 · ` +
    `${Number(frameState.pcmFrames || 0).toLocaleString()} 帧`
  );
}

function renderHostRingState() {
  if (hostState.sharedRingReady) {
    setText(
      "pcm-shared-host",
      `已创建 · 已投递 ${hostState.sharedRingPostCount || 0} 次`
    );
  } else {
    setText(
      "pcm-shared-host",
      `不可用 · HRESULT ${formatHresult(hostState.sharedRingResult)}`
    );
  }
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
    "pcm-native-transport",
    metrics.transport || "—"
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
  if (hostState.sharedRingReady === false) {
    verdict(
      "原生共享缓冲区不可用",
      "fail",
      `HRESULT ${formatHresult(hostState.sharedRingResult)}`
    );
    return;
  }
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
      Number(nativeMetrics.outOfOrder || 0) > 0 ||
      Number(frameState.pcmRingDrops || 0) > 0) {
    verdict(
      "PCM 流发生不连续",
      "fail",
      `缺口 ${nativeMetrics.sequenceGaps}，乱序 ${nativeMetrics.outOfOrder}，` +
      `环形缓冲丢块 ${frameState.pcmRingDrops || 0}`
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
      nativeMetrics.hasPcm &&
      nativeMetrics.transport === "shared-ring") {
    verdict(
      "关键通过：共享内存环持续送达完整 PCM",
      "pass",
      "无 Base64 音频负载；序号连续、吞吐量正确且样本非零。"
    );
    return;
  }
  if (nativeMetrics.continuous &&
      nativeMetrics.transport !== "shared-ring") {
    verdict(
      "PCM 正常，但仍在使用 Base64 回退",
      "fail",
      "共享缓冲区没有被 Spotify frame 接管。"
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

function formatHresult(value) {
  return `0x${Number(value || 0).toString(16).padStart(8, "0")}`;
}

function setText(id, value) {
  byId(id).textContent = String(value);
}
