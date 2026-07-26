const METADATA_POLL_MS = 2000;

let metadataTimer = null;
let lastMetadataKey = "";
let lastJacketSuccessKey = "";
let jacketInFlight = "";

function startMetadataPolling() {
  if (metadataTimer) return;
  collectAndSendMetadata();
  metadataTimer = setInterval(collectAndSendMetadata, METADATA_POLL_MS);
}

function stopMetadataPolling() {
  if (!metadataTimer) return;
  clearInterval(metadataTimer);
  metadataTimer = null;
  lastMetadataKey = "";
  lastJacketSuccessKey = "";
  jacketInFlight = "";
}

function refreshMetadataForSourceClaim() {
  lastMetadataKey = "";
  lastJacketSuccessKey = "";
  jacketInFlight = "";
  collectAndSendMetadata();
}

async function collectAndSendMetadata() {
  const tabId = getTargetTabId();
  if (!isStreamSocketOpen() || typeof tabId !== "number") return;
  const result = await chrome.runtime.sendMessage({ type: "read-metadata", tabId });
  if (!result || !result.ok || !result.metadata || !result.metadata.title) return;

  const metadata = result.metadata;
  const key = `${metadata.title}\n${metadata.artist || ""}`;
  const jacketUrl = metadata.jacket && metadata.jacket.url || "";
  const jacketKey = `${key}\n${jacketUrl}`;
  if (jacketUrl && jacketKey !== lastJacketSuccessKey && jacketKey !== jacketInFlight) {
    sendJacketUpdate(metadata.jacket, jacketKey);
  } else if (!jacketUrl && jacketKey !== lastJacketSuccessKey) {
    lastJacketSuccessKey = jacketKey;
    sendJacketUpdate({ url: "", mime: "", missing: true }, "");
  }
  if (key === lastMetadataKey) return;
  lastMetadataKey = key;
  sendMetadata({
    title: metadata.title,
    artist: metadata.artist || "",
    adapter: metadata.adapter || "",
    host: metadata.host || ""
  });
}

async function sendJacketUpdate(jacket, jacketKey) {
  if (jacketKey) jacketInFlight = jacketKey;
  try {
    const result = await sendJacket(jacket);
    if (result && result.sent && jacketKey) lastJacketSuccessKey = jacketKey;
  } catch (_) {
  } finally {
    if (jacketKey && jacketInFlight === jacketKey) jacketInFlight = "";
  }
}
