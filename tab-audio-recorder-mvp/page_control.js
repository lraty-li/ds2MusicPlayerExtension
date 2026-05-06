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
    metadata() {
      let best = { score: 0, title: "", artist: "", host: location.hostname };
      for (const adapter of [...selectAdapters(), genericMetadataAdapter]) {
        if (typeof adapter.metadata !== "function") continue;
        const result = adapter.metadata();
        if (result && result.score > best.score) {
          best = Object.assign({ adapter: adapter.name, host: location.hostname }, result);
        }
      }
      return best;
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
    metadata() {
      return readMediaSessionMetadata(160);
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
    metadata() {
      const player = document.getElementById("movie_player");
      const data = player && player.getVideoData ? player.getVideoData() : null;
      return pickBestMetadata([
        readDomMetadata(["ytmusic-player-bar .title"],
          ["ytmusic-player-bar .byline a", "ytmusic-player-bar .subtitle a"], 210),
        readDomMetadata(["ytd-watch-metadata h1 yt-formatted-string", "#title h1"],
          ["#owner #channel-name a", "ytd-video-owner-renderer #channel-name a"], 200),
        data && { score: 180, title: cleanTitle(data.title), artist: cleanTitle(data.author) },
        readMediaSessionMetadata(190)
      ]);
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
    metadata() {
      const session = readMediaSessionMetadata(170);
      if (session) return session;
      const title = cleanTitle(document.title).replace(/\s*-\s*网易云音乐\s*$/, "");
      return title ? { score: 80, title, artist: "" } : null;
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

  const genericMetadataAdapter = {
    name: "genericMetadata",
    metadata() {
      const session = readMediaSessionMetadata(100);
      if (session) return session;
      const title = cleanTitle(document.title);
      return title ? { score: 50, title, artist: "" } : null;
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

  function readMediaSessionMetadata(score) {
    const metadata = navigator.mediaSession && navigator.mediaSession.metadata;
    if (!metadata) return null;
    const title = cleanTitle(metadata.title);
    if (!title) return null;
    return {
      score,
      title,
      artist: cleanTitle(metadata.artist || "")
    };
  }

  function readDomMetadata(titleSelectors, artistSelectors, score) {
    const title = readFirstText(titleSelectors);
    if (!title) return null;
    return { score, title, artist: readFirstText(artistSelectors) };
  }

  function readFirstText(selectors) {
    for (const selector of selectors) {
      const element = document.querySelector(selector);
      const text = cleanTitle(element && element.textContent);
      if (text) return text;
    }
    return "";
  }

  function pickBestMetadata(items) {
    let best = null;
    for (const item of items) {
      if (!item || !item.title) continue;
      const penalty = item.title.includes("…") || item.title.includes("...") ? 100 : 0;
      const rank = item.score + Math.min(item.title.length, 80) - penalty;
      if (!best || rank > best.rank) best = Object.assign({ rank }, item);
    }
    return best;
  }

  function cleanTitle(value) {
    if (typeof value !== "string") return "";
    return value.replace(/\s+/g, " ").trim().slice(0, 512);
  }

  function emptyResult() {
    return { media: 0, changed: 0, clicked: 0, session: 0, already: 0 };
  }
})();
