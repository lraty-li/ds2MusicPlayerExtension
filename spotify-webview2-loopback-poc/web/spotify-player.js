import {
  publishSpotifyTrack,
  resetSpotifyMetadata
} from "./spotify-metadata.js";

let sdkPromise = null;
const READY_TIMEOUT_MS = 20_000;
const RETRY_DELAYS_MS = [2_000, 5_000, 10_000, 20_000, 30_000];

export class SpotifyConnectPlayer {
  constructor({ getToken, onStatus, onTrack, log }) {
    this.getToken = getToken;
    this.onStatus = onStatus;
    this.onTrack = onTrack;
    this.log = log;
    this.player = null;
    this.lastStateFingerprint = "";
    this.lastTrackKey = "";
    this.lastMetadataPaused = null;
    this.lastPlaying = null;
    this.stopped = true;
    this.ready = false;
    this.connecting = false;
    this.retryAttempt = 0;
    this.retryTimer = 0;
    this.readyTimer = 0;
  }

  async prepareAndConnect() {
    this.stopped = false;
    this.onStatus({ sdk: "正在加载", connect: "未连接", deviceId: "—" });
    try {
      await loadSpotifySdk();
    } catch (error) {
      this.onStatus({ sdk: "加载失败", connect: "等待重试" });
      this.log(`SDK 加载失败：${error.message}`);
      this.scheduleReconnect("SDK 加载失败");
      return false;
    }
    this.onStatus({ sdk: "已加载" });
    if (!this.player) this.createPlayer();
    return this.connect(false);
  }

  async activateAndConnect() {
    this.stopped = false;
    await loadSpotifySdk();
    if (!this.player) this.createPlayer();
    return this.connect(true);
  }

  disconnect() {
    this.stopped = true;
    this.ready = false;
    this.clearConnectionTimers();
    if (this.player) this.player.disconnect();
    this.lastTrackKey = "";
    this.lastMetadataPaused = null;
    this.updateSourcePlaying(false);
    resetSpotifyMetadata();
    this.onStatus({ connect: "未连接", deviceId: "—" });
    this.onTrack("—");
  }

  async applyControl(command) {
    if (!this.player) throw new Error("Spotify Player 尚未创建");
    if (command === "pause") {
      await this.player.pause();
    } else if (command === "resume") {
      await this.player.resume();
    } else {
      throw new Error(`未知控制命令：${command}`);
    }
    this.log(`游戏协议控制已执行：${command}`);
  }

  createPlayer() {
    this.player = new Spotify.Player({
      name: "Death Stranding 2",
      volume: 1,
      enableMediaSession: true,
      getOAuthToken: (callback) => {
        this.getToken()
          .then(callback)
          .catch((error) => {
            this.onStatus({ connect: "Token 获取失败" });
            this.log(error.message);
          });
      }
    });

    this.player.addListener("ready", ({ device_id: deviceId }) => {
      this.ready = true;
      this.retryAttempt = 0;
      this.clearConnectionTimers();
      this.onStatus({ connect: "设备已就绪", deviceId });
      this.log(`Connect 设备已就绪：${deviceId}`);
    });
    this.player.addListener("not_ready", () => {
      this.ready = false;
      this.onStatus({ connect: "设备离线", deviceId: "—" });
      this.log("Connect 设备进入 not_ready");
      this.scheduleReconnect("设备离线");
    });
    this.player.addListener("player_state_changed", (state) => {
      if (!state) {
        this.updateSourcePlaying(false);
        this.onTrack("尚未转移播放");
        this.log("player_state_changed state=null");
        return;
      }
      const track = state.track_window?.current_track;
      const artists = track?.artists?.map((artist) => artist.name).join(", ");
      const trackKey = track?.uri || track?.id ||
        `${track?.name || ""}\n${artists || ""}`;
      const trackChanged = track?.name &&
        trackKey !== this.lastTrackKey;
      const pauseChanged = track?.name &&
        state.paused !== this.lastMetadataPaused;
      if (trackChanged || pauseChanged) {
        this.lastTrackKey = trackKey;
        this.lastMetadataPaused = state.paused;
        publishSpotifyTrack(
          track, state.paused, trackChanged, this.log
        );
      }
      this.updateSourcePlaying(!state.paused);
      const fingerprint = `${state.paused}:${track?.uri || ""}`;
      if (fingerprint !== this.lastStateFingerprint) {
        this.lastStateFingerprint = fingerprint;
        this.log(
          `player_state_changed paused=${state.paused} ` +
          `position=${state.position} track=${track?.uri || "none"}`
        );
      }
      this.onTrack(
        track
          ? `${state.paused ? "已暂停" : "播放中"} · ${track.name} — ${artists}`
          : state.paused ? "已暂停" : "播放中"
      );
    });
    this.player.addListener("autoplay_failed", () => {
      this.onStatus({ connect: "autoplay_failed" });
      this.log("自动播放被拒绝；这是无点击启动路线的 No-Go 信号");
    });
    for (const type of [
      "initialization_error",
      "authentication_error",
      "account_error",
      "playback_error"
    ]) {
      this.player.addListener(type, ({ message }) => {
        this.onStatus({ connect: type });
        this.log(`${type}：${message}`);
        if (type !== "playback_error") {
          this.scheduleReconnect(type);
        }
      });
    }
  }

