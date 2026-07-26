import {
  beginAuthorization,
  clearAuthorization,
  finishAuthorizationCallback,
  getValidAccessToken,
  hasStoredAuthorization,
  loadClientId,
  redirectUri,
  saveClientId
} from "./auth.js";

const byId = (id) => document.getElementById(id);
const ui = {
  clientId: byId("client-id"),
  redirectUri: byId("redirect-uri"),
  saveClient: byId("save-client"),
  copyRedirect: byId("copy-redirect"),
  authorize: byId("authorize"),
  clearAuth: byId("clear-auth"),
  authStatus: byId("auth-status"),
  enablePlayer: byId("enable-player"),
  disconnectPlayer: byId("disconnect-player"),
  sdkState: byId("sdk-state"),
  connectState: byId("connect-state"),
  deviceId: byId("device-id"),
  trackState: byId("track-state"),
  metricRate: byId("metric-rate"),
  metricChannels: byId("metric-channels"),
  metricRms: byId("metric-rms"),
  metricPeak: byId("metric-peak"),
  metricNonzero: byId("metric-nonzero"),
  metricWindows: byId("metric-windows"),
  captureVerdict: byId("capture-verdict"),
  captureDetail: byId("capture-detail"),
  log: byId("log")
};

let player = null;
let sdkLoadPromise = null;
let consecutiveNonzeroWindows = 0;

initialize().catch((error) => {
  setAuthStatus(error.message, "error");
  log(`初始化失败：${error.message}`);
});

async function initialize() {
  ui.clientId.value = loadClientId();
  ui.redirectUri.value = redirectUri();
  bindActions();
  bindCaptureMetrics();

  try {
    if (await finishAuthorizationCallback()) {
      setAuthStatus("Spotify 授权成功，token 已保存在当前浏览器", "ok");
      log("OAuth PKCE 回调完成");
    }
  } catch (error) {
    clearAuthorization();
    setAuthStatus(error.message, "error");
    log(error.message);
  }

  updateAuthorizationUi();
  if (hasStoredAuthorization()) await preparePlayer();
}

function bindActions() {
  ui.saveClient.addEventListener("click", () => {
    try {
      const previousClientId = loadClientId();
      const clientId = saveClientId(ui.clientId.value);
      if (previousClientId && previousClientId !== clientId) {
        disconnectPlayer();
        log("Client ID 已变更，旧播放器已断开");
      }
      ui.clientId.value = clientId;
      updateAuthorizationUi();
      log("Client ID 已保存");
    } catch (error) {
      setAuthStatus(error.message, "error");
    }
  });

  ui.copyRedirect.addEventListener("click", async () => {
    await navigator.clipboard.writeText(ui.redirectUri.value);
    log("Redirect URI 已复制");
  });

  ui.authorize.addEventListener("click", () => {
    beginAuthorization(ui.clientId.value).catch((error) => {
      setAuthStatus(error.message, "error");
      log(error.message);
    });
  });

  ui.clearAuth.addEventListener("click", () => {
    disconnectPlayer();
    clearAuthorization();
    setAuthStatus("本地 token 已清除", "neutral");
    updateAuthorizationUi();
    log("已清除本地授权");
  });

  ui.enablePlayer.addEventListener("click", enablePlayer);
  ui.disconnectPlayer.addEventListener("click", disconnectPlayer);
}

function bindCaptureMetrics() {
  window.addEventListener("ds2-spotify-capture-metrics", (event) => {
    renderCaptureMetrics(event.detail || {});
  });
}

function updateAuthorizationUi() {
  const hasClientId = !!loadClientId();
  const authorized = hasStoredAuthorization();
  ui.authorize.disabled = !hasClientId;
  ui.clearAuth.disabled = !authorized;

  if (authorized) {
    setAuthStatus("已保存可刷新的 Spotify 授权", "ok");
  } else if (hasClientId) {
    setAuthStatus("Client ID 已保存，下一步进行 Spotify 授权", "neutral");
  }
  if (!authorized) ui.enablePlayer.disabled = true;
}

async function preparePlayer() {
  ui.sdkState.textContent = "正在加载";
  await loadSpotifySdk();
  ui.sdkState.textContent = "已加载";

  player = new Spotify.Player({
    name: "Death Stranding 2 Web SDK PoC",
    volume: 1,
    enableMediaSession: true,
    getOAuthToken: (callback) => {
      getValidAccessToken()
        .then(callback)
        .catch((error) => {
          setAuthStatus(error.message, "error");
          log(`Token 获取失败：${error.message}`);
        });
    }
  });
  installPlayerListeners();
  ui.enablePlayer.disabled = false;
  log("Web Playback SDK 已初始化，等待用户启用");
}

function loadSpotifySdk() {
  if (window.Spotify) return Promise.resolve();
  if (sdkLoadPromise) return sdkLoadPromise;

  sdkLoadPromise = new Promise((resolve, reject) => {
    window.onSpotifyWebPlaybackSDKReady = resolve;
    const script = document.createElement("script");
    script.src = "https://sdk.scdn.co/spotify-player.js";
    script.async = true;
    script.onerror = () => reject(new Error("Spotify Web Playback SDK 加载失败"));
    document.head.appendChild(script);
  });
  return sdkLoadPromise;
}

