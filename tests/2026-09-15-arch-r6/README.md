# R6 durable pair-wide selection and Settings API — 0.11.17-arch-r6 (2026-09-15)

Implementation of the [architecture review](../../docs/architecture-review.md) R6 checkpoint: one pair-wide operation service on top of R5's automatic pairing, a coordinator that admits at most one operation, durable checked records, a correlated outcome surface and the Settings HTTP API.

> **Status:** software complete, both instruments flashed to `0.11.17-arch-r6` over USB, and the Wi-Fi-side hardware acceptance passed. A radio cutover cannot be demonstrated on the bench right now because **both SiK radios fail an ATI probe** ("no response"); the firmware handled that correctly as a pre-cutover target failure.

## Contract implemented

- **`src/link_operation.h/.cpp`** (new, portable, fixed ≤256-byte engine): pair-wide operations with a coordinator (always the Rover unit), `negotiating → running → restoring → succeeded/failed/cancelled/interrupted/recovery_required` states, bounded windows (20 s negotiation, 10 s apply, 10 s restoration, 500 ms retries, 60 s cancellation tombstones) and the operation wire codec. Operation kinds (`Request/Prepare/Ready/Commit/Done`) ride the PLC1 body on the operation's **own** medium; the reserved attempt/revision bytes carry the coordinator-assigned revision, and a denial echoes a forwarded request that may carry none.
- **Durable records** in NVS `topolink`: `confirmed{transport, revision}` and `pending{kind, target, previous, tag, revision, committed}` with magic/version/range/CRC32 and **write-and-readback**; the R5 single-key `selection` record is migrated on load. A record that never committed is reported `interrupted` at boot with the confirmed selection kept authoritative; a readback failure becomes `recovery_required`, which inhibits readiness and retains the durable pending record.
- **`link_service`**: a second (staging) pair engine proves the candidate medium while production keeps running, then the proven candidate is **adopted** as the production session at commit (no second handshake); the negotiated proof carries the same replay/queue reset rules. Operation control is received on both media, but only the selected medium admits production data. Local recovery selection (`diagnostics` `corrections` op) now refuses while a pair-wide operation is admitted and clears an interrupted record.
- **Settings HTTP API** (`GET`/`POST /api/v1/settings`, route capacity raised 22 → 24): `version`, `boot_id`, `uptime_ms`, `revision`, `selected_transport`, `candidate_transport`, `peer_connected`, `corrections_fresh`, `operation{id,tag,kind,transport,previous_transport,state,committed,coordinator,phase_remaining_ms,reason}` and `last_tests{wifi,sik}`. POST accepts `link.select`, `link.cancel` (202 queued) and refuses `link.test` with 503 until R9 owns it; malformed bodies 400, 401/403 for access, 409 for stale revision/busy/conflicting id/cancelled tombstone, 503 for known storage failure. HTTP never writes NVS; a request that HTTP accepted and the main loop later refused is published as that operation's `failed` outcome rather than a retroactive status.
- **Diagnostics/settings coupling**: the settings snapshot needs the latest report per medium without copying a multi-kilobyte report into its bounded document, so the diagnostics service maintains a small per-medium summary, refreshed only when the stored report changes.

## Verification (all passing)

| Check | Result |
|---|---|
| `test/run_link_operation_tests.py` (new, production core) | admission staleness/busy/conflicting-id/idempotent repeat, no-op selection, unreachable peer leaves the selection untouched, cancel before and after commit, restoration after a failed cutover, `recovery_required`, delegate forwarding, reboot-interrupted reporting, PLC1 operation wire boundaries |
| `test/run_settings_http_tests.py` (new, sliced production handlers) | 202/400/401/403/409/503 cases for select, cancel, test-op, stale, busy, conflict, cancelled, storage, origin, malformed, oversize, unreadable, missing fields, GET and GET-unavailable |
| `test/run_pair_session_tests.py`, `test/run_update_tests.py`, `test/run_host_tests.py`, `test/run_survey_tests.py`, `test/run_ota_http_tests.py` | unchanged and passing |
| `pio run -e unit_a -e unit_b`, packaging | SUCCESS; 1,260,944-byte images, `0.11.17-arch-r6` verified inside both `.tpk` files |

## Hardware deployment and acceptance (2026-09-15)

