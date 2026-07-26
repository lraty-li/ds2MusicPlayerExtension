const HELPER_MODE_KEY = "ds2.spotify.helperMode";
const DIAGNOSTICS_KEY = "ds2.spotify.diagnostics";
const query = new URLSearchParams(window.location.search);

if (query.get("helper_mode") === "1") {
  sessionStorage.setItem(HELPER_MODE_KEY, "1");
  if (query.get("diagnostics") !== "1") {
    sessionStorage.removeItem(DIAGNOSTICS_KEY);
  }
}
if (query.get("diagnostics") === "1") {
  sessionStorage.setItem(DIAGNOSTICS_KEY, "1");
}

const helperMode =
  query.get("helper_mode") === "1" ||
  sessionStorage.getItem(HELPER_MODE_KEY) === "1";
const diagnostics =
  !helperMode ||
  query.get("diagnostics") === "1" ||
  sessionStorage.getItem(DIAGNOSTICS_KEY) === "1";

window.__ds2HelperDiagnostics = diagnostics;
if (diagnostics && window.location.pathname === "/index.html") {
  window.location.replace(`/diagnostics.html${window.location.search}`);
} else {
  const entry = diagnostics ? "./app.js" : "./helper-runtime.js";
  import(entry).catch(() => {
    window.chrome?.webview?.postMessage("helper-runtime-load-error");
  });
}