function installPlayerListeners() {
  player.addListener("ready", ({ device_id: deviceId }) => {
    ui.connectState.textContent = "设备已就绪";
    ui.deviceId.textContent = deviceId;
    ui.disconnectPlayer.disabled = false;
    log(`Connect 设备已就绪：${deviceId}`);
  });
  player.addListener("not_ready", () => {
    ui.connectState.textContent = "设备离线";
    ui.deviceId.textContent = "—";
    log("Connect 设备进入 not_ready");
  });
  player.addListener("player_state_changed", renderPlaybackState);
  player.addListener("autoplay_failed", () => log("Chrome 阻止了自动播放，请再次点击启用"));
  for (const type of ["initialization_error", "authentication_error", "account_error", "playback_error"]) {
    player.addListener(type, ({ message }) => {
      ui.connectState.textContent = type;
      log(`${type}：${message}`);
    });
  }
}

async function enablePlayer() {
  if (!player) return;
  ui.enablePlayer.disabled = true;
  ui.connectState.textContent = "正在连接";
  try {
    await player.activateElement();
    const connected = await player.connect();
    if (!connected) throw new Error("player.connect() 返回 false");
    ui.connectState.textContent = "已连接，等待 ready";
    ui.disconnectPlayer.disabled = false;
    log("已调用 activateElement() 和 connect()");
  } catch (error) {
    ui.connectState.textContent = "连接失败";
    ui.enablePlayer.disabled = false;
    log(`连接失败：${error.message}`);
  }
}

function disconnectPlayer() {
  if (player) player.disconnect();
  ui.connectState.textContent = "未连接";
  ui.deviceId.textContent = "—";
  ui.trackState.textContent = "—";
  ui.disconnectPlayer.disabled = true;
  ui.enablePlayer.disabled = !player;
}

function renderPlaybackState(state) {
  if (!state) {
    ui.trackState.textContent = "尚未转移播放";
    return;
  }
  const track = state.track_window && state.track_window.current_track;
  const artists = track ? track.artists.map((artist) => artist.name).join(", ") : "";
  ui.trackState.textContent = track
    ? `${state.paused ? "已暂停" : "播放中"} · ${track.name} — ${artists}`
    : state.paused ? "已暂停" : "播放中";
}

function renderCaptureMetrics(metrics) {
  if (!metrics.active) {
    consecutiveNonzeroWindows = 0;
    setCaptureVerdict(metrics.error ? "捕获扩展报告错误" : "尚未启动捕获",
      metrics.error ? "fail" : "idle");
    ui.metricWindows.textContent = "0";
    if (metrics.error) ui.captureDetail.textContent = metrics.error;
    return;
  }

  const rms = Number(metrics.rms || 0);
  const peak = Number(metrics.peak || 0);
  const nonzeroRatio = Number(metrics.nonzeroRatio || 0);
  const hasPcm = peak > 0.0001 && nonzeroRatio > 0.001;
  consecutiveNonzeroWindows = hasPcm ? consecutiveNonzeroWindows + 1 : 0;

  ui.metricRate.textContent = metrics.sampleRate ? `${metrics.sampleRate} Hz` : "—";
  ui.metricChannels.textContent = metrics.channels || "—";
  ui.metricRms.textContent = rms.toFixed(6);
  ui.metricPeak.textContent = peak.toFixed(6);
  ui.metricNonzero.textContent = `${(nonzeroRatio * 100).toFixed(2)}%`;
  ui.metricWindows.textContent = String(consecutiveNonzeroWindows);

  if (metrics.error) {
    setCaptureVerdict("捕获扩展报告错误", "fail");
    ui.captureDetail.textContent = metrics.error;
  } else if (metrics.sampleRate !== 48000) {
    setCaptureVerdict("捕获到 PCM，但采样率不是 48 kHz", "warn");
    ui.captureDetail.textContent = "最终游戏协议会拒绝这个采样率，需要显式重采样。";
  } else if (consecutiveNonzeroWindows >= 3) {
    setCaptureVerdict("通过：持续捕获到 48 kHz 非零 PCM", "pass");
    ui.captureDetail.textContent = "Web Playback SDK → tabCapture 的关键可行性已成立。";
  } else {
    setCaptureVerdict(hasPcm ? "正在确认持续性" : "捕获已启动，等待非零 PCM", "running");
    ui.captureDetail.textContent =
      `AudioContext=${metrics.audioContextState || "unknown"}，累计运行 ${formatDuration(metrics.elapsedMs)}。`;
  }
}

function setAuthStatus(text, kind) {
  ui.authStatus.textContent = text;
  ui.authStatus.className = `status ${kind}`;
}

function setCaptureVerdict(text, kind) {
  ui.captureVerdict.textContent = text;
  ui.captureVerdict.className = `verdict ${kind}`;
}

function formatDuration(value) {
  return `${Math.round(Number(value || 0) / 1000)} 秒`;
}

function log(message) {
  const time = new Date().toLocaleTimeString();
  const lines = `${ui.log.textContent}[${time}] ${message}\n`.split("\n");
  ui.log.textContent = lines.slice(-20).join("\n");
  ui.log.scrollTop = ui.log.scrollHeight;
}
