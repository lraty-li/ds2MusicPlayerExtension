(function registerYoutubeAdapter() {
  const tools = window.__ds2MediaTools;
  if (!tools) return;

  window.__ds2RegisterMediaAdapter({
    name: "youtube",
    matches(host) {
      return host.includes("youtube.com");
    },
    probe(command) {
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
    async control(command) {
      const player = document.getElementById("movie_player");
      if (!player || typeof player.pauseVideo !== "function") return null;

      const result = tools.emptyResult();
      result.media = 1;
      const state = readState(player);
      if (command === "pause") {
        if (state !== 1) {
          result.already = 1;
          return result;
        }
        player.pauseVideo();
        if (await tools.waitFor(() => readState(player) !== 1, 500)) result.changed = 1;
        return result;
      }

      if (state === 1) {
        result.already = 1;
        return result;
      }
      if (typeof player.playVideo !== "function") return null;
      player.playVideo();
      if (await tools.waitFor(() => readState(player) === 1, 700)) result.changed = 1;
      return result;
    }
  });

  function readState(player) {
    return player && player.getPlayerState ? player.getPlayerState() : 1;
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
    return readCurrentYtMusicJacket() ||
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
        source: "youtube-video"
      };
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
