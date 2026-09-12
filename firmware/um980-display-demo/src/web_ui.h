#pragma once

// Self-contained, flash-resident UI: no CDN, external font, or browser storage.
static const char kWebStatusPage[] = R"TOPOHTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light"><title>TopoRTK · Rover status</title>
<link rel="icon" href="data:,">
<style>
:root{color-scheme:dark;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#0b0e12;color:#f5f7fa;font-size:16px;--muted:#abb6c5;--line:#343e4b;--accent:#79d9ff}
*{box-sizing:border-box}body{margin:0}button,summary{font:inherit}button{min-height:44px;padding:.55rem 1rem;border:1px solid var(--line);border-radius:8px;background:#1b2531;color:#fff;cursor:pointer}button:hover{border-color:var(--accent)}button:disabled{opacity:.65;cursor:wait}:focus-visible{outline:3px solid var(--accent);outline-offset:4px}
.shell{max-width:1120px;margin:auto;padding:28px 24px 24px}header{display:flex;justify-content:space-between;align-items:center;gap:20px;padding-bottom:26px;border-bottom:1px solid var(--line)}.brand{font-size:1.35rem;font-weight:750;letter-spacing:-.04em}.brand span{color:var(--accent)}.tag{color:var(--muted);font-size:.875rem;margin-top:5px}.header-actions{display:flex;align-items:center;gap:14px}.unit{border:1px solid var(--line);border-radius:8px;padding:.65rem .8rem;font-size:.875rem;white-space:nowrap}
.connection{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;color:var(--muted);font-size:.875rem;padding:20px 0 14px}.connection strong{color:#eef4fc;font-weight:600}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#8290a0;margin-right:8px}.dot[data-live="true"]{background:var(--accent)}
.readiness{background:#262e38;border:1px solid #465365;border-radius:12px;padding:22px 24px;display:flex;align-items:center;justify-content:space-between;gap:24px}.readiness[data-ready="true"]{background:#10462c;border-color:#36865c}.eyebrow{text-transform:uppercase;letter-spacing:.13em;font-size:.875rem;color:#d0d9e4;margin:0 0 6px;font-weight:600}h1{font-size:clamp(1.8rem,5vw,2.8rem);line-height:1.08;letter-spacing:-.04em;margin:0}.local-time{text-align:right;font-variant-numeric:tabular-nums}.local-time strong{display:block;font-size:1.6rem;font-weight:650}.local-time small{color:#d0d9e4;font-size:.875rem}
.metrics{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin:20px 0}.metric{min-width:0;min-height:160px;border:1px solid var(--line);border-radius:12px;padding:20px 24px;background:#121922}.metric h2{font-size:.875rem;text-transform:uppercase;letter-spacing:.1em;color:var(--muted);font-weight:600;margin:0 0 18px}.value{font-size:clamp(1.8rem,4vw,2.4rem);font-weight:680;letter-spacing:-.04em;line-height:1.1;overflow-wrap:anywhere;font-variant-numeric:tabular-nums}.value[data-good="true"]{color:#80edb0}.sub{color:var(--muted);font-size:.875rem;margin:10px 0 0;line-height:1.5}
.warning{border:1px solid #796334;border-left:4px solid #efbd59;background:#211d16;border-radius:8px;padding:16px 20px;margin-bottom:26px}.warning[data-severity="error"]{border-color:#b77a79;border-left-color:#ff9a94;background:#281a1c}.warning h2{font-size:1.05rem;margin:0 0 6px}.warning p{margin:0;color:#e3daca;line-height:1.5}
details{border-top:1px solid var(--line);border-bottom:1px solid var(--line)}summary{cursor:pointer;min-height:52px;padding:15px 0;font-weight:600}.diagnostics{display:grid;grid-template-columns:1fr 1fr;gap:0 40px;margin:0;padding-bottom:12px}.diagnostics div{display:flex;justify-content:space-between;gap:20px;border-top:1px solid #252e39;padding:12px 0;font-size:.875rem;min-width:0}.diagnostics dt{color:var(--muted)}.diagnostics dd{margin:0;text-align:right;overflow-wrap:anywhere;font-variant-numeric:tabular-nums}footer{display:flex;justify-content:space-between;gap:15px;flex-wrap:wrap;font-size:.875rem;color:var(--muted);padding-top:20px}noscript p{border:1px solid #efbd59;padding:16px}
@media(max-width:560px){.shell{padding:18px 16px}header{gap:12px;padding-bottom:18px}.header-actions{gap:8px}.unit{padding:.6rem}.readiness{padding:20px 18px;align-items:flex-start;flex-direction:column;gap:16px}.local-time{text-align:left}.local-time strong{display:inline;font-size:1.1rem;margin-right:8px}.metrics{gap:12px}.metric{padding:18px 14px;min-height:154px}.metric h2{font-size:.8rem;letter-spacing:.04em}.value{font-size:1.65rem}.diagnostics{grid-template-columns:1fr}}
@media(max-width:440px){.metrics{grid-template-columns:1fr}.metric{min-height:130px}}
@media(max-width:350px){.header-actions{flex-direction:column;align-items:flex-end}}

/* Shared field-interface palette; green remains a verified-state indicator. */
:root{color-scheme:light;background:#eef2f5;color:#172d3b;--muted:#526674;--line:#d2dde4;--accent:#166546}.shell{max-width:1120px;padding-top:30px}.brand{letter-spacing:-.04em;font-size:1.6rem}.brand span{color:#173c50}button{background:#fff;color:#173c50;border-color:#9cabb6}button:hover{background:#e3edf1}.connection strong{color:#173c50}.survey-link{display:block;text-align:center;border-radius:10px;background:#173c50;color:#fff;text-decoration:none;padding:14px 20px;font-weight:650}.readiness{background:#e0e8ee;border-color:#bacbd6;color:#173c50;border-radius:14px}.readiness[data-ready="true"]{background:#e0f3e6;color:#164d2d;border-color:#89bfa0}.eyebrow,.local-time small{color:#425e6d}.metric{background:#fff;box-shadow:0 3px 12px #15374705;border-radius:14px}.value[data-good="true"]{color:#166546}.warning{background:#fff7e6;border-color:#d4b778;color:#684708}.warning p{color:#684708}.warning[data-severity="error"]{background:#fff0ed;border-color:#c5968d;color:#822d24}.warning[data-severity="error"] p{color:#822d24}.diagnostics div{border-color:var(--line)}summary{color:#264b60}.dot[data-live="true"]{background:#166546}@media(max-width:560px){.metric h2{font-size:.875rem}.shell{padding:18px 16px}.readiness{flex-direction:row;align-items:center;gap:12px}.local-time{text-align:right}.local-time strong{display:block;margin:0}.local-time small{display:block}h1{font-size:1.8rem}.header-actions{flex-wrap:wrap;justify-content:flex-end}.metrics{grid-template-columns:1fr 1fr}.metric{min-height:150px}.value{font-size:1.45rem}}@media(max-width:380px){.metrics{grid-template-columns:1fr}.readiness{flex-direction:column;align-items:start}.local-time{text-align:left}}
</style>
</head>
<body><div class="shell">
<header><div><div class="brand">Topo<span>RTK</span></div><div class="tag">Rover status</div></div><div class="header-actions"><span class="unit" id="unit">UNIT —</span><button id="refresh" type="button">Refresh</button></div></header>
<noscript><p>Enable JavaScript to show live instrument status.</p></noscript>
<main>
<div class="connection"><span><i id="live-dot" class="dot" data-live="false" aria-hidden="true"></i><strong id="connection" role="status">Connecting to Rover…</strong></span><span id="updated">Waiting for first update</span></div>
<p class="sub" style="margin:0 0 16px">Instrument Wi-Fi works without Internet. Choose “Stay connected” if Android asks.</p>
<p style="margin:0 0 20px"><a href="/survey" class="survey-link">Open survey jobs →</a> · <a href="/diagnostics">Link test</a></p>
<section id="readiness" class="readiness" data-ready="false" aria-labelledby="ready-title"><div><p class="eyebrow">Instrument readiness</p><h1 id="ready-title">CONNECTING</h1></div><div class="local-time"><strong id="local-time">TIME WAIT</strong><small>Local · UTC−6</small></div></section>
<div class="metrics">
<section class="metric"><h2>Correction link</h2><div class="value" id="link">—</div><p class="sub" id="link-sub">Waiting for instrument</p></section>
<section class="metric"><h2>GNSS solution</h2><div class="value" id="fix">—</div><p class="sub" id="fix-sub">Waiting for instrument</p></section>
<section class="metric"><h2>Horizontal uncertainty</h2><div class="value" id="accuracy">—</div><p class="sub">Receiver estimate · 1DRMS</p></section>
<section class="metric"><h2>Link signal</h2><div class="value" id="quality">—</div><p class="sub" id="rssi">Waiting for instrument</p></section>
</div>
<section id="warning" class="warning" aria-labelledby="warning-title"><h2 id="warning-title">Waiting for live status</h2><p id="warning-detail">Keep this device connected to the Rover's local network.</p></section>
<details><summary>Connection &amp; GNSS details</summary><dl class="diagnostics">
<div><dt>Network mode</dt><dd id="transport">—</dd></div><div><dt>Receiver profile</dt><dd id="profile">—</dd></div>
<div><dt>GNSS age</dt><dd id="gga-age">—</dd></div><div><dt>Corrections age</dt><dd id="rtcm-age">—</dd></div>
<div><dt>Peer age</dt><dd id="peer-age">—</dd></div><div><dt>Satellites</dt><dd id="satellites">—</dd></div>
<div><dt>Received packets</dt><dd id="packets">—</dd></div><div><dt>Packet gaps / errors</dt><dd id="errors">—</dd></div>
<div><dt>RTCM frames received</dt><dd id="rtcm-frames">—</dd></div><div><dt>GNSS UTC</dt><dd id="utc">TIME WAIT</dd></div>
</dl></details>
<details><summary>Connect a phone or tablet</summary><div class="sub" style="margin:0 0 18px">
<p>On the Rover touchscreen, open <strong>Link → Phone / Tablet</strong>. Join the Wi-Fi shown there and tap <strong>Show key</strong> for its password.</p>
<p>Open <strong id="phone-url">the address on the Rover screen</strong> in Chrome. This network provides instrument access only. Keep the connection when Android reports no Internet.</p>
<p id="phone-network">The Rover screen shows the current network name and address.</p>
</div></details>
</main><footer><span>Local connection · Updates every second</span><span>Read-only · UI 0.8.0</span></footer>
</div>
<script>
'use strict';
const el = id => document.getElementById(id);
const set = (id, value) => { el(id).textContent = value; };
const age = ms => ms === null ? 'Not received' : (ms / 1000).toFixed(1) + ' s';
let busy = false, lastSuccess = 0, lastSample = '', sampleChangedAt = 0, live = false;
function offline(reason) {
  live = false; el('live-dot').dataset.live = 'false'; el('readiness').dataset.ready = 'false';
  set('connection', 'Rover connection lost'); set('ready-title', 'STATUS UNAVAILABLE');
  for (const id of ['link','fix','accuracy','quality']) { set(id, '—'); el(id).dataset.good = 'false'; }
  set('link-sub', 'Current link state unknown'); set('fix-sub', 'Current GNSS state unknown'); set('rssi', 'Current signal unknown');
  set('local-time', 'TIME WAIT'); set('utc', 'TIME WAIT');
  for (const id of ['transport','profile','gga-age','rtcm-age','peer-age','satellites','packets','errors','rtcm-frames']) set(id, '—');
  set('phone-url','the address on the Rover screen'); set('phone-network','The Rover screen shows the current network name and address.');
  el('warning').dataset.severity = 'error'; set('warning-title', 'Reconnect to the Rover'); set('warning-detail', reason);
}
function render(s) {
  live = true; el('live-dot').dataset.live = 'true'; set('connection', 'Live from Rover');
  set('unit', 'UNIT ' + s.device.unit); el('readiness').dataset.ready = String(s.state.ready);
  set('ready-title', s.state.ready ? 'READY' : 'NOT READY');
  set('local-time', s.time.local ? s.time.local.slice(11,19) : 'TIME WAIT');
  set('link', s.link.connected ? 'CONNECTED' : 'NO LINK'); el('link').dataset.good = String(s.link.connected);
  set('link-sub', s.link.connected ? 'Receiving peer heartbeat' : 'No current base heartbeat');
  set('fix', s.gnss.fix); el('fix').dataset.good = String(s.state.gps_fixed);
  set('fix-sub', s.gnss.online ? (s.gnss.gga_age_ms === null ? 'Waiting for GNSS data' : 'GNSS data age ' + age(s.gnss.gga_age_ms)) : 'GNSS receiver offline');
  set('accuracy', s.gnss.horizontal_uncertainty_label); set('quality', s.link.quality || '—');
  set('rssi', s.link.rssi_dbm === null ? 'No current signal' : s.link.rssi_dbm + ' dBm · Wi-Fi signal');
  el('warning').dataset.severity = s.warning.severity; set('warning-title', s.warning.title); set('warning-detail', s.warning.detail);
  set('transport', s.link.transport); set('profile', s.device.profile); set('gga-age', age(s.gnss.gga_age_ms));
  set('rtcm-age', age(s.link.correction_age_ms)); set('peer-age', age(s.link.peer_age_ms));
  set('satellites', s.gnss.satellites === null ? '—' : String(s.gnss.satellites));
  set('packets', String(s.link.received_packets)); set('errors', s.link.sequence_gaps + ' / ' + s.link.invalid_packets);
  set('rtcm-frames', String(s.link.rtcm_received_frames)); set('utc', s.time.utc || 'TIME WAIT');
  set('phone-url',s.phone_wifi?.available ? 'http://' + s.phone_wifi.address : 'the address on the Rover screen');
  set('phone-network',s.phone_wifi?.available ? 'Wi-Fi: ' + s.phone_wifi.ssid + ' · Connected devices: ' + s.phone_wifi.clients : 'Check the Rover screen for phone Wi-Fi availability.');
}
async function refresh() {
  if (busy) return;
  busy = true; el('refresh').disabled = true;
  const controller = new AbortController(); const timeout = setTimeout(() => controller.abort(), 1800);
  try {
    const response = await fetch('/api/v1/status', {cache:'no-store', signal:controller.signal});
    if (!response.ok) throw new Error(response.status === 409 ? 'This instrument is no longer the Rover. Open the current Rover address.' : 'No fresh status. Check the Rover power and local network.');
    const s = await response.json();
    if (s.api_version !== 1 || s.device.role !== 'ROVER' || typeof s.state.ready !== 'boolean' || !Number.isFinite(s.uptime_ms)) throw new Error('Incompatible status response. Reload this page.');
    const now = performance.now(), key = s.boot_id + ':' + s.uptime_ms;
    if (key !== lastSample) { lastSample = key; sampleChangedAt = now; }
    else if (now - sampleChangedAt > 2500) throw new Error('Instrument status stopped updating. Check the Rover.');
    render(s); lastSuccess = performance.now(); set('updated', 'Updated just now');
  } catch (error) {
    offline(error.name === 'AbortError' || error.name === 'TypeError'
      ? 'Cannot reach the Rover. Check its power and reconnect to its local network.'
      : error.name === 'SyntaxError' ? 'The Rover returned unreadable status. Retrying…' : error.message);
  }
  finally { clearTimeout(timeout); busy = false; el('refresh').disabled = false; }
}
el('refresh').addEventListener('click', refresh);
document.addEventListener('visibilitychange', () => { if (!document.hidden) { if (lastSuccess && performance.now()-lastSuccess > 3000) offline('Fetching a fresh instrument snapshot…'); refresh(); } });
setInterval(() => {
  if (!lastSuccess) return;
  const elapsed = performance.now() - lastSuccess;
  set('updated', 'Last update ' + Math.floor(elapsed/1000) + ' s ago');
  if (live && elapsed > 3000) offline('The status is stale. Reconnecting to the Rover…');
}, 250);
setInterval(() => { if (!document.hidden) refresh(); }, 1000);
refresh();
</script></body></html>)TOPOHTML";
