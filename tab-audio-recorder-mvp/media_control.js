const PAGE_CONTROL_FILES = [
  "page_control.js",
  "adapters/youtube.js",
  "adapters/netease.js",
  "adapters/media_session_hook.js"
];

async function handleBrowserControl(message, fallbackTabId) {
  const tabId = typeof message.tabId === "number" ? message.tabId : fallbackTabId;
  if (typeof tabId !== "number") return { ok: false, error: "NOID" };

  const command = message.command;
  if (command !== "pause" && command !== "resume") return { ok: false, error: "BADCMD" };

  try {
    await injectPageControl(tabId, { allFrames: true });
    const probes = await callPageControl(tabId, { allFrames: true }, "probe", command);
    const bestFrameId = selectControlFrame(probes);
    console.log("DS2 media frame probes:", probes.map((item) => ({
      frameId: item.frameId,
      result: item.result
    })));
    if (typeof bestFrameId !== "number") {
      return { ok: false, error: "NOOP", probes: summarizeProbes(probes) };
    }

    await injectPageControl(tabId, { frameIds: [bestFrameId] });
    const results = await callPageControl(tabId, { frameIds: [bestFrameId] }, "control", command);
    return summarizeControlResults(command, results);
  } catch (_) {
    await injectPageControl(tabId, {});
    return summarizeControlResults(command, await callPageControl(tabId, {}, "control", command));
  }
}

async function readBrowserMetadata(fallbackTabId) {
  const tabId = fallbackTabId;
  if (typeof tabId !== "number") return { ok: false, error: "NOID" };

  try {
    await injectPageControl(tabId, { allFrames: true });
    const results = await callPageControl(tabId, { allFrames: true }, "metadata", "");
    const metadata = selectMetadataResult(results);
    return metadata ? { ok: true, metadata } : { ok: false, error: "NOMETA" };
  } catch (error) {
    console.error("Failed to read DS2 browser metadata:", error);
    return { ok: false, error: String(error) };
  }
}

async function injectPageControl(tabId, target) {
  await chrome.scripting.executeScript({
    target: Object.assign({ tabId }, target),
    world: "MAIN",
    files: PAGE_CONTROL_FILES
  });
}

async function callPageControl(tabId, target, method, command) {
  return chrome.scripting.executeScript({
    target: Object.assign({ tabId }, target),
    world: "MAIN",
    func: callInjectedPageControl,
    args: [method, command]
  });
}

function callInjectedPageControl(method, command) {
  if (!window.__ds2PageMediaControl) return null;
  return window.__ds2PageMediaControl[method](command);
}

function selectMetadataResult(results) {
  let best = null;
  for (const item of results || []) {
    if (!item || !item.result || !item.result.title) continue;
    const score = item.result.score || 0;
    if (!best || score > best.score) {
      best = {
        title: item.result.title,
        artist: item.result.artist || "",
        jacket: item.result.jacket || null,
        score,
        frameId: item.frameId,
        adapter: item.result.adapter || "",
        host: item.result.host || "",
        paused: typeof item.result.paused === "boolean" ?
          item.result.paused : null
      };
    }
  }
  return best;
}

function selectControlFrame(results) {
  let best = null;
  for (const item of results || []) {
    if (!item || !item.result) continue;
    const score = item.result.score || 0;
    if (!best || score > best.score) best = { frameId: item.frameId, score };
  }
  return best && best.score > 0 ? best.frameId : null;
}

function summarizeProbes(results) {
  return (results || []).map((item) => ({
    frameId: item && item.frameId,
    adapter: item && item.result && item.result.adapter || "",
    host: item && item.result && item.result.host || "",
    score: item && item.result && item.result.score || 0,
    hasSession: !!(item && item.result && item.result.hasSession)
  }));
}

function summarizeControlResults(command, results) {
  const summary = { ok: true, command, media: 0, changed: 0, clicked: 0, already: 0 };
  for (const item of results || []) {
    if (!item || !item.result) continue;
    summary.media += item.result.media || 0;
    summary.changed += item.result.changed || 0;
    summary.clicked += item.result.clicked || 0;
    summary.already += item.result.already || 0;
  }
  console.log(
    `DS2 media ${command}: media=${summary.media} changed=${summary.changed} ` +
    `clicked=${summary.clicked} already=${summary.already}`
  );
  if (summary.changed === 0 && summary.already === 0) {
    summary.ok = false;
    summary.error = "NOOP";
  }
  return summary;
}
