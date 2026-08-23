(function registerNeteaseAdapter() {
  const tools = window.__ds2MediaTools;
  if (!tools) return;

  window.__ds2RegisterMediaAdapter({
    name: "netease",
    matches(host) {
      return host.includes("music.163.com");
    },
    probe(command) {
      const audio = document.querySelector("audio");
      const button = findPlayButton(command);
      if (!audio && !button) return null;
      const paused = !audio || audio.paused || audio.ended;
      const wants = command === "pause" ? !paused : paused;
      return { score: wants ? 130 : 80, media: audio ? 1 : 0, button: button ? 1 : 0 };
    },
    metadata() {
      const playerBar = readPlayerBarMetadata();
      const session = tools.readMediaSessionMetadata(170);
      if (session && playerBar && playerBar.jacket) session.jacket = playerBar.jacket;
      if (playerBar) return playerBar;
      if (session) return session;
      const title = tools.cleanTitle(document.title).replace(/\s*-\s*网易云音乐\s*$/, "");
      return title ? {
        score: 80,
        title,
        artist: ""
      } : null;
    },
    captureProbe() {
      const audio = findCaptureAudio();
      if (!audio) return null;
      return {
        score: audio.paused || audio.ended ? 140 : 240,
        media: 1,
        readyState: audio.readyState || 0
      };
    },
    captureSource() {
      return findCaptureAudio();
    },
    async control(command) {
      const audio = document.querySelector("audio");
      const result = tools.emptyResult();
      result.media = audio ? 1 : 0;

      if (audio) {
        const paused = audio.paused || audio.ended;
        if (command === "pause" && paused) {
          result.already = 1;
          return result;
        }
        if (command === "resume" && !paused) {
          result.already = 1;
          return result;
        }
      }

      if (command === "pause" && audio) {
        audio.pause();
        if (await tools.waitFor(() => audio.paused, 300)) result.changed = 1;
        return result;
      }

      const button = findPlayButton(command);
      if (!button) return null;
      button.click();
      result.clicked = 1;

      if (!audio) {
        result.changed = 1;
        return result;
      }
      if (command === "resume" &&
        await tools.waitFor(() => !audio.paused && !audio.ended, 1000)) {
        result.changed = 1;
      } else if (command === "pause" && await tools.waitFor(() => audio.paused, 500)) {
        result.changed = 1;
      }
      return result;
    }
  });

  function findPlayButton(command) {
    const selectors = command === "pause" ? [
      "#g_player .btns .pas",
      ".m-playbar .btns .pas",
      "#g_player .btns .ply",
      ".m-playbar .btns .ply"
    ] : [
      "#g_player .btns .ply",
      ".m-playbar .btns .ply",
      "#g_player .btns .pas",
      ".m-playbar .btns .pas"
    ];
    for (const selector of selectors) {
      const element = document.querySelector(selector);
      if (element) return element;
    }
    return null;
  }

  function findCaptureAudio() {
    let best = null;
    for (const audio of document.querySelectorAll("audio")) {
      const score = (audio.paused || audio.ended ? 0 : 1000) +
        (audio.readyState || 0) * 100 + (audio.currentSrc ? 10 : 0);
      if (!best || score > best.score) best = { audio, score };
    }
    return best && best.audio;
  }

  function readPlayerBarMetadata() {
    const title = readFirstText([
      "#g_player .words .name",
      ".m-playbar .words .name",
      "#g_player .f-thide.name",
      ".m-playbar .f-thide.name"
    ]);
    if (!title) return null;
    return {
      score: 190,
      title,
      artist: readFirstText([
        "#g_player .words .by a",
        ".m-playbar .words .by a",
        "#g_player .words .by",
        ".m-playbar .words .by"
      ]),
      jacket: readPlayerBarJacket()
    };
  }

  function readPlayerBarJacket() {
    return readFirstImageJacket([
      "#g_player .head img",
      ".m-playbar .head img",
      "#g_player .cover img",
      ".m-playbar .cover img"
    ]);
  }

  function readFirstText(selectors) {
    for (const selector of selectors) {
      const element = document.querySelector(selector);
      const text = tools.cleanTitle(element && element.textContent);
      if (text) return text;
    }
    return "";
  }

  function readFirstImageJacket(selectors) {
    for (const selector of selectors) {
      const image = document.querySelector(selector);
      const jacket = image && readImageJacket(image);
      if (jacket) return jacket;
    }
    return null;
  }

  function readImageJacket(image) {
    const raw = image.currentSrc || image.src ||
      image.getAttribute("data-src") || image.getAttribute("src") || "";
    if (!raw || raw.startsWith("data:")) return null;
    return {
      url: normalizeNeteaseImageUrl(raw),
      mime: "image/jpeg",
      size: 640 * 640,
      source: "netease-player"
    };
  }

  function normalizeNeteaseImageUrl(raw) {
    const url = new URL(raw, location.href);
    if (url.hostname.endsWith("music.126.net") ||
      url.hostname.endsWith("music.163.com")) {
      url.search = "param=640y640";
    }
    return url.href;
  }
})();
