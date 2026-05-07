async function handlePopupMessage(message) {
  if (message.type === "popup-state") return readPanelState();
  const tabId = await resolveActiveTabId(message.tabId);
  if (message.type === "popup-toggle") {
    await toggleStream(tabId, message.streamId || null);
    return readPanelState(tabId);
  }
  if (message.type === "popup-control") {
    lastControlResult = await runBrowserControl({
      type: "browser-control",
      command: message.command,
      reason: "panel",
      tabId
    });
    return Object.assign({ ok: true, control: lastControlResult }, await readPanelState(tabId));
  }
  if (message.type === "popup-read-metadata") {
    const result = await readBrowserMetadata(tabId);
    lastReadMetadata = result.ok ? result.metadata : { error: result.error || "NOMETA" };
    return Object.assign({ ok: result.ok, metadata: lastReadMetadata }, await readPanelState(tabId));
  }
  return { ok: false, error: "BADCMD" };
}

async function readPanelState(preferredTabId) {
  await syncStreamState();
  const tabId = await resolveActiveTabId(preferredTabId).catch(() => null);
  return {
    ok: true,
    state: Object.assign({}, state),
    offscreen: await readOffscreenStatus(),
    activeTabId: tabId,
    lastReadMetadata,
    lastSentMetadata,
    lastControlResult
  };
}

async function syncStreamState() {
  const offscreen = await readOffscreenStatus();
  if (!offscreen.active) {
    state = { streaming: false, tabId: null, status: "idle", lastControl: state.lastControl };
    stopMetadataPolling();
    return offscreen;
  }
  state.streaming = true;
  state.tabId = typeof offscreen.tabId === "number" ? offscreen.tabId : state.tabId;
  state.status = offscreen.connected ? "streaming" : "waiting";
  if (offscreen.connected) startMetadataPolling();
  else stopMetadataPolling();
  return offscreen;
}

async function resolveActiveTabId(preferredTabId) {
  if (typeof preferredTabId === "number") return preferredTabId;
  const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
  const tab = tabs && tabs[0];
  if (tab && typeof tab.id === "number") return tab.id;
  throw new Error("missing active tab");
}

async function runAndStoreBrowserControl(message) {
  lastControlResult = await runBrowserControl(message);
  return lastControlResult;
}
