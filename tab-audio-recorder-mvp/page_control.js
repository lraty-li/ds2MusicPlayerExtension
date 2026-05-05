(function installDs2PageMediaControl() {
  const version = 4;
  if (window.__ds2PageMediaControlVersion === version && window.__ds2PageMediaControl) return;
  window.__ds2PageMediaControlVersion = version;

  const api = {
    control(command) {
      for (const adapter of selectAdapters()) {
        const result = adapter.control(command);
        if (result) return result;
      }
      return emptyResult();
    },
    probe(command) {
      let best = { score: 0, adapter: "", host: location.hostname };
      for (const adapter of selectAdapters()) {
        const probe = adapter.probe(command);
        if (probe && probe.score > best.score) {
          best = Object.assign({ adapter: adapter.name, host: location.hostname }, probe);
        }
      }
      return best;
    }
  };

  window.__ds2PageMediaControl = api;

  function selectAdapters() {
    const host = location.hostname;
    if (host.includes("youtube.com")) {
      return [youtubeAdapter, genericMediaAdapter, mediaSessionAdapter, clickAdapter];
    }
    if (host.includes("music.163.com")) {
      return [neteaseAdapter, mediaSessionAdapter];
    }
    return [genericMediaAdapter, mediaSessionAdapter, clickAdapter];
  }

  const mediaSessionAdapter = {
    name: "mediaSession",
    probe(command) {
      const action = toMediaSessionAction(command);
      return hasMediaSessionHandler(action) ? { score: 140, hasSession: true } : null;
    },
    control(command) {
      const action = toMediaSessionAction(command);
      const result = emptyResult();
      return dispatchMediaSession(action, result) ? result : null;
    }
  };

  const youtubeAdapter = {
    name: "youtube",
    probe(command) {
      const player = document.getElementById("movie_player");
      if (!player) return null;
      const state = player.getPlayerState ? player.getPlayerState() : 1;
      const wants = command === "pause" ? state === 1 : state !== 1;
      return { score: wants ? 180 : 100, playerState: state };
    },
    control(command) {
      const player = document.getElementById("movie_player");
      if (!player || typeof player.pauseVideo !== "function") return null;
      const state = player.getPlayerState ? player.getPlayerState() : 1;
      const result = emptyResult();
      result.media = 1;
      if (command === "pause" && state === 1) {
        player.pauseVideo();
        result.changed = 1;
      } else if (command === "resume" && state !== 1) {
        player.playVideo();
        result.changed = 1;
      } else {
        result.already = 1;
      }
      return result;
    }
  };

  const neteaseAdapter = {
    name: "netease",
    probe(command) {
      const audio = document.querySelector("audio");
      if (!audio) return null;
      const paused = audio.paused || audio.ended;
      const wants = command === "pause" ? !paused : paused;
      return { score: wants ? 120 : 80, media: 1, currentTime: audio.currentTime || 0 };
    },
    control(command) {
      const audio = document.querySelector("audio");
      if (!audio) return null;
      const result = emptyResult();
      result.media = 1;
      if (command === "pause") {
        if (audio.paused || audio.ended) result.already = 1;
        else {
          audio.pause();
          result.changed = 1;
        }
        return result;
      }
      if (!audio.paused && !audio.ended) {
        result.already = 1;
        return result;
      }
      return null;
    }
  };

  const genericMediaAdapter = {
    name: "genericMedia",
    probe(command) {
      const media = collectMedia(document).filter((item) => !item.ended);
      if (media.length === 0) return null;
      const wants = command === "pause" ?
        media.filter((item) => !item.paused).length :
        media.filter((item) => item.paused).length;
      return { score: wants > 0 ? 90 : 40, media: media.length, wants };
    },
    control(command) {
      const media = collectMedia(document).filter((item) => !item.ended);
      if (media.length === 0) return null;
      const result = emptyResult();
      result.media = media.length;
      for (const element of media) {
        if (command === "pause") pauseElement(element, result);
        else resumeElement(element, result);
      }
      return result;
    }
  };

  const clickAdapter = {
    name: "clickFallback",
    probe(command) {
      return findButton(command) ? { score: 20 } : null;
    },
    control(command) {
      const button = findButton(command);
      if (!button) return null;
      button.click();
      const result = emptyResult();
      result.clicked = 1;
      return result;
    }
  };

  function pauseElement(element, result) {
    if (element.paused || element.ended) result.already++;
    else {
      element.pause();
      result.changed++;
    }
  }

  function resumeElement(element, result) {
    if (!element.paused && !element.ended) result.already++;
    else {
      const playResult = element.play();
      result.changed++;
      if (playResult && typeof playResult.catch === "function") {
        playResult.catch(() => {
          result.changed--;
        });
      }
    }
  }

  function hasMediaSessionHandler(action) {
    const handlers = window.__ds2MediaSessionHandlers;
    return !!handlers && typeof handlers[action] === "function";
  }

  function toMediaSessionAction(command) {
    return command === "resume" ? "play" : "pause";
  }

  function dispatchMediaSession(action, result) {
    const fn = window.__ds2DispatchMediaSessionAction;
    if (typeof fn !== "function" || !fn(action)) return false;
    result.session = 1;
    result.changed = 1;
    return true;
  }

  function collectMedia(root) {
    const result = Array.from(root.querySelectorAll("audio, video"));
    for (const element of root.querySelectorAll("*")) {
      if (element.shadowRoot) result.push(...collectMedia(element.shadowRoot));
    }
    return result;
  }

  function findButton(command) {
    const selectors = command === "pause" ?
      [".ytp-play-button[aria-label*='Pause']", "[aria-label*='暂停']", "[title*='暂停']"] :
      [".ytp-play-button[aria-label*='Play']", "[aria-label*='播放']", "[title*='播放']"];
    for (const selector of selectors) {
      const button = document.querySelector(selector);
      if (button) return button;
    }
    return null;
  }

  function emptyResult() {
    return { media: 0, changed: 0, clicked: 0, session: 0, already: 0 };
  }
})();
