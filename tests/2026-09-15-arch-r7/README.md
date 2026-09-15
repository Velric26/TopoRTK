# R7 asset migration — canonical `web/` sources, embedder and derived capacity (0.11.18-arch-r7, 2026-09-15)

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

## Scope of this step

Done: asset sources, manifest, deterministic embedder, derived HTTP capacity, deleted embedded headers, browser checks reading canonical sources. Still open in R7: explicit navigation and the single shared controller client (today's per-page scripts and monkey-patching remain), and the R7b Debug-card rename with the live upload percentage — its edit now lands in `web/debug.html`.
