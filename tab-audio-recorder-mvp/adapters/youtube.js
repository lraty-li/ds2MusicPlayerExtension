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
      return tools.pickBestMetadata([
        tools.readDomMetadata(["ytmusic-player-bar .title"],
          ["ytmusic-player-bar .byline a", "ytmusic-player-bar .subtitle a"], 210),
        tools.readDomMetadata(["ytd-watch-metadata h1 yt-formatted-string", "#title h1"],
          ["#owner #channel-name a", "ytd-video-owner-renderer #channel-name a"], 200),
        data && {
          score: 180,
          title: tools.cleanTitle(data.title),
          artist: tools.cleanTitle(data.author)
        },
        tools.readMediaSessionMetadata(190)
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
})();
