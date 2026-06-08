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
    readDocumentArtwork,
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
      artist: cleanTitle(metadata.artist || ""),
      jacket: tagJacket(readArtwork(metadata.artwork), "mediaSession") ||
        tagJacket(readDocumentArtwork(), "document")
    };
  }

  function readArtwork(artwork) {
    if (!Array.isArray(artwork) || artwork.length === 0) return null;
    let best = null;
    for (const item of artwork) {
      if (!item || typeof item.src !== "string" || !item.src) continue;
      const url = new URL(item.src, location.href).href;
      const size = parseArtworkSize(item.sizes || "");
      if (!best || size > best.size) {
        best = { url, mime: item.type || "", size };
      }
    }
    return best && best.url ? best : null;
  }

  function parseArtworkSize(sizes) {
    const match = String(sizes || "").match(/(\d+)\s*x\s*(\d+)/i);
    return match ? Number(match[1]) * Number(match[2]) : 0;
  }

  function readDomMetadata(titleSelectors, artistSelectors, score) {
    const title = readFirstText(titleSelectors);
    if (!title) return null;
    return {
      score,
      title,
      artist: readFirstText(artistSelectors),
      jacket: tagJacket(readDocumentArtwork(), "document")
    };
  }

  function readDocumentArtwork() {
    const selectors = [
      'meta[property="og:image"]',
      'meta[property="og:image:secure_url"]',
      'meta[name="twitter:image"]',
      'meta[itemprop="image"]',
      'link[rel="image_src"]'
    ];
    for (const selector of selectors) {
      const element = document.querySelector(selector);
      const value = element && (element.content || element.href);
      if (value) return { url: new URL(value, location.href).href, mime: "", size: 0 };
    }

    const video = document.querySelector("video[poster]");
    if (video && video.poster) {
      return { url: new URL(video.poster, location.href).href, mime: "", size: 0 };
    }
    return readLargestImage();
  }

  function readLargestImage() {
    let best = null;
    for (const image of document.querySelectorAll("img")) {
      const src = image.currentSrc || image.src;
      if (!src) continue;
      const size = (image.naturalWidth || image.width || 0) *
        (image.naturalHeight || image.height || 0);
      if (size >= 4096 && (!best || size > best.size)) {
        best = { url: new URL(src, location.href).href, mime: "", size };
      }
    }
    return best;
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
    let bestJacket = null;
    for (const item of items) {
      if (!item || !item.title) continue;
      if (item.jacket && item.jacket.url) {
        const sourceBoost = item.jacket.source === "mediaSession" ? 1000000 : 0;
        const jacketRank = sourceBoost + item.score * 1000 + (item.jacket.size || 0);
        if (!bestJacket || jacketRank > bestJacket.rank) {
          bestJacket = { rank: jacketRank, jacket: item.jacket };
        }
      }
      const penalty = item.title.includes("…") || item.title.includes("...") ? 100 : 0;
      const rank = item.score + Math.min(item.title.length, 80) - penalty;
      if (!best || rank > best.rank) best = Object.assign({ rank }, item);
    }
    if (best && bestJacket) best.jacket = bestJacket.jacket;
    return best;
  }

  function tagJacket(jacket, source) {
    return jacket && jacket.url ? Object.assign({ source }, jacket) : null;
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
