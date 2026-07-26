const byId = (id) => document.getElementById(id);
let hostState = {};
let audioContext = null;
let oscillator = null;
let gainNode = null;
let consecutiveNonzero = 0;
let consecutiveMutedSilence = 0;
let capturedBeforeMute = false;
let pageSinkState = { desiredMode: "default", trackedSilent: false };

export function initializeHostProbe(log) {
  window.addEventListener("poc-audio-output-state", ({ detail }) => {
    const suppressionChanged =
      pageSinkState.trackedSilent !== !!detail?.trackedSilent ||
      pageSinkState.desiredMode !== detail?.desiredMode;
    pageSinkState = detail || pageSinkState;
    if (suppressionChanged) {
      consecutiveNonzero = 0;
      consecutiveMutedSilence = 0;
    }
  });
  byId("toggle-mute").addEventListener("click", () => post("toggle-mute"));
  byId("toggle-session-mute").addEventListener(
    "click", () => post("toggle-session-mute")
  );
  byId("open-devtools").addEventListener("click", () => post("open-devtools"));
  byId("test-tone").addEventListener("click", () => {
    toggleTone(log).catch((error) => log(`本地音调失败：${error.message}`));
  });
  window.__pocToggleTone = () =>
    toggleTone(log).catch((error) => log(`本地音调失败：${error.message}`));

  if (!window.chrome?.webview) {
    setText("capture-state", "不是 WebView2 宿主");
    return;
  }
  window.chrome.webview.addEventListener("message", ({ data }) => {
    if (data?.type === "host-state") renderHostState(data);
    if (data?.type === "capture-metrics") renderMetrics(data);
  });
  post("request-host-state");
  probeEme(log);
}

function post(command) {
  window.chrome?.webview?.postMessage(command);
}

function renderHostState(state) {
  hostState = state;
  const outputSuppressed = isOutputSuppressed();
  if (!outputSuppressed && !state.documentPlayingAudio) {
    capturedBeforeMute = false;
  }
  setText("runtime-version", state.runtime || "—");
  setText("helper-pid", state.helperPid || "—");
  setText("browser-pid", state.browserPid || "—");
  setText("capture-target-pid", state.captureTargetPid || "—");
  setText("proxy-state", state.proxyServer || "系统代理");
  setText("muted-state", state.muted ? "是（会切断 PCM）" : "否");
  setText(
    "session-muted-state",
    state.sessionMuted
      ? `是 · ${state.sessionMuteCount || 0} 个会话`
      : Number(state.sessionMuteResult || 0) === 0
        ? "否"
        : `失败 · ${formatHresult(state.sessionMuteResult)}`
  );
  setText("doc-audio", state.documentPlayingAudio ? "正在播放" : "未播放");
  setText(
    "capture-state",
    state.captureActive
      ? "运行中 · WebView2 浏览器进程树"
      : `未运行 · HRESULT ${formatHresult(state.captureResult)}`
  );
  byId("toggle-mute").textContent =
    state.muted ? "取消 WebView2 内部静音" : "WebView2 内部静音（对照）";
  byId("toggle-session-mute").textContent =
    state.sessionMuted ? "恢复 Windows 音频会话" : "静音 Windows 音频会话";
  byId("toggle-mute").disabled = !!state.sessionMuted;
  byId("toggle-session-mute").disabled = !!state.muted;
}

