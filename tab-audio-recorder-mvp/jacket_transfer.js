const MAX_JACKET_IMAGE_BYTES = 2 * 1024 * 1024;

async function sendJacket(jacket) {
  if (!socket || socket.readyState !== WebSocket.OPEN) {
    return { ok: false, sent: false, error: "socket closed" };
  }
  const urls = readJacketUrls(jacket);
  if (!jacket || urls.length === 0) {
    sendJacketStatus("missing_url", { error: "missing artwork url" });
    return { ok: false, sent: false, error: "missing url" };
  }

  try {
    const source = String(jacket.source || "");
    let url = "";
    let response = null;
    for (const candidateUrl of urls) {
      sendJacketStatus("fetch_start", { url: candidateUrl, mime: jacket.mime || "", source });
      response = await fetchJacket(candidateUrl);
      if (response && response.ok) {
        url = candidateUrl;
        break;
      }
      sendJacketStatus("fetch_failed", {
        url: candidateUrl,
        source,
        error: response ? `http ${response.status}` : "fetch failed"
      });
    }
    if (!response || !response.ok) {
      return { ok: false, sent: false, error: "fetch failed" };
    }
    const blob = await response.blob();
    if (!blob || blob.size <= 0 || blob.size > MAX_JACKET_IMAGE_BYTES) {
      sendJacketStatus("bad_size", { url, source, bytes: blob && blob.size || 0 });
      return { ok: false, sent: false, error: "bad size" };
    }
    const bytes = new Uint8Array(await blob.arrayBuffer());
    const mime = normalizeJacketMime(
      response.headers.get("content-type") || blob.type || jacket.mime);
    const payload = {
      type: "jacket",
      mime,
      source: url.slice(0, 1024),
      jacketSource: source,
      bytes: bytes.length,
      data: bytesToBase64(bytes)
    };
    const imageInfo = formatJacketImageInfo(bytes.length, jacket);
    sendJacketStatus("send_start", {
      url,
      source,
      mime,
      bytes: bytes.length,
      error: imageInfo
    });
    socket.send(JSON.stringify(payload));
    sendJacketStatus("sent", { url, source, mime, bytes: bytes.length, error: imageInfo });
    return { ok: true, sent: true, bytes: bytes.length, mime: payload.mime };
  } catch (error) {
    sendJacketStatus("error", {
      url: jacket && jacket.url || "",
      source: jacket && jacket.source || "",
      error: String(error)
    });
    return { ok: false, sent: false, error: String(error) };
  }
}

function readJacketUrls(jacket) {
  const values = [];
  if (jacket && jacket.url) values.push(jacket.url);
  if (jacket && Array.isArray(jacket.fallbackUrls)) values.push(...jacket.fallbackUrls);

  const urls = [];
  const seen = new Set();
  for (const value of values) {
    const url = String(value || "");
    if (!url || seen.has(url)) continue;
    seen.add(url);
    urls.push(url);
  }
  return urls;
}

async function fetchJacket(url) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 8000);
  try {
    return await fetch(url, {
      credentials: "include",
      cache: "force-cache",
      signal: controller.signal
    });
  } finally {
    clearTimeout(timer);
  }
}

function normalizeJacketMime(mime) {
  const clean = String(mime || "").split(";")[0].trim().toLowerCase();
  return (clean || "application/octet-stream").slice(0, 96);
}

function formatJacketImageInfo(bytes, jacket) {
  const declared = Number(jacket && jacket.size || 0);
  const declaredText = declared > 0 ? ` declaredPixels=${declared}` : "";
  return `rawBytes=${bytes}${declaredText}`;
}

function sendJacketStatus(stage, details) {
  if (!socket || socket.readyState !== WebSocket.OPEN) return;
  const payload = {
    type: "jacket_status",
    stage: String(stage || "").slice(0, 64),
    source: String(details && details.url || "").slice(0, 1024),
    jacketSource: String(details && details.source || "").slice(0, 64),
    mime: String(details && details.mime || "").slice(0, 96),
    bytes: Number(details && details.bytes || 0),
    error: String(details && details.error || "").slice(0, 256)
  };
  try {
    socket.send(JSON.stringify(payload));
  } catch (_) {
    if (typeof handleSocketClosed === "function") handleSocketClosed(socket);
  }
}

function bytesToBase64(bytes) {
  let binary = "";
  const chunk = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunk) {
    binary += String.fromCharCode.apply(null, bytes.subarray(offset, offset + chunk));
  }
  return btoa(binary);
}
