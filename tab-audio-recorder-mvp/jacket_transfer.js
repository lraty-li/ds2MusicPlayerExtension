const JACKET_WIDTH = 512;
const JACKET_HEIGHT = 512;
const MAX_JACKET_INPUT_BYTES = 8 * 1024 * 1024;
const RGBA_JACKET_BYTES = JACKET_WIDTH * JACKET_HEIGHT * 4;

async function sendJacket(jacket) {
  if (!socket || socket.readyState !== WebSocket.OPEN) {
    return { ok: false, sent: false, error: "socket closed" };
  }
  if (!jacket || !jacket.url) {
    sendJacketStatus("missing_url", { error: "missing artwork url" });
    return { ok: false, sent: false, error: "missing url" };
  }

  try {
    const url = String(jacket.url || "");
    const source = String(jacket.source || "");
    sendJacketStatus("fetch_start", { url, mime: jacket.mime || "", source });
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 8000);
    let response = null;
    try {
      response = await fetch(url, {
        credentials: "include",
        cache: "force-cache",
        signal: controller.signal
      });
    } finally {
      clearTimeout(timer);
    }
    if (!response.ok) {
      sendJacketStatus("fetch_failed", { url, source, error: `http ${response.status}` });
      return { ok: false, sent: false, error: "fetch failed" };
    }
    let blob = await response.blob();
    if (!blob || blob.size <= 0 || blob.size > MAX_JACKET_INPUT_BYTES) {
      sendJacketStatus("bad_size", { url, source, bytes: blob && blob.size || 0 });
      return { ok: false, sent: false, error: "bad size" };
    }
    const bytes = await convertArtworkToRgba(blob);
    if (!bytes || bytes.length !== RGBA_JACKET_BYTES) {
      sendJacketStatus("rgba_bad_size", { url, source, bytes: bytes && bytes.length || 0 });
      return { ok: false, sent: false, error: "bad rgba size" };
    }
    const mime = "application/x-ds2-rgba";
    const payload = {
      type: "jacket",
      mime,
      source: url.slice(0, 1024),
      jacketSource: source,
      width: JACKET_WIDTH,
      height: JACKET_HEIGHT,
      bytes: bytes.length,
      data: bytesToBase64(bytes)
    };
    sendJacketStatus("send_start", { url, source, mime, bytes: bytes.length });
    socket.send(JSON.stringify(payload));
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

async function convertArtworkToRgba(blob) {
  const image = await createImageBitmap(blob);
  const canvas = document.createElement("canvas");
  canvas.width = JACKET_WIDTH;
  canvas.height = JACKET_HEIGHT;
  const ctx = canvas.getContext("2d", { alpha: false });
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, JACKET_WIDTH, JACKET_HEIGHT);
  const scale = Math.min(JACKET_WIDTH / image.width, JACKET_HEIGHT / image.height);
  const width = Math.max(1, Math.round(image.width * scale));
  const height = Math.max(1, Math.round(image.height * scale));
  const x = Math.floor((JACKET_WIDTH - width) / 2);
  const y = Math.floor((JACKET_HEIGHT - height) / 2);
  ctx.drawImage(image, x, y, width, height);
  if (typeof image.close === "function") image.close();
  return new Uint8Array(ctx.getImageData(0, 0, JACKET_WIDTH, JACKET_HEIGHT).data.buffer);
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
