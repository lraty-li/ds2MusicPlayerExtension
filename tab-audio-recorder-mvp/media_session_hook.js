(function installDs2MediaSessionHook() {
  if (window.__ds2MediaSessionHookInstalled) return;
  window.__ds2MediaSessionHookInstalled = true;
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

  patchSession(navigator.mediaSession);
  window.__ds2DispatchMediaSessionAction = function dispatchMediaSessionAction(action) {
    const handler = window.__ds2MediaSessionHandlers &&
      window.__ds2MediaSessionHandlers[action];
    if (typeof handler !== "function") return false;
    try {
      const result = handler({ action });
      if (result && typeof result.catch === "function") {
        result.catch((error) => {
          console.error("DS2 MediaSession handler rejected:", error);
        });
      }
      return true;
    } catch (error) {
      console.error("DS2 MediaSession handler failed:", error);
      return false;
    }
  };
})();
