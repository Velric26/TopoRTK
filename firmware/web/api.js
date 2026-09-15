// One controller client for every page: control token, ownership state, JSON
// requests and the browser's client identity. Loaded before page scripts.
//
//   topoToken()                 current control token ('' when not claimed)
//   topoOwner()                 true while a token is held
//   topoOnControl(fn)           subscribe to claim/release/takeover; returns an unsubscribe
//   topoRequest(path, data?)    JSON request; data present => POST; resolves {status, ok, body, headers}
//   topoClaim()                 claim control; resolves the token or throws
//   topoRelease()               release control (best effort)
//   topoClientId()              stable id for this browser session
//
// A 401 from any request means another controller took over: the token is dropped
// and subscribers are notified, so no page keeps acting on a stale bearer.
(() => {
  'use strict';
  const TOKEN_KEY = 'topoControlToken';
  const CLIENT_KEY = 'topoClientId';
  const read = (key) => { try { return sessionStorage.getItem(key) || ''; } catch { return ''; } };
  const write = (key, value) => {
    try { value ? sessionStorage.setItem(key, value) : sessionStorage.removeItem(key); } catch { /* private mode */ }
  };

  let token = read(TOKEN_KEY);
  const listeners = new Set();
  function changed() { for (const listener of [...listeners]) { try { listener(); } catch { /* page listener */ } } }
  function setToken(value) {
    const next = value || '';
    if (next === token) return;
    token = next;
    write(TOKEN_KEY, token);
    changed();
  }

  function clientId() {
    let id = read(CLIENT_KEY);
    if (!id) {
      id = Array.from(crypto.getRandomValues(new Uint8Array(16)), (byte) => byte.toString(16).padStart(2, '0')).join('');
      write(CLIENT_KEY, id);
    }
    return id;
  }

  async function request(path, data, options = {}) {
    const headers = {};
    if (data !== undefined) headers['Content-Type'] = 'application/json';
    if (token) headers.Authorization = 'Bearer ' + token;
    const response = await fetch(path, {
      method: data === undefined ? 'GET' : 'POST',
      cache: 'no-store',
      headers,
      body: data === undefined ? undefined : JSON.stringify(data),
      signal: options.signal || AbortSignal.timeout(options.timeout || 4000),
    });
    if (response.status === 401) setToken('');
    const text = await response.text();
    let body = null;
    try { body = text ? JSON.parse(text) : null; } catch { body = null; }
    return { status: response.status, ok: response.ok, headers: response.headers, body };
  }

  window.topoToken = () => token;
  window.topoOwner = () => !!token;
  window.topoOnControl = (listener) => { listeners.add(listener); return () => listeners.delete(listener); };
  window.topoRequest = request;
  window.topoClientId = clientId;
  window.topoClaim = async () => {
    const response = await request('/api/v1/control', { client: clientId() });
    if (!response.ok || !response.body || !response.body.token)
      throw new Error((response.body && response.body.error) || ('control ' + response.status));
    setToken(response.body.token);
    return response.body.token;
  };
  window.topoRelease = async () => {
    try { if (token) await request('/api/v1/control/release', {}); } catch { /* best effort */ }
    setToken('');
  };
  window.topoForget = () => setToken('');
})();