function renderMetrics(metrics) {
  const rms = Number(metrics.rms || 0);
  const peak = Number(metrics.peak || 0);
  const ratio = Number(metrics.nonzeroRatio || 0);
  const hasPcm = peak > 0.0001 && ratio > 0.001;
  const outputSuppressed = isOutputSuppressed();
  if (!outputSuppressed && hostState.documentPlayingAudio && hasPcm) {
    capturedBeforeMute = true;
  }
  consecutiveNonzero = hasPcm ? consecutiveNonzero + 1 : 0;
  consecutiveMutedSilence =
    outputSuppressed && hostState.documentPlayingAudio && !hasPcm
      ? consecutiveMutedSilence + 1
      : 0;

  setText("metric-rate", `${metrics.sampleRate || 0} Hz / ${metrics.channels || 0} ch`);
  setText("metric-rms", rms.toFixed(7));
  setText("metric-peak", peak.toFixed(7));
  setText("metric-nonzero", `${(ratio * 100).toFixed(3)}%`);
  setText("metric-frames", Number(metrics.totalFrames || 0).toLocaleString());
  setText("metric-silent", metrics.silentPackets || 0);
  setText("metric-discontinuity", metrics.discontinuities || 0);
  setText("metric-timestamp", metrics.timestampErrors || 0);

  if (Number(metrics.error || 0) !== 0) {
    verdict("进程回环捕获失败", "fail",
      `HRESULT ${formatHresult(metrics.error)}`);
  } else if (pageSinkState.trackedSilent && pageSinkState.directHasPcm) {
    verdict("关键通过：silent sink 后直接 PCM 仍连续非零", "pass",
      `Web Audio RMS ${Number(pageSinkState.directRms || 0).toFixed(7)}；` +
      "Process Loopback 是否归零不再决定可行性。");
  } else if (outputSuppressed && consecutiveNonzero >= 3) {
    const method = suppressionMethod();
    verdict("关键通过：输出静音后仍有连续 PCM", "pass",
      `${method}下桌面无声，但进程回环仍取得 PCM。`);
  } else if (consecutiveMutedSilence >= 3 && capturedBeforeMute) {
    verdict("关键失败：宿主静音后 PCM 同时归零", "fail",
      "同一播放流在静音前存在 PCM，静音后消失。");
  } else if (consecutiveMutedSilence >= 3) {
    verdict("静音前未捕获到有效 PCM", "fail",
      "当前不能归因于静音；请先修复捕获目标或音频进程覆盖范围。");
  } else if (consecutiveNonzero >= 3) {
    verdict("已持续捕获到 48 kHz 双声道 PCM", "running",
      "现在可应用 AudioContext silent sink，观察 PCM 是否继续非零。");
  } else {
    verdict("捕获运行中，等待页面产生声音", "idle",
      "可先使用本地 440 Hz 音调排除 Spotify/DRM 变量。");
  }
}

function isOutputSuppressed() {
  return !!(
    hostState.muted ||
    hostState.sessionMuted ||
    pageSinkState.trackedSilent
  );
}

function suppressionMethod() {
  if (pageSinkState.trackedSilent) return "AudioContext silent sink";
  if (hostState.sessionMuted) return "Windows 音频会话静音";
  return "WebView2 内部静音";
}

async function toggleTone(log) {
  if (oscillator) {
    oscillator.stop();
    oscillator.disconnect();
    gainNode.disconnect();
    oscillator = null;
    gainNode = null;
    byId("test-tone").textContent = "播放本地 440 Hz 音调";
    log("本地测试音调已停止");
    return;
  }
  audioContext ||= new AudioContext({ sampleRate: 48000 });
  log(`准备本地音调，AudioContext=${audioContext.state}`);
  await audioContext.resume();
  if (audioContext.state !== "running") {
    throw new Error(`AudioContext 未运行：${audioContext.state}`);
  }
  oscillator = audioContext.createOscillator();
  gainNode = audioContext.createGain();
  oscillator.frequency.value = 440;
  gainNode.gain.value = 0.05;
  oscillator.connect(gainNode).connect(audioContext.destination);
  oscillator.start();
  byId("test-tone").textContent = "停止本地测试音调";
  log(`本地音调已启动，AudioContext=${audioContext.sampleRate} Hz / running`);
}

async function probeEme(log) {
  if (!navigator.requestMediaKeySystemAccess) {
    setText("eme-state", "不支持 EME");
    return;
  }
  try {
    await navigator.requestMediaKeySystemAccess("com.widevine.alpha", [{
      initDataTypes: ["cenc"],
      audioCapabilities: [{ contentType: 'audio/mp4; codecs="mp4a.40.2"' }]
    }]);
    setText("eme-state", "Widevine 可用");
    log("EME 探测通过：com.widevine.alpha");
  } catch (error) {
    setText("eme-state", `Widevine 不可用：${error.name}`);
    log(`EME 探测失败：${error.message}`);
  }
}

function verdict(title, kind, detail) {
  const element = byId("capture-verdict");
  element.textContent = title;
  element.className = `verdict ${kind}`;
  setText("capture-detail", detail);
}

function formatHresult(value) {
  return `0x${Number(value || 0).toString(16).padStart(8, "0")}`;
}

function setText(id, value) {
  byId(id).textContent = String(value);
}
