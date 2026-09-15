# R7 asset migration — canonical `web/` sources, embedder, derived capacity, shared client and the Debug update card (0.11.18-arch-r7 … 0.11.21-arch-r7d, 2026-09-15)

First R7 step: the browser pages and scripts stop living inside C++ string literals. Seven blobs were extracted byte-identically into `firmware/web/`, described by `web/assets.json`, embedded by a PlatformIO pre-build script, and served from a generated route table. No page behavior, layout or URL changed — the deployed bytes are identical to what 0.11.17-arch-r6 served.

## Layout

| Canonical source | Was | URLs | MIME |
|---|---|---|---|
| `web/status.html` | `web_ui.h` `R"TOPOHTML(` | `/`, `/ui/v1/` (Rover) | `text/html` |
| `web/survey.html` | `survey_ui.h` `R"SURVEY(` | `/survey`, `/`, `/ui/v1/` (Base) | `text/html` |
| `web/survey-tools.js` | `survey_tools_ui.h` `R"TOOLS(` | `/survey-tools.js` | `application/javascript` |
| `web/diagnostics.html` | `link_diagnostic_ui.h` `R"HTML(` | `/diagnostics` | `text/html` |
| `web/debug.html` | `debug_ui.h` `R"HTML(` | `/debug` | `text/html` |
| `web/debug-nav.js` | `debug_ui.h` `R"JS(` | `/debug-nav.js` | `application/javascript` |
| `web/update-ui.js` | `ota_ui.h` `R"JS(` | `/update-ui.js` | `application/javascript` |

`tools/embed_web_assets.py` validates the manifest (missing file, duplicate source, duplicate URL and unsupported MIME fail the build), then writes one `web_assets.h` per PlatformIO environment under `.pio/build/<env>/web_assets/` with flash-resident arrays and the route table; unchanged output is not rewritten, so incremental builds stay stable. `web_http.cpp` serves every page from that table, keeps the role-dependent root, returns 404 for unknown asset URLs, and derives `max_uri_handlers` from the asset and API tables instead of the previous hardcoded 24. The six embedded-header sources were deleted; browser checks read the canonical files (`web/assets.json` is the map), and the OTA HTTP host test now slices the upload handlers without naming an unrelated handler.

## Verification

- `pio run -e unit_a -e unit_b`: both SUCCESS, RAM 41.3% (135,244 B), flash 1,261,809 B (Unit A) / 1,261,805 B (Unit B); the pre-build script runs per environment and reports `7 sources, 8 routes, 112266 bytes`.
- Host suites pass: `run_host_tests.py`, `run_settings_http_tests.py`, `run_ota_http_tests.py`, `run_link_operation_tests.py`, `run_pair_session_tests.py`, `run_survey_tests.py` (incl. the 96 PROJ UTM examples), `run_update_tests.py`.
- Browser checks pass on the migrated sources: `check_gui_layout.cjs`, `check_web_browser.cjs` (also live against the Rover), `check_diagnostic_browser.cjs`, `check_survey_browser.cjs`, `check_debug_browser.cjs`, `check_ota_browser.cjs` — every route, assertion and message unchanged from before the migration.
- **Deployed byte identity** (`served-assets.json`): all 8 URLs on both units return the canonical source bytes, MIME types and the role-dependent root (`/` = `survey.html` 28,075 B on Base, `status.html` 15,448 B on Rover); `/no-such-asset.html` returns 404.
- **Rendered pages on the deployed build** (`rover-status.png`, `base-survey.png`): Rover status (correction link CONNECTED, GNSS NO FIX indoors, link signal −61 dBm) and Base survey (Base receiver setup, job `Vallarta01`, view-only with take-control) render styled with no JavaScript errors. The Debug page reports `Firmware 0.11.18-arch-r7`. The only console entry is the browser's own `/favicon.ico` request, which 404s; that route has never been served.
- OTA to both units through `run_ota_live.cjs --install` with acknowledged peer notices: Unit A 93.29 s, Unit B 27.54 s; both reported "new boot verified, Debug On (default), saved survey state unchanged".

## Deployment record

| Unit | Role | Before | After | Result |
|---|---|---|---|---|
| A (192.168.100.20) | Base | 0.11.17-arch-r6 | 0.11.18-arch-r7 | new boot verified, jobs/points preserved |
| B (192.168.100.19) | Rover | 0.11.17-arch-r6 | 0.11.18-arch-r7 | new boot verified, jobs/points preserved |

Both instruments stayed on the selected Wi-Fi link (revision 6, peer connected) through the update.

## Open observation (not attributable to this change)

During the first `check_web_hardware.py` run against the deployed Rover, its assertion that `link.invalid_packets` and `link.sequence_gaps` stay constant across the sampling window failed by one increment. The counter is incremented only in the UDP receive path (`main.cpp` admission of correction/test datagrams), which HTTP asset serving cannot reach. A 60 s idle sample afterwards showed no spontaneous increment and the re-run passed. Recorded as a live transient to watch, not a migration regression.

## R7b — Debug card heading and live upload percentage (in 0.11.19-arch-r7b)

