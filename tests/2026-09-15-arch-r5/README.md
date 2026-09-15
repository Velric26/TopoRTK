# R5 automatic pair negotiation — 0.11.16-arch-r5 deployed to both units (2026-09-15)

Implementation of the [architecture review](../../docs/architecture-review.md) R5 checkpoint: a protocol-breaking cutover that removes manual correction-session entry and gives the pair an automatic, current-boot-proven session on the selected medium. Both instruments were cut over **over OTA only** (no USB was attached); Unit B required the operator-authorized explicit unconfirmed-peer override because the two units were mid-protocol-change at that moment.

## Contract implemented

- **`pair_session`** (new, portable): `Engine`/`Snapshot` with a fixed ≤256-byte workspace, no Arduino/NVS/heap. Bootstrap rides RTM1 type-3 control envelopes on route 0 with the new 40-byte `PLC1` body (version, kind, unit/role, transport, sender/target boot, fresh challenge, echo, proposed session, attempt/revision reserved at 0) and the surrounding zero-padding rules preserved. The logical Rover opens an untargeted Hello; a solicited reply binds the observed boot and echoes its discovery token, which the Base retires at establishment, so a replayed or delayed Hello can never restart a proven session. Session ids stay in the existing production range (>999999) and are minted by the logical Base only after a fresh challenge/echo. Connectivity requires a rolling bidirectional heartbeat exchange within the existing four-second window; a same-boot outage retains the session, generation, replay high-water marks and queue state, while a proven boot/role change clears them.
- **`link_service`** (new): sole owner of the selected production link, the live `Bridge`, the peer socket and the bounded COM2 output. Transport selection is now a **local, persisted preference** in NVS namespace `topolink` (magic/version/range/CRC32 with write-and-readback; a failed readback is `recovery_required`, missing record keeps the historical Wi-Fi default). Production COM2 admission requires an established, connected pair session, so link state no longer depends on delivery packets.
- **Wi-Fi v3**: `WiFiTestPacket` and `WiFiRtcmHeader` carry sender/receiver boot ids, the session, unit and role; the packet magic/version/CRC and the one-time rejection of recognized legacy version-2 traffic are unchanged in shape. Replay protection is a session-scoped monotonic sequence; a session or peer-boot change resets the counters, and unselected-medium data is never admitted.
- **`peer_update`/OTA**: notice exchange now consumes the proven pair snapshot and rides the selected medium (route = session for both Wi-Fi and radio); discovery/UDP ownership and the old `correction_radio_restore` manual-rejoin path are gone. An update reboot keeps the remote notice attempt, so its "Reconnected" label still requires a proven current-boot link **and** fresh receiver quality.
- **Diagnostics and UI**: manual live-session entry (`Start new SiK session` / `Join Base SiK session`, session numbers, post-OTA `REJOIN SiK LINK`) is removed from the Diagnostics/Debug pages, the live hardware drivers and the living docs. The advanced synthetic matching test keeps its own six-digit run ids (R9 makes it automatic); radio diagnostics still require selecting Wi-Fi locally first.

## Software verification (all passing)

| Check | Result |
|---|---|
| `test/run_pair_session_tests.py` (new, production core) | startup orders on both media, lossy proof/backpressure, strict codec (padding/reserved/opcode/session), replay of every prior kind, replayed-Hello cannot rotate a live session, reboot isolation, same-role, outage retention, clock wrap |
| `test/run_update_tests.py` | notice wire/core, OTA service 23 cases, Wi-Fi and radio production pair/OTA notices incl. reboot isolation and quality recovery |
| `test/run_host_tests.py` | 8 firmware blocks incl. new Wi-Fi v3 identity/replay admission, status JSON, UI previews |
| `test/run_survey_tests.py`, `test/run_ota_http_tests.py` | unchanged and passing |
| Browser checks (Edge/Playwright) | web, OTA, diagnostic, Debug, GUI layout, survey — all passing; the layout harness gained the `/debug-nav.js` route and correct post-restructure paths it was missing |
| `pio run -e unit_a -e unit_b`, packaging | SUCCESS, 1,249,040 / 1,249,024 image bytes, version verified inside both `.tpk` files |

## OTA deployment

| Unit | Path | Evidence |
|---|---|---|
| A / Base | Acknowledged notice (peer still on R4) → verified new boot | `firmware/.pio/ota-live-1789489240146` |
| B / Rover | First attempt refused (peer on R5 cannot acknowledge an R4 notice; nothing flashed), then the **operator-authorized unconfirmed override** | `firmware/.pio/ota-live-1789489278869` (refusal), `firmware/.pio/ota-live-1789489611128` (completed, `unconfirmedOverride: true`) |

`test/run_ota_live.cjs` gained an explicit `--allow-unconfirmed` flag: without it an unacknowledged preparation still aborts before writing flash, and the flag records itself in the evidence. Saved survey state, jobs and Debug default were verified unchanged on both units after the cutover.

## Hardware acceptance performed

- Automatic negotiation after Unit B's post-update reboot: both units report `peer_connected: true`, `pair_state: connected` and the **same nonzero session** (`102127552`), with each unit's `peer_boot` equal to the other's boot — i.e. bidirectional current-boot proof with no operator input and no session numbers.
- Radio cutover at runtime: selecting Radio locally on both instruments negotiated a fresh session over SiK (`197291837`) within seconds while the Rover status JSON reported `transport: "SiK RADIO"`, `rssi_dbm: null` (no fabricated Wi-Fi dBm) and `correction_state: waiting_observations` (no receiver differential data indoors).
- Returning both to Wi-Fi negotiated another fresh session (`3923464542`); the previous sessions were not reused.
- Saved state intact throughout (A: 2 records/2 jobs; B: 0 records, collection idle).

## Outstanding

- Cold-boot matrix with the Base–Rover Wi-Fi absent (radio-only), deliberate replay injection and a role change while idle still need restarts and therefore USB or a scheduled power cycle; only the Base-first-after-B-reboot ordering was exercised here.
- Outdoor SiK RTK/recovery acceptance remains its own gate, unchanged by this checkpoint.
- Next per the review: **R6** (pair-wide durable selection, correlated operation outcomes, Settings API) — R5 provides only the local persisted preference, no pair-wide commit.
