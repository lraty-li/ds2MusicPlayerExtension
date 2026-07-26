const byId = (id) => document.getElementById(id);
let hostState = {};
let audioContext = null;
let oscillator = null;
let gainNode = null;
let consecutiveNonzero = 0;
let consecutiveMutedSilence = 0;

export function initializeHostProbe(log) {
  byId("toggle-mute").addEventListener("click", () => post("toggle-mute"));
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
  setText("runtime-version", state.runtime || "—");
  setText("helper-pid", state.helperPid || "—");
  setText("browser-pid", state.browserPid || "—");
  setText("muted-state", state.muted ? "是（桌面应无声）" : "否（桌面可听）");
  setText("doc-audio", state.documentPlayingAudio ? "正在播放" : "未播放");
  setText(
    "capture-state",
    state.captureActive
      ? "运行中 · helper 进程树"
      : `未运行 · HRESULT ${formatHresult(state.captureResult)}`
  );
  byId("toggle-mute").textContent = state.muted ? "取消宿主静音" : "静音宿主";
}

function renderMetrics(metrics) {
  const rms = Number(metrics.rms || 0);
  const peak = Number(metrics.peak || 0);
  const ratio = Number(metrics.nonzeroRatio || 0);
  const hasPcm = peak > 0.0001 && ratio > 0.001;
  consecutiveNonzero = hasPcm ? consecutiveNonzero + 1 : 0;
  consecutiveMutedSilence =
    hostState.muted && hostState.documentPlayingAudio && !hasPcm
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
  } else if (hostState.muted && consecutiveNonzero >= 3) {
    verdict("关键通过：宿主静音后仍有连续 PCM", "pass",
      "这证明桌面不重复出声与进程回环可以同时成立。");
  } else if (consecutiveMutedSilence >= 3) {
    verdict("关键失败：宿主静音后 PCM 同时归零", "fail",
      "若真实 Spotify 也如此，就需要独立音频端点或虚拟音频设备。");
  } else if (consecutiveNonzero >= 3) {
    verdict("已持续捕获到 48 kHz 双声道 PCM", "running",
      "下一步点击“静音宿主”，观察 PCM 是否继续非零。");
  } else {
    verdict("捕获运行中，等待页面产生声音", "idle",
      "可先使用本地 440 Hz 音调排除 Spotify/DRM 变量。");
  }
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
