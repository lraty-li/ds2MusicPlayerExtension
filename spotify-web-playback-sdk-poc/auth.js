const CLIENT_ID_KEY = "ds2.spotify.poc.clientId";
const TOKEN_KEY = "ds2.spotify.poc.token";
const VERIFIER_KEY = "ds2.spotify.poc.pkceVerifier";
const STATE_KEY = "ds2.spotify.poc.oauthState";
const SCOPES = ["streaming", "user-read-email", "user-read-private"];

export function redirectUri() {
  return new URL("/", window.location.origin).href;
}

export function loadClientId() {
  return localStorage.getItem(CLIENT_ID_KEY) || "";
}

export function saveClientId(value) {
  const clientId = String(value || "").trim();
  if (!clientId) throw new Error("Client ID 不能为空");

  const previous = loadClientId();
  if (previous && previous !== clientId) {
    localStorage.removeItem(TOKEN_KEY);
  }
  localStorage.setItem(CLIENT_ID_KEY, clientId);
  return clientId;
}

export function hasStoredAuthorization() {
  const token = readToken();
  return !!(token && token.refreshToken);
}

export function clearAuthorization() {
  localStorage.removeItem(TOKEN_KEY);
  sessionStorage.removeItem(VERIFIER_KEY);
  sessionStorage.removeItem(STATE_KEY);
}

export async function beginAuthorization(clientIdValue) {
  const clientId = saveClientId(clientIdValue);
  const verifier = randomString(64);
  const state = randomString(32);
  const challenge = await createChallenge(verifier);

  sessionStorage.setItem(VERIFIER_KEY, verifier);
  sessionStorage.setItem(STATE_KEY, state);

  const url = new URL("https://accounts.spotify.com/authorize");
  url.search = new URLSearchParams({
    client_id: clientId,
    response_type: "code",
    redirect_uri: redirectUri(),
    scope: SCOPES.join(" "),
    state,
    code_challenge_method: "S256",
    code_challenge: challenge
  }).toString();
  window.location.assign(url);
}

export async function finishAuthorizationCallback() {
  const query = new URLSearchParams(window.location.search);
  const error = query.get("error");
  const code = query.get("code");
  if (!error && !code) return false;

  const callbackState = query.get("state") || "";
  const expectedState = sessionStorage.getItem(STATE_KEY) || "";
  const verifier = sessionStorage.getItem(VERIFIER_KEY) || "";
  cleanCallbackUrl();

  if (error) throw new Error(`Spotify 拒绝授权：${error}`);
  if (!callbackState || callbackState !== expectedState) {
    throw new Error("OAuth state 校验失败，请重新授权");
  }
  if (!verifier) throw new Error("缺少 PKCE verifier，请在同一标签页重新授权");

  const clientId = loadClientId();
  const token = await requestToken({
    client_id: clientId,
    grant_type: "authorization_code",
    code,
    redirect_uri: redirectUri(),
    code_verifier: verifier
  });
  storeToken(token, clientId, "");
  sessionStorage.removeItem(VERIFIER_KEY);
  sessionStorage.removeItem(STATE_KEY);
  return true;
}

export async function getValidAccessToken() {
  const clientId = loadClientId();
  const stored = readToken();
  if (!clientId || !stored || stored.clientId !== clientId) {
    throw new Error("尚未完成 Spotify 授权");
  }

  if (stored.accessToken && stored.expiresAt > Date.now() + 60_000) {
    return stored.accessToken;
  }
  if (!stored.refreshToken) throw new Error("授权已过期，请重新授权");

  try {
    const token = await requestToken({
      client_id: clientId,
      grant_type: "refresh_token",
      refresh_token: stored.refreshToken
    });
    return storeToken(token, clientId, stored.refreshToken).accessToken;
  } catch (error) {
    clearAuthorization();
    throw error;
  }
}

async function requestToken(parameters) {
  const response = await fetch("https://accounts.spotify.com/api/token", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(parameters)
  });
  const body = await response.json().catch(() => ({}));
  if (!response.ok || !body.access_token) {
    throw new Error(body.error_description || body.error || `Token 请求失败：HTTP ${response.status}`);
  }
  return body;
}

function storeToken(response, clientId, previousRefreshToken) {
  const stored = {
    clientId,
    accessToken: response.access_token,
    refreshToken: response.refresh_token || previousRefreshToken,
    expiresAt: Date.now() + Number(response.expires_in || 3600) * 1000,
    scope: response.scope || ""
  };
  localStorage.setItem(TOKEN_KEY, JSON.stringify(stored));
  return stored;
}

function readToken() {
  try {
    return JSON.parse(localStorage.getItem(TOKEN_KEY) || "null");
  } catch (_) {
    return null;
  }
}

function cleanCallbackUrl() {
  window.history.replaceState({}, document.title, redirectUri());
}

function randomString(length) {
  const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  const bytes = crypto.getRandomValues(new Uint8Array(length));
  return Array.from(bytes, (value) => alphabet[value % alphabet.length]).join("");
}

async function createChallenge(verifier) {
  const bytes = new TextEncoder().encode(verifier);
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return btoa(String.fromCharCode(...new Uint8Array(digest)))
    .replace(/=/g, "")
    .replace(/\+/g, "-")
    .replace(/\//g, "_");
}
