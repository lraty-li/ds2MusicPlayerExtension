let sdkPromise = null;

export class SpotifyConnectPlayer {
  constructor({ getToken, onStatus, onTrack, log }) {
    this.getToken = getToken;
    this.onStatus = onStatus;
    this.onTrack = onTrack;
    this.log = log;
    this.player = null;
  }

  async prepareAndConnect() {
    this.onStatus({ sdk: "正在加载", connect: "未连接", deviceId: "—" });
    await loadSpotifySdk();
    this.onStatus({ sdk: "已加载" });
    if (!this.player) this.createPlayer();
    await this.connect(false);
  }

  async activateAndConnect() {
    await loadSpotifySdk();
    if (!this.player) this.createPlayer();
    await this.connect(true);
  }

  disconnect() {
    if (this.player) this.player.disconnect();
    this.onStatus({ connect: "未连接", deviceId: "—" });
    this.onTrack("—");
  }

  createPlayer() {
    this.player = new Spotify.Player({
      name: "Death Stranding 2 Helper PoC",
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
      this.onStatus({ connect: "设备已就绪", deviceId });
      this.log(`Connect 设备已就绪：${deviceId}`);
    });
    this.player.addListener("not_ready", () => {
      this.onStatus({ connect: "设备离线", deviceId: "—" });
      this.log("Connect 设备进入 not_ready");
    });
    this.player.addListener("player_state_changed", (state) => {
      if (!state) {
        this.onTrack("尚未转移播放");
        return;
      }
      const track = state.track_window?.current_track;
      const artists = track?.artists?.map((artist) => artist.name).join(", ");
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
      });
    }
  }

  async connect(withActivation) {
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
    } catch (error) {
      this.onStatus({ connect: "连接失败" });
      this.log(`连接失败：${error.message}`);
    }
  }
}

function loadSpotifySdk() {
  if (window.Spotify) return Promise.resolve();
  if (sdkPromise) return sdkPromise;
  sdkPromise = new Promise((resolve, reject) => {
    window.onSpotifyWebPlaybackSDKReady = resolve;
    const script = document.createElement("script");
    script.src = "https://sdk.scdn.co/spotify-player.js";
    script.async = true;
    script.onerror = () => reject(new Error("Spotify Web Playback SDK 加载失败"));
    document.head.appendChild(script);
  });
  return sdkPromise;
}
