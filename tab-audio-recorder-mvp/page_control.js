(function installDs2PageMediaControl() {
  const version = 6;
  window.__ds2PageMediaControlVersion = version;
  window.__ds2MediaAdapters = [];

  window.__ds2RegisterMediaAdapter = function registerMediaAdapter(adapter) {
    if (!adapter || !adapter.name || typeof adapter.control !== "function") return;
    window.__ds2MediaAdapters.push(adapter);
  };

  window.__ds2MediaTools = {
    cleanTitle,
    collectMedia,
    emptyResult,
    hasOutcome,
    pickBestMetadata,
    readDomMetadata,
    readMediaSessionMetadata,
    waitFor
  };

  window.__ds2PageMediaControl = {
    async control(command) {
      for (const adapter of selectAdapters()) {
        const result = await adapter.control(command);
        if (hasOutcome(result)) return result;
      }
      return emptyResult();
    },
    metadata() {
      let best = { score: 0, title: "", artist: "", host: location.hostname };
      for (const adapter of selectAdapters(true)) {
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

  function selectAdapters(includeMetadataFallback) {
    const adapters = window.__ds2MediaAdapters || [];
    const host = location.hostname;
    const siteAdapters = adapters.filter((adapter) => adapter.matches(host));
    if (siteAdapters.length > 0) {
      return includeMetadataFallback ?
        siteAdapters.concat(adapters.filter((adapter) => adapter.fallbackMetadata)) :
        siteAdapters;
    }
    return adapters.filter((adapter) => adapter.fallback);
  }

  function collectMedia(root) {
    const result = Array.from(root.querySelectorAll("audio, video"));
    for (const element of root.querySelectorAll("*")) {
      if (element.shadowRoot) result.push(...collectMedia(element.shadowRoot));
    }
    return result;
  }

  function waitFor(predicate, timeoutMs) {
    const started = Date.now();
    return new Promise((resolve) => {
      function tick() {
        if (predicate()) {
          resolve(true);
          return;
        }
        if (Date.now() - started >= timeoutMs) {
          resolve(false);
          return;
        }
        setTimeout(tick, 50);
      }
      tick();
    });
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

  function hasOutcome(result) {
    return !!result && (result.changed > 0 || result.already > 0);
  }

  function emptyResult() {
    return { media: 0, changed: 0, clicked: 0, already: 0 };
  }
})();
