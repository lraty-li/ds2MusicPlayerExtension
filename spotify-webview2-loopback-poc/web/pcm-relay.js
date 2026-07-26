const CHANNEL = "ds2-audio-frame-probe-v1";
let installed = false;

export function initializeNativePcmRelay() {
  if (installed) return;
  installed = true;
  window.addEventListener("message", relayPcmChunk);
}

function relayPcmChunk({ data }) {
  if (data?.channel !== CHANNEL) return;
  if (data.type === "pcm-ring-commit") {
    if (!validRingCommit(data) || !window.chrome?.webview) return;
    window.chrome.webview.postMessage([
      "pcm-ring-v1",
      data.streamId,
      data.sequence,
      data.slot
    ].join("|"));
    return;
  }
  if (data.type !== "pcm-chunk") return;
  if (!validChunkEnvelope(data) || !window.chrome?.webview) return;
  window.chrome.webview.postMessage([
    "pcm-v1",
    data.streamId,
    data.sequence,
    data.sampleRate,
    data.channels,
    data.frames,
    data.payload
  ].join("|"));
}

function validRingCommit(data) {
  return (
    typeof data.streamId === "string" &&
    /^[a-z0-9._-]{1,96}$/i.test(data.streamId) &&
    Number.isSafeInteger(data.sequence) &&
    data.sequence >= 0 &&
    Number.isSafeInteger(data.slot) &&
    data.slot >= 0 &&
    data.slot < 1024
  );
}

function validChunkEnvelope(data) {
  const integer = (value, minimum, maximum) =>
    Number.isSafeInteger(value) && value >= minimum && value <= maximum;
  return (
    typeof data.streamId === "string" &&
    /^[a-z0-9._-]{1,96}$/i.test(data.streamId) &&
    integer(data.sequence, 0, Number.MAX_SAFE_INTEGER) &&
    integer(data.sampleRate, 8000, 192000) &&
    integer(data.channels, 1, 8) &&
    integer(data.frames, 1, 48000) &&
    typeof data.payload === "string" &&
    data.payload.length > 0 &&
    data.payload.length <= 1048576
  );
}