  updateSourcePlaying(playing) {
    if (this.lastPlaying === playing) return;
    this.lastPlaying = playing;
    window.chrome?.webview?.postMessage(
      `game-source-playing:${playing ? "1" : "0"}`
    );
    this.log(`Spotify 音源状态：${playing ? "claim" : "idle"}`);
  }

  async connect(withActivation) {
    if (this.connecting || this.stopped) return false;
    this.clearRetryTimer();
    this.connecting = true;
    this.onStatus({ connect: withActivation ? "手动激活中" : "自动连接中" });
    try {
      if (withActivation) await this.player.activateElement();
      const connected = await this.player.connect();
      if (!connected) throw new Error("player.connect() 返回 false");
      this.onStatus({ connect: "已连接，等待 ready" });
      this.log(
        withActivation
          ? "已用用户点击调用 activateElement()"
          : "未调用 activateElement()，正在验证无点击冷启动"
      );
      this.armReadyTimeout();
      return true;
    } catch (error) {
      this.onStatus({ connect: "连接失败" });
      this.log(`连接失败：${error.message}`);
      this.scheduleReconnect("连接失败");
      return false;
    } finally {
      this.connecting = false;
    }
  }

  armReadyTimeout() {
    if (this.ready || this.stopped) return;
    clearTimeout(this.readyTimer);
    this.readyTimer = window.setTimeout(() => {
      this.readyTimer = 0;
      if (this.ready || this.stopped) return;
      this.log("等待 Spotify ready 超时");
      this.player?.disconnect();
      this.scheduleReconnect("等待 ready 超时");
    }, READY_TIMEOUT_MS);
  }

  scheduleReconnect(reason) {
    if (this.stopped || this.retryTimer) return;
    clearTimeout(this.readyTimer);
    this.readyTimer = 0;
    const index = Math.min(
      this.retryAttempt,
      RETRY_DELAYS_MS.length - 1
    );
    const delay = RETRY_DELAYS_MS[index];
    this.retryAttempt++;
    this.onStatus({
      connect: `${reason}；${delay / 1000} 秒后重试`
    });
    this.retryTimer = window.setTimeout(() => {
      this.retryTimer = 0;
      if (this.stopped) return;
      this.ready = false;
      this.player?.disconnect();
      void this.prepareAndConnect();
    }, delay);
  }

  clearRetryTimer() {
    clearTimeout(this.retryTimer);
    this.retryTimer = 0;
  }

  clearConnectionTimers() {
    this.clearRetryTimer();
    clearTimeout(this.readyTimer);
    this.readyTimer = 0;
  }
}

function loadSpotifySdk() {
  if (window.Spotify) return Promise.resolve();
  if (sdkPromise) return sdkPromise;
  const script = document.createElement("script");
  const pending = new Promise((resolve, reject) => {
    const timer = window.setTimeout(() => {
      script.remove();
      reject(new Error("Spotify Web Playback SDK 加载超时"));
    }, READY_TIMEOUT_MS);
    window.onSpotifyWebPlaybackSDKReady = () => {
      clearTimeout(timer);
      resolve();
    };
    script.src = "https://sdk.scdn.co/spotify-player.js";
    script.async = true;
    script.onerror = () => {
      clearTimeout(timer);
      script.remove();
      reject(new Error("Spotify Web Playback SDK 加载失败"));
    };
    document.head.appendChild(script);
  });
  sdkPromise = pending.catch((error) => {
    sdkPromise = null;
    throw error;
  });
  return sdkPromise;
}
