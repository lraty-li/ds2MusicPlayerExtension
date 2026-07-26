import {
  beginAuthorization,
  clearAuthorization,
  finishAuthorizationCallback,
  getValidAccessToken,
  hasStoredAuthorization,
  loadClientId,
  saveClientId
} from "./auth.js";
import { initializeNativePcmRelay } from "./pcm-relay.js";
import { SpotifyConnectPlayer } from "./spotify-player.js";

const CLIENT_ID_PATTERN = /^[0-9a-f]{32}$/i;
let spotifyPlayer = null;
let lastPlaybackStatus = "";

window.__pocSpotifyControl = applyNativeSpotifyControl;
window.__pocSpotifyShutdown = disconnectPlayer;
post("helper-status:runtime-start");
initialize().catch(() => post("helper-runtime-error"));

async function initialize() {
  initializeNativePcmRelay();
  const query = new URLSearchParams(window.location.search);
  const configuredClientId = query.get("client_id");
  if (configuredClientId && CLIENT_ID_PATTERN.test(configuredClientId)) {
    saveClientId(configuredClientId);
  }

  try {
    await finishAuthorizationCallback();
  } catch {
    clearAuthorization();
    post("helper-auth-error");
    return;
  }

  const clientId = loadClientId();
  if (!clientId) {
    post("helper-config-missing-client-id");
    return;
  }
  if (!hasStoredAuthorization()) {
    await beginAuthorization(clientId);
    return;
  }

  post("helper-auth-ready");
  post("helper-status:authorization-ready");
  try {
    await prepareWidevine();
    post("helper-status:widevine=ready");
  } catch (error) {
    post(`helper-status:widevine=unavailable:${safeText(error.name)}`);
  }
  spotifyPlayer = createPlayer();
  try {
    await spotifyPlayer.prepareAndConnect();
  } catch {
    post("helper-player-error");
  }
}

function createPlayer() {
  return new SpotifyConnectPlayer({
    getToken: getValidAccessToken,
    onStatus: reportPlayerStatus,
    onTrack: reportPlaybackStatus,
    log: reportPlayerLog
  });
}

function disconnectPlayer() {
  if (!spotifyPlayer) return;
  post("helper-status:player-disconnect");
  spotifyPlayer.disconnect();
  spotifyPlayer = null;
}

function reportPlayerStatus(status) {
  const parts = [];
  if (status.sdk) parts.push(`sdk=${status.sdk}`);
  if (status.connect) parts.push(`connect=${status.connect}`);
  if (status.deviceId && status.deviceId !== "—") {
    parts.push("device=ready");
  }
  if (parts.length) post(`helper-status:${parts.join(" ")}`);
}

function reportPlayerLog(message) {
  if (/^(player_state_changed|Spotify 音源状态|Connect 设备)/.test(message)) {
    post(`helper-status:${safeText(message)}`);
  }
}

function reportPlaybackStatus(text) {
  const status = safeText(text);
  if (!status || status === lastPlaybackStatus) return;
  lastPlaybackStatus = status;
  post(`helper-status:playback=${status}`);
}

async function prepareWidevine() {
  if (!navigator.requestMediaKeySystemAccess) {
    throw new DOMException("EME unsupported", "NotSupportedError");
  }
  const request = navigator.requestMediaKeySystemAccess(
    "com.widevine.alpha",
    [{
      initDataTypes: ["cenc"],
      audioCapabilities: [{
        contentType: 'audio/mp4; codecs="mp4a.40.2"'
      }]
    }]
  );
  let timeoutId = 0;
  const timeout = new Promise((_, reject) => {
    timeoutId = window.setTimeout(
      () => reject(new DOMException("Widevine timeout", "TimeoutError")),
      10_000
    );
  });
  try {
    await Promise.race([request, timeout]);
  } finally {
    window.clearTimeout(timeoutId);
  }
}

function safeText(value) {
  return String(value || "")
    .replace(/[\r\n\t]/g, " ")
    .slice(0, 384);
}

async function applyNativeSpotifyControl(command) {
  if (!spotifyPlayer) return false;
  try {
    await spotifyPlayer.applyControl(command);
    return true;
  } catch {
    return false;
  }
}

function post(message) {
  window.chrome?.webview?.postMessage(message);
}
