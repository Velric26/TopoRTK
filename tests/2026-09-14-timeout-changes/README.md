# OTA deadline raise and Debug persistence — 0.11.3 deployed to both units (2026-09-14)

Two operator-requested behavior changes, implemented and covered by updated automated checks, then deployed to both units over Wi-Fi OTA with acknowledged paired notices and verified boot acceptance.

## Changes

- OTA transfer deadline raised from 120 to 300 seconds (`web_http.cpp` upload loop, `ota_service.cpp` write/finish guards). The 12-second no-progress stall limit and all other OTA guard deadlines (60-second stage expiry, 3.5-second notice stages, boot acceptance window) are unchanged. Motivation: the verified 118.602-second transfer left ~1.4 seconds of margin.
- Debug idle timeout removed (`debug_core.h` Session, `debug_service.cpp`, `web_http.cpp`, `debug_ui.h`, touchscreen Debug page, main loop). Debug now stays On until disabled on the instrument or web, or until restart. The `POST /api/v1/debug` `activity` operation, `debug_activity()`, `debug_service()`, the browser keep-alive/extend controls and the `remaining_ms`/`idle_ms` status fields were removed with the timer. The controller's separate two-minute takeover lease is unchanged. Mid-upload Debug-disable safety behavior (pause continues; flash not torn down) is preserved and still tested.
- Firmware version bumped to 0.11.3 (`web_http.h`).

## Automated checks (all run this session, all PASS)

- `test/run_host_tests.py` — firmware cases including reworked Debug session cases: enable during receiver setup, persistence across 15-minute and 45-minute horizons, timer-rollover persistence, disable clears capture, bounded capture, unchanged COM2 output.
- `test/run_ota_http_tests.py` — all 12 production upload-handler cases including the extended `deadline` case (150 × 2 s steps → 300-second abort message).
- `test/run_update_tests.py` — update-notice core, 23 production OTA service cases (including `timeout` retargeted to the 300-second window), two-peer HELLO/ACK/recovery.
- `test/run_survey_tests.py` — durable jobs, quality gates, journal integrity, 96 PROJ UTM examples (worst difference 0.760 mm).
- `pio run -e unit_a -e unit_b` — both builds SUCCESS.

## Browser regression checks (executed this session, PASS)

- Environment installed this session: Node.js v24.19.0 (winget, machine default `C:\Program Files\nodejs`) and Playwright 1.63.0 (`test/node_modules`, git-ignored). The checks drive headless Microsoft Edge via the `msedge` channel.
- `test/check_debug_browser.cjs` — PASS: gray tab and enable instructions, hardware-enabled availability, takeover without PIN, monitoring while occupation is active, no idle timer (zero `activity` posts), text escaping/filter/download/pause, controller loss clears private view, stale/offline disable access, role-specific update warnings, 320/390/768/1280 layouts. Artifacts: `debug-browser.json`, `debug-*.png`.
- `test/check_ota_browser.cjs` — PASS: target/review/confirmation, unconfirmed-peer override, upload and new-boot verification. Artifacts: `ota-browser.json`, `ota-review-*.png`.
- Two fixes made while running these: `debug_ui.h` briefly gained a duplicate `#pause` button id (removed; the Communications-section pause button is the only one), and `check_ota_browser.cjs` now anchors its output directory to the repository root like `check_debug_browser.cjs` instead of the process working directory.

## Live Wi-Fi OTA — both units updated to 0.11.3 (PASS)

Both units received 0.11.3 over the local-router Wi-Fi using the standard browser workflow (`run_ota_live.cjs`), with acknowledged paired notices in both directions (Unit A=Base at 192.168.100.20, Unit B=Rover at 192.168.100.19):

| Unit | From | Peer stages | Result |
|---|---|---|---|
| A | 0.11.0 | preparing → acknowledged → updating → reconnected | Update complete; new boot verified; Debug Off after restart; saved survey state unchanged |
| B | 0.11.2 | preparing → acknowledged → updating → reconnected | Update complete; new boot verified; Debug Off after restart; saved survey state unchanged |

Evidence: `unit-a-ota-evidence.json`, `unit-b-ota-evidence.json`, review screenshots. Job/point records were preserved (2 jobs on A checked before/after via the survey snapshot subset).

Scope note: during these transfers each receiver still ran its old firmware, so the previous 120-second deadline governed. The new 300-second deadline takes effect for the next update cycle from 0.11.3 and remains to be exercised under real conditions. Debug-persistence (no idle timeout) is now deployed on both units; a long-idle touchscreen observation can document it whenever convenient.

## Still outstanding

- Deliberate rollback hardware acceptance (P0): flash a failing/interrupted image and record actual bootloader rollback and watchdog behavior.
- Exercise the 300-second deadline on a future update cycle.

See [PROJECT.md](../../PROJECT.md) and the [design record](../../docs/debug-and-ota.md).
