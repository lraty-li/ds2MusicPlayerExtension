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
      const session = tools.readMediaSessionMetadata(170);
      if (session) return session;
      const title = tools.cleanTitle(document.title).replace(/\s*-\s*网易云音乐\s*$/, "");
      return title ? { score: 80, title, artist: "" } : null;
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
})();