Both instruments were flashed over **USB** (COM4 Unit A, COM10 Unit B) with the signed-by-hash application image; the earlier OTA attempts of the same image had failed mid-transfer on the bench link (see *Blocked transport* below). Saved state survived both flashes: Unit A reports `BASE` with 2 records and 2 jobs, Unit B `ROVER` with 0 records, collection idle, Debug on. After the two reboots the pair renegotiated automatically (fresh session `1406924330`, `peer_connected: true` on both, `candidate_transport: null`).

Acceptance run against the deployed image (both roles, over the real HTTP API):

| Step | Result |
|---|---|
| `GET /api/v1/settings` on both roles | `version 1`, equal `revision 0`, `selected_transport "wifi"`, `peer_connected true`, `candidate_transport null`, `last_tests {"wifi":null,"sik":null}` |
| No-op `link.select wifi` on the coordinator | 202 → `succeeded/applied`, `revision` unchanged (no commit, no revision moved) |
| Same request **forwarded from the Base** | 202 → `succeeded/applied`, i.e. the Base-side request reaches the Rover coordinator |
| `link.select` with a stale revision | 409 `stale_revision` |
| `link.test` before R9 | 503 `test_operation_not_available` |
| Malformed id / missing confirmation | 400 `request_refused` / 400 `confirm_required` |
| `link.cancel` of an admitted operation | 202 → `cancelled/cancelled` on both units, selection and revision unchanged |
| Radio candidate without live radios | `failed/peer_unreachable`, both units stayed on Wi-Fi, revisions unchanged, pair still `connected` — a pre-cutover target failure leaves the previous link selected and never reports success |
| Post-run health | A `BASE` 2 records/2 jobs, B `ROVER` 0 records, collection idle, diagnostics not busy, pair `connected` |

**Radio hardware finding:** the radio *candidate* cannot be proven on the bench at the moment, and the counters localize it to Unit A's SiK module. With both units on Wi-Fi and both radios powered by the bench switch, Unit B's radio answers `UART responds as SiK` (`RFD SiK 2.0 on HM-TRP`) while Unit A reports `No SiK identity response`, and during a cutover attempt the new counters show Unit A receiving **zero** radio control frames (`radio_ctl_rx 0`, `operation_rx 0`, `control_rx` only from Wi-Fi) while Unit B also receives nothing on radio (`radio_ctl_rx 0`). A local Radio selection on these same radios did pair earlier in the session (session `3673110729`), so the recovery step is a radio power cycle, and if that does not restore the identity response, a check of Unit A's UART2 TX/RX crossing and common ground. The radios' configuration (baud, air rate, power, ECC) was not touched and no antenna was disconnected. The post-commit **restoration** path (cutover applied, target unusable, previous medium restored) is covered by the portable suite's fault injection rather than on hardware, because it requires a working candidate medium.

## Defects found and fixed by this hardware run (all with regressions)

The deployment and the first hardware attempts exposed four real defects in the new service, each fixed with a portable test:

1. **Stale admission clock** — an accepted operation used the engine's last tick time, so a request admitted before the first tick (or on a freshly started engine) expired immediately. `request`/`forward` now take the caller's `now`.
2. **Commit retries reset an applied operation** — the coordinator's 500 ms `Commit` repeats restarted the delegate's apply phase; a retransmitted commit is now idempotent (and re-announces `Done`).
3. **A retried prepare re-adopted a finished operation** — and, separately, a delegate kept the previous operation's `committed` flag, so a new commit looked like a retransmission. Adopting an operation now always resets that state, a prepare for the operation already in progress only re-announces readiness, and an unrelated active operation still refuses it. A request the instrument is itself forwarding is recognised as its own answer rather than as a conflict.
4. **Stale baseline after a local recovery selection** — a local `select()` updated the durable record but not the operation engine, so `previous_transport` (and the revision baseline) could be wrong afterwards. The engine now adopts the locally selected medium.

A delegate that is already on the requested medium also reports its candidate satisfied instead of settling silently, which is what a mixed pair does when only one side is on the target.

## Outstanding

- Radio cutover and post-commit restoration on hardware (needs Unit A's SiK radio answering).
- The Settings **page** that consumes this API is R8; R7 (canonical `web/` assets and the deterministic embedder) precedes it and needs no hardware.
