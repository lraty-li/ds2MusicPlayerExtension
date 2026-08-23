(function registerYoutubeAdapter() {
  const tools = window.__ds2MediaTools;
  if (!tools) return;

  window.__ds2RegisterMediaAdapter({
    name: "youtube",
    matches(host) {
      return host.includes("youtube.com");
    },
    probe(command) {
      const ytmusicState = readYtMusicButtonState();
      if (ytmusicState) {
        console.log(`DS2 youtube music probe command=${command} state=${ytmusicState}`);
        const wants = command === "pause" ?
          ytmusicState === "playing" : ytmusicState === "paused";
        return { score: wants ? 230 : 120, ytmusicState };
      }
      const player = document.getElementById("movie_player");
      if (!player) return null;
      const state = readState(player);
      const wants = command === "pause" ? state === 1 : state !== 1;
      return { score: wants ? 180 : 100, playerState: state };
    },
    metadata() {
      const player = document.getElementById("movie_player");
      const data = player && player.getVideoData ? player.getVideoData() : null;
      const jacket = readCurrentJacket(player, data);
      const session = tools.readMediaSessionMetadata(120);
      if (session && jacket) session.jacket = jacket;
      return tools.pickBestMetadata([
        readTextMetadata(["ytmusic-player-bar .title"],
          ["ytmusic-player-bar .byline a", "ytmusic-player-bar .subtitle a"], 210, jacket),
        readTextMetadata(["ytd-watch-metadata h1 yt-formatted-string", "#title h1"],
          ["#owner #channel-name a", "ytd-video-owner-renderer #channel-name a"], 200, jacket),
        data && {
          score: 180,
          title: tools.cleanTitle(data.title),
          artist: tools.cleanTitle(data.author),
          jacket
        },
        session
      ]);
    },
    captureProbe() {
      const video = findCaptureVideo();
      if (!video) return null;
      return {
        score: video.paused || video.ended ? 150 : 260,
        media: 1,
        readyState: video.readyState || 0
      };
    },
    captureSource() {
      return findCaptureVideo();
    },
    async control(command) {
      const ytmusic = await controlYtMusic(command);
      if (ytmusic) return ytmusic;

      const player = document.getElementById("movie_player");
      const video = findCaptureVideo();
      if (!player && !video) return null;

      const result = tools.emptyResult();
      result.media = 1;
      if (command === "pause") {
        if (!isPlaying(player, video)) {
          result.already = 1;
          return result;
        }
        if (player && typeof player.pauseVideo === "function") player.pauseVideo();
        else if (video) video.pause();
        if (await tools.waitFor(() => !isPlaying(player, video), 500)) result.changed = 1;
        return result;
      }

      if (isPlaying(player, video)) {
        result.already = 1;
        return result;
      }
      if (player && typeof player.playVideo === "function") player.playVideo();
      else if (video && typeof video.play === "function") await video.play().catch(() => {});
      if (await tools.waitFor(() => isPlaying(player, video), 700)) {
        result.changed = 1;
        return result;
      }

      const button = findPlayButton();
      if (button) {
        button.click();
        result.clicked = 1;
        result.changed = 1;
        await tools.waitFor(() => isPlaying(player, video), 1000);
      }
      return result;
    }
  });

  function readState(player) {
    return player && player.getPlayerState ? player.getPlayerState() : 0;
  }

  function isPlaying(player, video) {
    if (player && typeof player.getPlayerState === "function") return readState(player) === 1;
    return !!(video && !video.paused && !video.ended);
  }

  function findCaptureVideo() {
    const player = document.getElementById("movie_player");
    const main = player && findBestVideo(tools.collectMedia(player));
    if (main) return main;
    return findBestVideo(tools.collectMedia(document));
  }

  function findBestVideo(mediaElements) {
    let best = null;
    for (const video of mediaElements || []) {
      if (video.tagName !== "VIDEO") continue;
      const area = (video.videoWidth || video.clientWidth || 0) *
        (video.videoHeight || video.clientHeight || 0);
      const score = (video.paused || video.ended ? 0 : 1000000) +
        area + (video.readyState || 0) * 1000;
      if (!best || score > best.score) best = { video, score };
    }
    return best && best.video;
  }

  async function controlYtMusic(command) {
    if (location.hostname !== "music.youtube.com") return null;
    const button = findPlayButton();
    if (!button) return null;

    const state = readYtMusicButtonState(button);
    console.log(`DS2 youtube music control command=${command} state=${state}`);
    const result = tools.emptyResult();
    result.media = 1;
    if (command === "pause" && state === "paused") {
      result.already = 1;
      return result;
    }
    if (command === "resume" && state === "playing") {
      result.already = 1;
      return result;
    }
    if ((command === "pause" && state === "playing") ||
      (command === "resume" && state === "paused")) {
      button.click();
      result.clicked = 1;
      const expected = command === "pause" ? "paused" : "playing";
      if (await tools.waitFor(() => readYtMusicButtonState() === expected, 1000)) {
        result.changed = 1;
      }
      return result;
    }
    return null;
  }

  function readYtMusicButtonState(button = findPlayButton()) {
    if (!button) return "";
    const text = [
      button.getAttribute("aria-label"),
      button.getAttribute("title"),
      button.title,
      button.textContent
    ].join(" ").toLowerCase();
    if (/pause|暂停|一時停止/.test(text)) return "playing";
    if (/play|播放|再生/.test(text)) return "paused";
    return "";
  }

  function findPlayButton() {
    const selectors = [
      "ytmusic-player-bar .play-pause-button",
      "ytmusic-player-bar tp-yt-paper-icon-button[title]",
      "#movie_player .ytp-play-button",
      "button[aria-label='Play']",
      "button[title='Play']",
      "button[aria-label='播放']",
      "button[title='播放']"
    ];
    for (const selector of selectors) {
      const button = document.querySelector(selector);
      if (button) return button;
    }
    return null;
  }

  function readTextMetadata(titleSelectors, artistSelectors, score, jacket) {
    const title = readFirstText(titleSelectors);
    if (!title) return null;
    return {
      score,
      title,
      artist: readFirstText(artistSelectors),
      jacket
    };
  }

  function readFirstText(selectors) {
    for (const selector of selectors) {
      const element = document.querySelector(selector);
      const text = tools.cleanTitle(element && element.textContent);
      if (text) return text;
    }
    return "";
  }

  function readCurrentJacket(player, data) {
    const ytmusicJacket = readCurrentYtMusicJacket();
    return normalizeYoutubeImageJacket(ytmusicJacket, data) ||
      ytmusicJacket ||
      readCurrentVideoJacket(player, data) ||
      null;
  }

  function readCurrentYtMusicJacket() {
    return readFirstImageJacket([
      "ytmusic-player-bar ytmusic-thumbnail-renderer img",
      "ytmusic-player-bar yt-img-shadow img",
      "ytmusic-player-page ytmusic-player img"
    ], "ytmusic-player");
  }

  function readCurrentVideoJacket(player, data) {
    const videoId = data && typeof data.video_id === "string" ? data.video_id : "";
    if (videoId) {
      return buildYoutubeVideoJacket(videoId, "youtube-video");
    }
    const video = player && player.querySelector ? player.querySelector("video[poster]") : null;
    if (video && video.poster) {
      return {
        url: new URL(video.poster, location.href).href,
        mime: "",
        size: 0,
        source: "youtube-video"
      };
    }
    return null;
  }

  function normalizeYoutubeImageJacket(jacket, data) {
    const urlVideoId = extractYoutubeImageVideoId(jacket && jacket.url);
    const dataVideoId = data && typeof data.video_id === "string" ? data.video_id : "";
    const videoId = urlVideoId || dataVideoId;
    if (!urlVideoId || !videoId) return null;
    return buildYoutubeVideoJacket(videoId, "youtube-video");
  }

  function buildYoutubeVideoJacket(videoId, source) {
    const encoded = encodeURIComponent(videoId);
    return {
      url: `https://i.ytimg.com/vi/${encoded}/maxresdefault.jpg`,
      fallbackUrls: [
        `https://i.ytimg.com/vi/${encoded}/hq720.jpg`,
        `https://i.ytimg.com/vi/${encoded}/sddefault.jpg`,
        `https://i.ytimg.com/vi/${encoded}/hqdefault.jpg`
      ],
      mime: "image/jpeg",
      size: 1280 * 720,
      source
    };
  }

  function extractYoutubeImageVideoId(value) {
    try {
      const url = new URL(value || "", location.href);
      if (!url.hostname.endsWith("ytimg.com")) return "";
      const parts = url.pathname.split("/");
      const vi = parts.indexOf("vi");
      return vi >= 0 && parts[vi + 1] ? parts[vi + 1] : "";
    } catch (_) {
      return "";
    }
  }

  function readFirstImageJacket(selectors, source) {
    for (const selector of selectors) {
      const image = document.querySelector(selector);
      const candidate = image && readImageCandidate(image, source);
      if (candidate) return candidate;
    }
    return null;
  }

  function readImageCandidate(image, source) {
    let best = image.currentSrc || image.src || image.getAttribute("src") || "";
    let bestSize = image.naturalWidth && image.naturalHeight ?
      image.naturalWidth * image.naturalHeight : 0;
    for (const item of parseSrcset(image.getAttribute("srcset") || "")) {
      if (!best || item.size > bestSize) {
        best = item.url;
        bestSize = item.size;
      }
    }
    return best && !best.startsWith("data:") ? {
      url: new URL(best, location.href).href,
      mime: "",
      size: bestSize,
      source
    } : null;
  }

  function parseSrcset(srcset) {
    const result = [];
    for (const item of String(srcset || "").split(",")) {
      const parts = item.trim().split(/\s+/);
      if (!parts[0]) continue;
      const descriptor = parts[1] || "";
      const width = descriptor.endsWith("w") ? Number(descriptor.slice(0, -1)) || 0 : 0;
      result.push({ url: parts[0], size: width * width });
    }
    return result;
  }
})();
