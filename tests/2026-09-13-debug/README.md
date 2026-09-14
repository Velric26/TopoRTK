# Debug 0.10.5 — host and browser checkpoint

Date: 2026-09-13. Source baseline: `1549fcd`, plus this Debug increment. **Not flashed; hardware acceptance pending.** The two deployed instruments remain on 0.10.4. No receiver/radio configuration, saved instrument jobs or field points were changed.

## Results

| Check | Result | Evidence |
|---|---|---|
| Production firmware host harness | PASS: physical touch routing, enable during receiver setup, idle expiry/renewal/wrap, polling isolation, bounded capture, same expected COM2 output with Debug enabled; existing freshness/profile/storage/UI regressions | [host.txt](host.txt) |
| Debug production HTML/JS with controlled HTTP fixtures | PASS: gray tab, enable instructions, PIN-free takeover, active-occupation monitoring, text escaping/filter/download/pause, control loss, stale/offline/expired states, role warnings, disabled OTA and responsive widths | [debug-browser.json](debug-browser.json) |
| Survey production browser/C++ engine regression | PASS: create/configure/collect/restart, quality abort, edits, targets/checks/lines, offline controls and Base view | [survey-browser.txt](survey-browser.txt) |
| Unit A / Unit B PlatformIO build | PASS, both targets; static RAM 130,468 bytes (39.8%); flash 1,217,161 / 1,217,165 bytes (18.6% of OTA slot) | [build.txt](build.txt) |

Browser layout widths: 320, 390, 768 and 1280 px. The phone Debug view and host-rendered touchscreen Setup/Debug pages were visually reviewed. Screenshots and fixture exports in this directory contain synthetic test data, not field measurements. The host runner's printed preview range ends at `ui-4.png`; it also generated and checked the new `ui-5.png` Debug screen.

## Reproduction

From the repository root, with PlatformIO, Python, g++ and Node/Playwright available:

```powershell
platformio run -d firmware/um980-display-demo -e unit_a -e unit_b
python firmware/um980-display-demo/test/run_host_tests.py
node firmware/um980-display-demo/test/check_debug_browser.cjs
$env:TOPORTK_TEST_RECORD='tests/2026-09-13-debug'
node firmware/um980-display-demo/test/check_survey_browser.cjs
```

Browser tests use installed Edge through Playwright. The production survey browser suite uses its C++ engine fixture; Debug HTTP behavior is simulated by explicit fixtures, not the ESP-IDF HTTP server. The host harness compiles the production Debug service against hardware doubles. No test here validates real UART scheduling, HTTP authorization on the instrument, power cycling, Android networking or RF/RTK performance.

## Remaining acceptance

- Flash/read back both units and exercise actual physical Debug enable/disable, idle expiration, current-controller private access and takeover rejection of old tokens.
- Measure UART/RTCM forwarding and receiver freshness with passive capture under real load. Fixed memory, rate limiting and host output checks reduce risk; they do not prove zero hardware timing impact.
- Implement/test paired update notice and acknowledgement, bounded retries/deadlines, missed notices, stale/replayed messages, cancellation and reconnection before claiming peer update status works.
- Implement/test OTA image admission, exclusive ownership, interrupted transfer, startup health checks and rollback before enabling Upload. Test local Wi-Fi and hotspot paths on both roles.
- Preserve the currently flashed 0.10.4 baseline and USB recovery access. A combined USB preparation is recommended to reduce enclosure access; none was performed in this checkpoint.

The [full design](../../docs/debug-and-ota.md) distinguishes implemented passive Debug from planned disruptive update actions. Packet-loss root-cause investigation remains deferred.
