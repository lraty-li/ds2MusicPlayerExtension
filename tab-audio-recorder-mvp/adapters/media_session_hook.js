(function registerMediaSessionHookAdapter() {
  const tools = window.__ds2MediaTools;
  if (!tools) return;
  window.__ds2MediaSessionHandlers = window.__ds2MediaSessionHandlers || {};

  const actions = new Set([
    "play",
    "pause",
    "stop",
    "seekbackward",
    "seekforward",
    "seekto",
    "previoustrack",
    "nexttrack"
  ]);

  if (!window.__ds2MediaSessionHookInstalled) {
    window.__ds2MediaSessionHookInstalled = true;
    patchSession(navigator.mediaSession);
  }

  window.__ds2RegisterMediaAdapter({
    name: "mediaSessionHook",
    fallback: true,
    fallbackMetadata: true,
    matches() {
      return false;
    },
    probe(command) {
      const action = toAction(command);
      return hasHandler(action) ? { score: 140, hasSession: true } : null;
    },
    metadata() {
      const session = tools.readMediaSessionMetadata(100);
      if (session) return session;
      const title = tools.cleanTitle(document.title);
      return title ? {
        score: 50,
        title,
        artist: "",
        jacket: tools.readDocumentArtwork()
      } : null;
    },
    control(command) {
      const action = toAction(command);
      if (!hasHandler(action)) return null;
      const result = tools.emptyResult();
      if (dispatch(action)) result.changed = 1;
      return result;
    }
  });

  function patchSession(session) {
    if (!session || session.__ds2Patched || typeof session.setActionHandler !== "function") {
      return;
    }

    const original = session.setActionHandler.bind(session);
    session.setActionHandler = function setActionHandler(action, handler) {
      if (actions.has(action)) {
        window.__ds2MediaSessionHandlers[action] = typeof handler === "function" ? handler : null;
      }
      return original(action, handler);
    };
    session.__ds2Patched = true;
  }

  function toAction(command) {
    return command === "resume" ? "play" : "pause";
  }

  function hasHandler(action) {
    return typeof window.__ds2MediaSessionHandlers[action] === "function";
  }

  function dispatch(action) {
    const handler = window.__ds2MediaSessionHandlers[action];
    if (typeof handler !== "function") return false;
    try {
      const result = handler({ action });
      if (result && typeof result.catch === "function") result.catch(() => {});
      return true;
    } catch (_) {
      return false;
    }
  }
})();
