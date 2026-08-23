(function registerYoutubeMusicAdapter() {
  const tools = window.__ds2MediaTools;
  if (!tools) return;

  window.__ds2RegisterMediaAdapter({
    name: "youtubeMusic",
    matches(host) {
      return host === "music.youtube.com";
    },
    probe(command) {
      const button = findPlayPauseButton();
      const state = readButtonState(button);
      const wants = command === "pause" ? state === "playing" : state === "paused";
      return {
        score: button ? (wants ? 280 : 150) : 0,
        button: button ? 1 : 0,
        state
      };
    },
    async control(command) {
      const button = findPlayPauseButton();
      const state = readButtonState(button);
      const result = tools.emptyResult();
      result.media = button ? 1 : 0;
      result.debug = `youtubeMusic button=${button ? 1 : 0} state=${state}`;
      if (!button) return result;

      const target = command === "pause" ? "paused" : "playing";
      if (state === target) {
        result.already = 1;
        return result;
      }

      button.click();
      result.clicked = 1;
      if (await tools.waitFor(() => readButtonState(findPlayPauseButton()) === target, 1000)) {
        result.changed = 1;
      } else {
        result.debug += ` after=${readButtonState(findPlayPauseButton())}`;
      }
      return result;
    }
  });

  function findPlayPauseButton() {
    const bar = deepFind(document, "ytmusic-player-bar") || document;
    const selectors = [
      "#play-pause-button",
      ".play-pause-button",
      "tp-yt-paper-icon-button[aria-label]",
      "tp-yt-paper-icon-button[title]",
      "button[aria-label]",
      "button[title]",
      "yt-icon-button",
      "paper-icon-button"
    ];
    for (const selector of selectors) {
      const buttons = deepFindAll(bar, selector);
      for (const button of buttons) {
        if (looksLikePlayPause(button)) return button;
      }
    }
    return null;
  }

  function looksLikePlayPause(element) {
    const text = readElementText(element).toLowerCase();
    return /play|pause|播放|暂停|再生|一時停止|play_arrow/.test(text) ||
      /play-pause/.test(String(element.id || element.className || ""));
  }

  function readButtonState(button) {
    if (!button) return "";
    const text = readElementText(button).toLowerCase();
    if (/pause|暂停|一時停止/.test(text)) return "playing";
    if (/play_arrow|play|播放|再生/.test(text)) return "paused";
    return "";
  }

  function readElementText(element, depth = 0) {
    if (!element || depth > 4) return "";
    const parts = [
      element.getAttribute && element.getAttribute("aria-label"),
      element.getAttribute && element.getAttribute("title"),
      element.getAttribute && element.getAttribute("data-title"),
      element.getAttribute && element.getAttribute("icon"),
      element.id,
      element.className,
      element.textContent
    ];
    for (const child of Array.from(element.children || [])) {
      parts.push(readElementText(child, depth + 1));
    }
    if (element.shadowRoot) {
      for (const child of Array.from(element.shadowRoot.children || [])) {
        parts.push(readElementText(child, depth + 1));
      }
    }
    return parts.filter(Boolean).join(" ");
  }

  function deepFind(root, selector) {
    return deepFindAll(root, selector)[0] || null;
  }

  function deepFindAll(root, selector) {
    const result = [];
    visit(root);
    return result;

    function visit(node) {
      if (!node || !node.querySelectorAll) return;
      result.push(...node.querySelectorAll(selector));
      for (const element of node.querySelectorAll("*")) {
        if (element.shadowRoot) visit(element.shadowRoot);
      }
    }
  }
})();
