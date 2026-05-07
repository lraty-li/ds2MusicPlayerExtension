const ids = {
  status: "status",
  toggle: "toggle",
  pause: "pause",
  resume: "resume",
  metadata: "metadata",
  refresh: "refresh",
  readTitle: "read-title",
  readArtist: "read-artist",
  readSource: "read-source",
  sentTitle: "sent-title",
  sentArtist: "sent-artist",
  sentSource: "sent-source",
  sentAt: "sent-at",
  controlResult: "control-result"
};

const el = Object.fromEntries(
  Object.entries(ids).map(([key, id]) => [key, document.getElementById(id)])
);

el.toggle.addEventListener("click", toggleStream);
el.pause.addEventListener("click", () => run("popup-control", { command: "pause" }));
el.resume.addEventListener("click", () => run("popup-control", { command: "resume" }));
el.metadata.addEventListener("click", () => run("popup-read-metadata"));
el.refresh.addEventListener("click", refresh);

refresh();

async function run(type, extra = {}) {
  setBusy(true);
  try {
    render(await chrome.runtime.sendMessage(Object.assign({ type }, extra)));
  } catch (error) {
    renderError(error);
  } finally {
    setBusy(false);
  }
}

async function toggleStream() {
  setBusy(true);
  try {
    const current = await chrome.runtime.sendMessage({ type: "popup-state" });
    const state = current && current.state || {};
    const message = { type: "popup-toggle", tabId: current && current.activeTabId };
    if (!state.streaming) {
      message.streamId = await chrome.tabCapture.getMediaStreamId({
        targetTabId: message.tabId
      });
    }
    render(await chrome.runtime.sendMessage(message));
  } catch (error) {
    renderError(error);
  } finally {
    setBusy(false);
  }
}

async function refresh() {
  setBusy(true);
  try {
    render(await chrome.runtime.sendMessage({ type: "popup-state" }));
  } catch (error) {
    renderError(error);
  } finally {
    setBusy(false);
  }
}

function render(response) {
  if (!response || !response.ok) {
    renderError(response && response.error ? response.error : "No response");
    return;
  }

  const state = response.state || {};
  const offscreen = response.offscreen || {};
  const stream = state.streaming ? state.status : "idle";
  const socket = offscreen.connected ? "connected" : offscreen.active ? "waiting" : "closed";
  el.status.textContent = `stream=${stream} socket=${socket} tab=${response.activeTabId || "-"}`;

  renderMetadata("read", response.lastReadMetadata);
  renderMetadata("sent", response.lastSentMetadata);
  el.sentAt.textContent = response.lastSentMetadata && response.lastSentMetadata.sentAt || "-";
  el.controlResult.textContent = response.lastControlResult ?
    JSON.stringify(response.lastControlResult, null, 2) : "-";
}

function renderMetadata(prefix, metadata) {
  const title = el[`${prefix}Title`];
  const artist = el[`${prefix}Artist`];
  const source = el[`${prefix}Source`];
  if (!metadata) {
    title.textContent = "-";
    artist.textContent = "-";
    source.textContent = "-";
    return;
  }
  title.textContent = metadata.title || metadata.error || "-";
  artist.textContent = metadata.artist || "-";
  source.textContent = [metadata.adapter, metadata.host].filter(Boolean).join(" / ") || "-";
}

function renderError(error) {
  el.status.textContent = `ERR ${String(error)}`;
}

function setBusy(busy) {
  for (const button of document.querySelectorAll("button")) button.disabled = busy;
}
