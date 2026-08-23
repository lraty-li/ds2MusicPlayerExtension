(function registerBilibiliAdapter() {
  const tools = window.__ds2MediaTools;
  if (!tools) return;

  window.__ds2RegisterMediaAdapter({
    name: "bilibili",
    matches(host) {
      return host === "bilibili.com" || host.endsWith(".bilibili.com");
    },
    probe(command) {
      const video = findCaptureVideo();
      if (!video) return null;
      const paused = video.paused || video.ended;
      const wants = command === "pause" ? !paused : paused;
      return { score: wants ? 150 : 90, media: 1 };
    },
    captureProbe() {
      const video = findCaptureVideo();
      if (!video) return null;
      return {
        score: video.paused || video.ended ? 140 : 240,
        media: 1,
        readyState: video.readyState || 0
      };
    },
    captureSource() {
      return findCaptureVideo();
    },
    metadata() {
      return tools.pickBestMetadata([
        tools.readMediaSessionMetadata(180),
        tools.readDomMetadata([
          ".video-title",
          "h1.video-title",
          ".media-title",
          "h1"
        ], [
          ".up-name",
          ".username",
          ".media-info .media-count"
        ], 130)
      ]);
    },
    async control(command) {
      const video = findCaptureVideo();
      if (!video) return null;
      const result = tools.emptyResult();
      result.media = 1;
      const paused = video.paused || video.ended;
      if (command === "pause" && paused) {
        result.already = 1;
        return result;
      }
      if (command === "resume" && !paused) {
        result.already = 1;
        return result;
      }
      if (command === "pause") video.pause();
      else await video.play().catch(() => {});
      const ok = command === "pause" ? () => video.paused : () => !video.paused;
      if (await tools.waitFor(ok, 700)) result.changed = 1;
      return result;
    }
  });

  function findCaptureVideo() {
    let best = null;
    for (const video of document.querySelectorAll("video")) {
      const area = (video.videoWidth || video.clientWidth || 0) *
        (video.videoHeight || video.clientHeight || 0);
      const score = (video.paused || video.ended ? 0 : 1000000) +
        area + (video.readyState || 0) * 1000;
      if (!best || score > best.score) best = { video, score };
    }
    return best && best.video;
  }
})();
