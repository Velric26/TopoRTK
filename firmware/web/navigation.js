// Shared page navigation. Pages declare their own cross-page tabs — a container
// marked [data-navigation] plus, where the Debug page applies, `#debugTab`,
// `#debugAvailability` and `#debugPeerNote` — and this script marks the current
// page and gates the Debug tab on the instrument's Debug state. It never injects
// markup, so each page owns what it shows.
(() => {
  'use strict';
  const here = location.pathname === '/ui/v1/' ? '/' : location.pathname;
  for (const link of document.querySelectorAll('[data-navigation] a[href]')) {
    const target = new URL(link.getAttribute('href'), location.origin).pathname;
    const current = target === here || (target === '/survey' && (here === '/' || here.startsWith('/survey')));
    if (current) link.setAttribute('aria-current', 'page');
    else link.removeAttribute('aria-current');
  }

  const button = document.querySelector('#debugTab');
  if (!button) return;
  // A gated navigation control must be natively disableable: `disabled` has no
  // effect on an anchor, so the click is wired here and only fires when enabled.
  button.onclick = () => { if (!button.disabled) location.assign('/debug'); };
  const note = document.querySelector('#debugAvailability');
  const peerNote = document.querySelector('#debugPeerNote');
  const unavailable = 'Debug unavailable. On the instrument touchscreen: Setup → Debug → Enable Debug.';
  if (note) note.textContent = unavailable;
  let previous = null;
  let advanced = 0;
  async function check() {
    try {
      const response = await fetch('/api/v1/debug', { cache: 'no-store', signal: AbortSignal.timeout(2500) });
      if (!response.ok) throw Error();
      const status = await response.json();
      if (status.version !== 1 || !Number.isFinite(status.uptime_ms)) throw Error();
      const key = status.boot_id + ':' + status.uptime_ms;
      if (key !== previous) { previous = key; advanced = Date.now(); }
      if (Date.now() - advanced > 4000) throw Error();
      button.disabled = !status.enabled;
      if (peerNote) {
        peerNote.hidden = !status.peer_status;
        peerNote.textContent = status.peer_status || '';
      }
      if (note)
        note.textContent = status.enabled
          ? 'Debug available · Passive monitoring does not interrupt surveying.'
          : unavailable;
    } catch {
      button.disabled = true;
      if (note)
        note.textContent = 'Debug unavailable while disconnected. Reconnect, then enable it on the touchscreen: Setup → Debug → Enable Debug.';
      if (peerNote) peerNote.hidden = true;
    } finally {
      setTimeout(check, 2500);
    }
  }
  check();
})();