The Debug page's firmware card is now titled **Updating Firmware** (`web/debug.html`), and while bytes are in flight the title becomes **Updating Firmware – N%**. The percentage comes from the upload's own progress events in the page driving the transfer, and the same formatter also renders the instrument-reported `received`/`total` when a client polls during an in-flight upload.

Live verification on Unit A (0.11.19-arch-r7b), a real transfer that was deliberately aborted mid-flight so nothing was re-flashed:

- Uploading page title: `Updating Firmware – 7%` → `Updating Firmware – 14%`.
- The instrument's own counters for the same transfer: `received 180224`, `total 1262720` (14.3 %), `error "Upload connection closed or receive error"`, `state failed`, `firmware 0.11.19-arch-r7b` retained, `locked false`, `paused false`, `attempt 35`.
- A second, passive client polling `/api/v1/update` every 400 ms during the transfer received **no response at all** (all samples timed out): the instrument pauses web access while receiving an image, so the percentage is visible to the uploading page, not to a bystander. The polling formatter covers the reachable window after the last byte, before the instrument leaves `uploading`.
- After the abort the pair returned to normal: both units `0.11.19-arch-r7b`, Wi-Fi selected, revision 6, peers connected, jobs preserved (A 2, B 0).
- `check_ota_browser.cjs` gained a deterministic assertion for the rename and for a server-reported `uploading` state (`Updating Firmware – 42%`, clamped `– 100%`, back to plain when idle); `check_debug_browser.cjs` still passes.

## R7 step 2 — one controller client, explicit navigation (in 0.11.20-arch-r7c and 0.11.21-arch-r7d)

The three page-local control clients are gone. `web/api.js` owns the control token (`sessionStorage` key `topoControlToken`), the browser's client id, claim/release and a JSON `topoRequest` that attaches the bearer, drops it on any 401 and notifies subscribers — so a takeover invalidates the previous controller's pages instead of leaving them acting on a stale bearer. Each page reconciles what it shows with the instrument's own `X-Controller` answer after a read-only poll, because such a poll never 401s. `web/navigation.js` replaces `debug-nav.js`: it marks the current page and gates an explicitly declared Debug tab, creating no markup of its own.

- Pages migrated: `status.html`, `survey.html` (+ `survey-tools.js`), `diagnostics.html`, `debug.html` (+ `update-ui.js`); `debug-nav.js` is deleted and the survey page declares `#debugTab`, `#debugAvailability` and `#debugPeerNote` after its workflow nav. The Debug card's monkey-patch (`originalControls`) is gone.
- 8 sources → 9 routes; both builds 41.3 % RAM, 1,267,392-byte images, `embed_web_assets` reporting `8 sources, 9 routes, 117460 bytes`.
- Checks updated: the retired per-page token keys (`surveyToken`, `diagnosticToken`, `debugToken`) no longer exist, so five checks that seeded or read them were silently degrading — including two hardware checks that release control in teardown. They now use `topoControlToken`. Harness route maps gained `/api.js`; `check_web_browser`'s title deadline was raised from 7 s to 20 s (it failed only when another Edge session ran concurrently).
- Host suites pass (host, OTA HTTP, settings HTTP); all six browser checks pass. `check_web_browser` remains sensitive to concurrent Edge sessions; it passes consistently when the machine is otherwise idle.
- Deployed: 0.11.20-arch-r7c to both units (A's first attempt aborted by design when the peer's acknowledgement did not arrive — no override used), then 0.11.21-arch-r7d after review caught that the survey page's ownership fix landed *after* the 0.11.20 build: the deployed asset was 75 bytes behind its source. Both units re-took the image with acknowledged notices and verified boots.
- On-board verification (`served-assets.json`): **20/20 served URLs match their canonical sources byte-for-byte** on both units, `/debug-nav.js` returns 404, and a real browser run of all five pages reports `topoRequest`/`topoToken` present, no legacy storage keys, the Debug tab rendered as a gated `BUTTON` with its availability note, and role-correct control text (`View only · Take control` on survey, `… to start or cancel` on the link test page).

## Bench note — a dark screen is usually saved `NIGHT` brightness, not a dead unit (2026-09-15)

Unit B looked powered off after a USB replug: no Wi-Fi, screen black, and a power cycle changed nothing. The board was in fact running — its serial console reported `BRIGHTNESS: NIGHT current=26 target=26 PWM=PASS hardware_duty=26 frequency=5000 Hz`, i.e. ~10 % backlight, and the mode is saved in `device_config`, which is why a reboot restored it. `brightness auto` over the serial console answered `CONFIG SAVE: PASS` and the panel came back at `AUTO NO GPS current=255 target=255 hardware_duty=256`. A second symptom was misleading: the unit's HTTP timed out at the moment of the report but recovered on its own, while the Wi-Fi counters showed `gap`/`bad` climbing — short timeouts on this bench link are not proof that an instrument is down. When a unit looks dead, check the brightness mode over serial (`brightness?`, `brightness auto|day|night`) before suspecting hardware.

Separately, the Rover's GNSS handshake never completes indoors: `GNSS STARTUP: attempt N` climbs without a fix because the handshake needs a fresh GGA within 5 s (`main.cpp:1763-1776`) and this receiver emits none without satellites, so the profile is reapplied every few seconds. Expected on a fix-less bench; it needs a sky view to settle.
