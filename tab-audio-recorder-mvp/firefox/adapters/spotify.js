(function registerSpotifyAdapter() {
  const tools = window.__ds2MediaTools;
  if (!tools) return;

  window.__ds2RegisterMediaAdapter({
    name: "spotify",
    matches(host) {
      return host === "open.spotify.com" || host.endsWith(".spotify.com");
    },
    probe(command) {
      const media = findCaptureMedia();
      const button = findPlayButton(command);
      if (!media && !button) return null;
      const paused = !media || media.paused || media.ended;
      const wants = command === "pause" ? !paused : paused;
      return { score: wants ? 150 : 90, media: media ? 1 : 0 };
    },
    captureProbe() {
      const media = findCaptureMedia();
      if (!media) return null;
      return {
        score: media.paused || media.ended ? 130 : 230,
        media: 1,
        readyState: media.readyState || 0
      };
    },
    captureSource() {
      return findCaptureMedia();
    },
    metadata() {
      return tools.pickBestMetadata([
        tools.readMediaSessionMetadata(190),
        tools.readDomMetadata([
          '[data-testid="context-item-info-title"]',
          '[data-testid="now-playing-widget"] a[href*="/track/"]',
          '.Root__now-playing-bar a[href*="/track/"]'
        ], [
          '[data-testid="context-item-info-artist"]',
          '[data-testid="now-playing-widget"] a[href*="/artist/"]',
          '.Root__now-playing-bar a[href*="/artist/"]'
        ], 150)
      ]);
    },
    async control(command) {
      const media = findCaptureMedia();
      const result = tools.emptyResult();
      result.media = media ? 1 : 0;
      if (media) {
        const paused = media.paused || media.ended;
        if (command === "pause" && paused) {
          result.already = 1;
          return result;
        }
        if (command === "resume" && !paused) {
          result.already = 1;
          return result;
        }
      }
      if (media && command === "pause" && !media.paused && !media.ended) {
        media.pause();
        if (await tools.waitFor(() => media.paused, 500)) result.changed = 1;
        return result;
      }
      const button = findPlayButton(command);
      if (!button) return null;
      button.click();
      result.clicked = 1;
      result.changed = 1;
      return result;
    }
  });

  function findCaptureMedia() {
    let best = null;
    for (const media of tools.collectMedia(document)) {
      if (media.tagName !== "AUDIO" && media.tagName !== "VIDEO") continue;
      const score = (media.paused || media.ended ? 0 : 1000) +
        (media.readyState || 0) * 100 + (media.currentSrc ? 10 : 0);
      if (!best || score > best.score) best = { media, score };
    }
    return best && best.media;
  }

  function findPlayButton(command) {
    const selectors = command === "pause" ? [
      '[data-testid="control-button-pause"]',
      '[aria-label="Pause"]'
    ] : [
      '[data-testid="control-button-play"]',
      '[aria-label="Play"]'
    ];
    for (const selector of selectors) {
      const button = document.querySelector(selector);
      if (button) return button;
    }
    return null;
  }
})();
