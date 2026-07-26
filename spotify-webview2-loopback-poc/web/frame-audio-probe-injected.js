(() => {
  "use strict";
  const channel = "ds2-audio-frame-probe-v1";
  if (window.__ds2AudioFrameProbe) return;

  const probe = {
    channel,
    frameId: crypto.randomUUID?.() ||
      `${Date.now()}-${Math.random().toString(16).slice(2)}`,
    contexts: new Map(),
    mediaElements: new Map(),
    commandHandlers: new Map(),
    desiredMode: "default",
    nextContextId: 1,
    nextMediaId: 1,
    observer: null
  };
  window.__ds2AudioFrameProbe = probe;

  function readContextSink(context) {
    try {
      const sink = context.sinkId;
      if (sink && typeof sink === "object") return sink.type || "object";
      return sink ? String(sink) : "default";
    } catch {
      return "unreadable";
    }
  }

  function readMediaSink(element) {
    try {
      return element.sinkId ? String(element.sinkId) : "default";
    } catch {
      return "unreadable";
    }
  }

  function buildState(closed = false) {
    const contexts = [...probe.contexts.values()].map((record) => ({
      id: record.id,
      source: record.source,
      state: record.context.state,
      sampleRate: record.context.sampleRate,
      sink: readContextSink(record.context),
      error: record.error
    }));
    const media = [...probe.mediaElements.values()].map((record) => ({
      id: record.id,
      tag: record.element.tagName.toLowerCase(),
      playing: !record.element.paused && !record.element.ended,
      sink: readMediaSink(record.element),
      graphState: record.graphState,
      graphError: record.graphError,
      directRms: record.directRms,
      directPeak: record.directPeak,
      directNonzero: record.directNonzero,
      directWindows: record.directWindows
    }));
    const noneContexts =
      contexts.filter((item) => item.sink === "none").length;
    const uncoveredPlaying = media.filter(
      (item) => item.playing && item.graphState !== "attached"
    ).length;
    return {
      frameId: probe.frameId,
      closed,
      isTop: window === window.top,
      origin: location.origin,
      desiredMode: probe.desiredMode,
      contextSetSinkSupported:
        typeof window.AudioContext?.prototype?.setSinkId === "function",
      mediaSetSinkSupported:
        typeof window.HTMLMediaElement?.prototype?.setSinkId === "function",
      contexts,
      media,
      trackedSilent:
        probe.desiredMode === "none" &&
        contexts.length > 0 &&
        noneContexts === contexts.length &&
        uncoveredPlaying === 0
    };
  }

  probe.report = (closed = false) => {
    try {
      window.top.postMessage({
        channel,
        type: "frame-report",
        state: buildState(closed)
      }, "*");
    } catch {
    }
  };

  async function applyContextMode(record) {
    if (typeof record.context.setSinkId !== "function") {
      record.error = "setSinkId unsupported";
      return false;
    }
    try {
      const sink =
        probe.desiredMode === "none" ? { type: "none" } : "";
      await record.context.setSinkId(sink);
      record.error = "";
      return true;
    } catch (error) {
      record.error = `${error.name || "Error"}: ${error.message}`;
      return false;
    }
  }

  async function restoreMediaDefault(record) {
    if (typeof record.element.setSinkId !== "function") return false;
    try {
      await record.element.setSinkId("");
      return true;
    } catch {
      return false;
    }
  }

  async function setMode(mode) {
    probe.desiredMode = mode;
    probe.report();
    await Promise.all(
      [...probe.contexts.values()].map(applyContextMode)
    );
    if (mode === "default") {
      await Promise.all(
        [...probe.mediaElements.values()].map(restoreMediaDefault)
      );
    }
    probe.report();
  }

  function registerContext(context, source) {
    if (probe.contexts.has(context)) return;
    const record = {
      id: probe.nextContextId++,
      context,
      source,
      error: ""
    };
    probe.contexts.set(context, record);
    context.addEventListener?.("statechange", () => probe.report());
    if (probe.desiredMode === "none") {
      applyContextMode(record).finally(probe.report);
    } else {
      probe.report();
    }
  }

  function wrapContext(globalName) {
    const NativeContext = window[globalName];
    if (typeof NativeContext !== "function" ||
        NativeContext.__ds2AudioFrameProbe) {
      return;
    }
    try {
      const WrappedContext = new Proxy(NativeContext, {
        construct(target, args) {
          const context = Reflect.construct(target, args, target);
          registerContext(context, globalName);
          return context;
        }
      });
      Object.defineProperty(
        WrappedContext, "__ds2AudioFrameProbe", { value: true }
      );
      window[globalName] = WrappedContext;
    } catch {
    }
  }

  function registerMedia(element) {
    if (probe.mediaElements.has(element)) return;
    const record = {
      id: probe.nextMediaId++,
      element,
      graphState: "none",
      graphError: "",
      directRms: 0,
      directPeak: 0,
      directNonzero: 0,
      directWindows: 0
    };
    probe.mediaElements.set(element, record);
    for (const name of ["play", "playing", "pause", "ended", "emptied"]) {
      element.addEventListener(name, () => probe.report());
    }
    probe.report();
  }

  function wrapMediaPlayback() {
    const prototype = window.HTMLMediaElement?.prototype;
    if (!prototype || prototype.play.__ds2AudioFrameProbe) return;
    const nativePlay = prototype.play;
    const wrappedPlay = function (...args) {
      registerMedia(this);
      const result = nativePlay.apply(this, args);
      Promise.resolve(result).then(
        () => probe.report(),
        () => probe.report()
      );
      return result;
    };
    Object.defineProperty(
      wrappedPlay, "__ds2AudioFrameProbe", { value: true }
    );
    prototype.play = wrappedPlay;
  }

  function scanNode(node) {
    if (!(node instanceof Element)) return;
    if (node.matches("audio, video")) registerMedia(node);
    node.querySelectorAll?.("audio, video").forEach(registerMedia);
  }

  function startObserver() {
    document.querySelectorAll("audio, video").forEach(registerMedia);
    if (!document.documentElement || probe.observer) return;
    probe.observer = new MutationObserver((records) => {
      for (const record of records) {
        for (const node of record.addedNodes) scanNode(node);
      }
    });
    probe.observer.observe(
      document.documentElement, { childList: true, subtree: true }
    );
    probe.report();
  }

  function forward(message) {
    document.querySelectorAll("iframe").forEach((frame) => {
      try {
        frame.contentWindow?.postMessage(message, "*");
      } catch {
      }
    });
  }

  window.addEventListener("message", ({ data }) => {
    if (data?.channel !== channel || data.type !== "command") return;
    if (data.action === "set-mode" &&
        (data.mode === "none" || data.mode === "default")) {
      setMode(data.mode);
    } else if (data.action === "report") {
      probe.report();
    } else {
      probe.commandHandlers.get(data.action)?.(data);
    }
    forward(data);
  });

  wrapContext("AudioContext");
  wrapContext("webkitAudioContext");
  wrapMediaPlayback();
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", startObserver, { once: true });
  } else {
    startObserver();
  }
  window.addEventListener("pagehide", () => probe.report(true), { once: true });
  queueMicrotask(probe.report);
})();
