const MAX_JACKET_BYTES = 2 * 1024 * 1024;
const FETCH_TIMEOUT_MS = 8000;

let trackGeneration = 0;

export function publishSpotifyTrack(track, paused, refreshJacket, log) {
  if (!track?.name) return;
  const artist = (track.artists || [])
    .map((item) => item?.name)
    .filter(Boolean)
    .join(", ");
  const trackKey = String(
    track.uri || track.id || `${track.name}\n${artist}`
  ).slice(0, 512);
  const metadata = {
    type: "metadata",
    title: String(track.name).slice(0, 512),
    artist: artist.slice(0, 512),
    album: String(track.album?.name || "").slice(0, 512),
    adapter: "spotify_web_playback_sdk",
    host: "spotify",
    trackKey,
    paused: Boolean(paused)
  };
  if (postGamePayload(metadata)) {
    log?.(`Spotify 元数据已发送：${metadata.title} — ${metadata.artist}`);
  } else {
    log?.("Spotify 元数据发送失败：WebView2 host 不可用");
  }

  if (!refreshJacket) return;
  const generation = ++trackGeneration;
  const urls = readImageUrls(track);
  void publishJacket(urls, trackKey, generation, log);
}

export function resetSpotifyMetadata() {
  ++trackGeneration;
}

async function publishJacket(urls, trackKey, generation, log) {
  if (urls.length === 0) {
    postJacketStatus("missing_url", trackKey, {
      error: "Spotify track has no album image"
    });
    return;
  }

  for (const url of urls) {
    postJacketStatus("fetch_start", trackKey, { source: url });
    try {
      const response = await fetchWithTimeout(url);
      if (!response.ok) {
        postJacketStatus("fetch_failed", trackKey, {
          source: url,
          error: `http ${response.status}`
        });
        continue;
      }
      const blob = await response.blob();
      if (blob.size <= 0 || blob.size > MAX_JACKET_BYTES) {
        postJacketStatus("bad_size", trackKey, {
          source: url,
          bytes: blob.size,
          error: "album image size is outside the accepted range"
        });
        continue;
      }
      const bytes = new Uint8Array(await blob.arrayBuffer());
      if (generation !== trackGeneration) return;
      const mime = normalizeMime(
        response.headers.get("content-type") || blob.type
      );
      const payload = {
        type: "jacket",
        mime,
        source: url.slice(0, 1024),
        jacketSource: "spotify_web_playback_sdk",
        bytes: bytes.length,
        data: bytesToBase64(bytes),
        trackKey
      };
      if (!postGamePayload(payload)) {
        postJacketStatus("send_failed", trackKey, {
          source: url,
          mime,
          bytes: bytes.length,
          error: "WebView2 host unavailable"
        });
        return;
      }
      postJacketStatus("sent", trackKey, {
        source: url,
        mime,
        bytes: bytes.length
      });
      log?.(`Spotify 专辑图已发送：${bytes.length} bytes · ${mime}`);
      return;
    } catch (error) {
      postJacketStatus("fetch_failed", trackKey, {
        source: url,
        error: String(error?.message || error)
      });
    }
  }
  log?.("Spotify 专辑图获取失败：所有候选地址均不可用");
}

function readImageUrls(track) {
  const images = Array.isArray(track.album?.images)
    ? track.album.images
    : [];
  const urls = [];
  const seen = new Set();
  for (const image of images) {
    const url = String(image?.url || "");
    if (!url || seen.has(url)) continue;
    seen.add(url);
    urls.push(url);
  }
  return urls;
}

async function fetchWithTimeout(url) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
  try {
    return await fetch(url, {
      cache: "force-cache",
      credentials: "omit",
      signal: controller.signal
    });
  } finally {
    clearTimeout(timer);
  }
}

function postJacketStatus(stage, trackKey, details) {
  postGamePayload({
    type: "jacket_status",
    stage: String(stage).slice(0, 64),
    source: String(details?.source || "").slice(0, 1024),
    jacketSource: "spotify_web_playback_sdk",
    mime: String(details?.mime || "").slice(0, 96),
    bytes: Number(details?.bytes || 0),
    error: String(details?.error || "").slice(0, 256),
    trackKey
  });
}

function postGamePayload(payload) {
  const host = window.chrome?.webview;
  if (!host) return false;
  host.postMessage(`game-json:${JSON.stringify(payload)}`);
  return true;
}

function normalizeMime(value) {
  return String(value || "application/octet-stream")
    .split(";")[0]
    .trim()
    .toLowerCase()
    .slice(0, 96);
}

function bytesToBase64(bytes) {
  let binary = "";
  const chunk = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunk) {
    binary += String.fromCharCode.apply(
      null,
      bytes.subarray(offset, offset + chunk)
    );
  }
  return btoa(binary);
}
