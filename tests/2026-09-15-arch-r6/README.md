# R6 durable pair-wide selection and Settings API — 0.11.17-arch-r6 (2026-09-15)

Implementation of the [architecture review](../../docs/architecture-review.md) R6 checkpoint: one pair-wide operation service on top of R5's automatic pairing, a coordinator that admits at most one operation, durable checked records, a correlated outcome surface and the Settings HTTP API.

> **Status:** software complete and fully verified offline; **both instruments already run the `0.11.17-arch-r6` image**, but the settings endpoint requires the follow-up build below and the hardware acceptance did not run. Two OTA transfer attempts on the current bench link failed mid-upload, so the remaining device work needs USB (requested; details at the end).

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

## Hardware state and blocker

Both units took the `0.11.17-arch-r6` image earlier in the session through the normal acknowledged OTA path, and the pair negotiates automatically on the bench link (same session, `peer_connected: true` on both units; Rover station at −56 dBm "EXCELLENT"). The settings endpoint on that image returns 503 because its snapshot document overflowed when copying the stored diagnostic report — the per-medium summary above is that fix, and it is **not yet installed**.

Two sequential OTA attempts of the fixed image failed mid-transfer on the current bench link:

| Attempt | Outcome |
|---|---|
| 1 | `Upload connection closed or receive error` at 884,736 / 1,261,072 bytes; previous firmware retained, lock and pause released |
| 2 | `Upload stalled for 12 seconds` at 314,810 / 1,261,072 bytes; previous firmware retained, lock and pause released |

Both failures left the instruments healthy: `update failed`, `locked false`, `paused false`, Debug on, jobs/records unchanged, and the pair still connected. Because the fix cannot be delivered over the air right now, the remaining work needs **USB (COM4/COM10)**: flash `unit_a`/`unit_b` from `.pio/build/*/firmware.tpk` (or `.bin`), then run the acceptance below.

## Outstanding acceptance (needs the fixed image on both units)

- `GET /api/v1/settings` schema and `last_tests` on both roles.
- `link.select` no-op (already-selected medium), a real cutover to Radio and back, with both units agreeing on `selected_transport` and `revision`.
- A Base-side request forwarded to the Rover coordinator.
- 409 stale revision, 409 conflicting id, 503 `link.test`, cancellation of an admitted operation with restoration.
- One `link.cancel`/`link.select` pair exercised from a real browser page (R8 owns the Settings UI; this checkpoint's API is the contract R8 consumes).
