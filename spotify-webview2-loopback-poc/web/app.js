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
import {
  enableAutomaticHelperAudio,
  initializeAudioOutputProbe
} from "./audio-output-probe.js";
import { initializeGameStreamStatus } from "./game-stream-status.js";
import { initializeHostProbe } from "./host-probe.js";
import { initializeNativePcmBridge } from "./native-pcm-bridge.js";
import { SpotifyConnectPlayer } from "./spotify-player.js";

const byId = (id) => document.getElementById(id);
const HELPER_MODE_KEY = "ds2.spotify.helperMode";
let spotifyPlayer = null;
let helperMode = false;
window.__pocSpotifyControl = applyNativeSpotifyControl;

initialize().catch((error) => {
  setAuthStatus(error.message, "error");
  log(`初始化失败：${error.message}`);
});

async function initialize() {
  const query = new URLSearchParams(window.location.search);
  const configuredClientId = query.get("client_id");
  if (query.get("helper_mode") === "1") {
    sessionStorage.setItem(HELPER_MODE_KEY, "1");
  }
  helperMode =
    query.get("helper_mode") === "1" ||
    sessionStorage.getItem(HELPER_MODE_KEY) === "1";
  if (configuredClientId && /^[0-9a-f]{32}$/i.test(configuredClientId)) {
    saveClientId(configuredClientId);
  }
  byId("client-id").value = loadClientId();
  byId("redirect-uri").value = redirectUri();
  bindActions();
  initializeNativePcmBridge(log);
  initializeGameStreamStatus(log);
  initializeHostProbe(log);
  initializeAudioOutputProbe(log);
  if (helperMode) enableAutomaticHelperAudio();
  if (configuredClientId) log("已从 config.json 自动加载 Client ID");

  try {
    if (await finishAuthorizationCallback()) {
      setAuthStatus("Spotify 授权成功，refresh token 已保存在专属 WebView2 配置中", "ok");
      log("OAuth PKCE 回调完成");
    }
  } catch (error) {
    clearAuthorization();
    setAuthStatus(error.message, "error");
    log(error.message);
  }
  updateAuthorizationUi();
  updateHelperWindow();
  if (hasStoredAuthorization()) await autoStartPlayer();
}

function bindActions() {
  byId("save-client").addEventListener("click", () => {
    try {
      const previous = loadClientId();
      const current = saveClientId(byId("client-id").value);
      if (previous && previous !== current) disconnectPlayer();
      byId("client-id").value = current;
      updateAuthorizationUi();
      log("Client ID 已保存");
    } catch (error) {
      setAuthStatus(error.message, "error");
    }
  });

  byId("copy-redirect").addEventListener("click", async () => {
    await navigator.clipboard.writeText(redirectUri());
    log("Redirect URI 已复制");
  });

  byId("authorize").addEventListener("click", () => {
    beginAuthorization(byId("client-id").value).catch((error) => {
      setAuthStatus(error.message, "error");
      log(error.message);
    });
  });

  byId("clear-auth").addEventListener("click", () => {
    disconnectPlayer();
    clearAuthorization();
    setAuthStatus("本地授权已清除", "neutral");
    updateAuthorizationUi();
    updateHelperWindow();
    log("已清除 WebView2 配置中的 Spotify token");
  });

  byId("manual-connect").addEventListener("click", async () => {
    try {
      spotifyPlayer ||= createPlayer();
      await spotifyPlayer.activateAndConnect();
    } catch (error) {
      log(`手动激活失败：${error.message}`);
    }
  });
  byId("disconnect-player").addEventListener("click", disconnectPlayer);
}

async function autoStartPlayer() {
  spotifyPlayer ||= createPlayer();
  try {
    await spotifyPlayer.prepareAndConnect();
  } catch (error) {
    setConnectStatus({ sdk: "加载失败", connect: "失败" });
    log(`播放器自动启动失败：${error.message}`);
    updateHelperWindow();
  }
}

function createPlayer() {
  return new SpotifyConnectPlayer({
    getToken: getValidAccessToken,
    onStatus: setConnectStatus,
    onTrack: (text) => {
      byId("track-state").textContent = text;
    },
    log
  });
}

function disconnectPlayer() {
  if (spotifyPlayer) spotifyPlayer.disconnect();
}

async function applyNativeSpotifyControl(command) {
  if (!spotifyPlayer) {
    log(`忽略游戏协议控制：播放器尚未创建（${command}）`);
    return false;
  }
  try {
    await spotifyPlayer.applyControl(command);
    return true;
  } catch (error) {
    log(`游戏协议控制失败：${error.message}`);
    return false;
  }
}

function updateAuthorizationUi() {
  const hasClientId = !!loadClientId();
  const authorized = hasStoredAuthorization();
  byId("authorize").disabled = !hasClientId;
  byId("clear-auth").disabled = !authorized;
  byId("manual-connect").disabled = !authorized;
  if (authorized) {
    setAuthStatus("已保存可刷新的 Spotify 授权", "ok");
  } else if (hasClientId) {
    setAuthStatus("Client ID 已保存，下一步进行本机 PKCE 授权", "neutral");
  }
}

function updateHelperWindow() {
  if (!helperMode) return;
  window.chrome?.webview?.postMessage(
    hasStoredAuthorization()
      ? "helper-auth-ready"
      : "helper-auth-required"
  );
}

function setConnectStatus(update) {
  if (update.sdk) byId("sdk-state").textContent = update.sdk;
  if (update.connect) byId("connect-state").textContent = update.connect;
  if (update.deviceId) byId("device-id").textContent = update.deviceId;
}

function setAuthStatus(text, kind) {
  const element = byId("auth-status");
  element.textContent = text;
  element.className = `status ${kind}`;
}

function log(message) {
  window.chrome?.webview?.postMessage(`web-log:${message}`);
  const output = byId("log");
  const lines = `${output.textContent}[${new Date().toLocaleTimeString()}] ${message}\n`
    .split("\n")
    .slice(-30);
  output.textContent = lines.join("\n");
  output.scrollTop = output.scrollHeight;
}
