const FIREFOX_MAX_JACKET_IMAGE_BYTES = 2 * 1024 * 1024;

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || message.type !== "fetch-jacket" || !message.url) return false;
  fetchJacketForContent(message.url)
    .then(sendResponse)
    .catch((error) => sendResponse({ ok: false, error: String(error), status: 0 }));
  return true;
});

async function fetchJacketForContent(url) {
  const response = await fetch(String(url), {
    credentials: "include",
    cache: "force-cache"
  });
  const mime = response.headers.get("content-type") || "";
  if (!response.ok) {
    return { ok: false, status: response.status, mime, error: `http ${response.status}` };
  }

  const blob = await response.blob();
  if (!blob || blob.size <= 0 || blob.size > FIREFOX_MAX_JACKET_IMAGE_BYTES) {
    return {
      ok: false,
      status: response.status,
      mime: blob && blob.type || mime,
      bytes: blob && blob.size || 0,
      error: "bad size"
    };
  }

  return {
    ok: true,
    status: response.status,
    mime: mime || blob.type || "",
    buffer: await blob.arrayBuffer()
  };
}
