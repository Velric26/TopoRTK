# Host checks

From `firmware/um980-display-demo`, after building `unit_b`:

```powershell
g++ -std=c++11 -Wall -Wextra -Werror test/test_config.cpp -o .pio/test_config.exe
.pio/test_config.exe
python test/run_host_tests.py
```

Python 3 and `g++` are required. No extra Python packages are used. These checks do not open serial ports or change connected hardware.

- `test_config.cpp`: round trips for every supported setting, rejected invalid/schema values, clearing prior base outputs, role-dependent RTCM streams, and recorded UM980 response checksums.
- `run_host_tests.py`: compiles the current production `main.cpp` against minimal hardware doubles. It exercises NVS failures/reload/no-op writes, profile acknowledgements/timeouts/retry, stale peer/correction clearing, post-profile GGA grace, actual touch polling and cancellation, and navigation.
- Drawing tests execute production drawing functions with the dependency's bitmap font, reject drawing/text outside the screen, and assert that a second unchanged render does no drawing. Images are written to `.pio/ui-0.png` (Home), `ui-1.png` (GPS), `ui-2.png` (Link), `ui-3.png` (Setup), and `ui-base-selection.png`.

The host rasterizer approximates rounded edges; it is a layout preview, not an LCD capture. Preview GNSS values are synthetic. Hardware touch alignment, daylight readability, Wi-Fi reversal with both units, full power loss, and RTK behavior require physical validation.

## Survey workflow tests

`run_survey_tests.py` compiles the production engine, journal store and controller-lease logic, exercises failure/recovery paths, and checks UTM against independent PROJ. Install its test-only oracle with `python -m pip install --target .pio/proj-test pyproj`.

`check_survey_browser.cjs` runs the actual embedded page against a native production-engine process using Playwright/Edge, including phone/tablet/desktop layouts. `check_survey_hardware.py 360` performs a read-only HTTP/USB-presence soak on the two bench IPs; it requires pyserial and already-flashed units. See the [survey test record](../../../tests/2026-09-10-survey-workflow/README.md).

## Standalone diagnostic and shared takeover checks

`check_diagnostic_browser.cjs` exercises the production embedded page with controlled states: claim/release/reload, both roles without PIN, offline/stale controls, correct current/saved report downloads and responsive layouts. `check_takeover_hardware.cjs` checks both current bench instruments with two browser contexts and releases control afterward, without valid survey commands. `check_diagnostic_hardware.cjs` arms a bounded test through both real browser pages, disconnects/reconnects the browser contexts and downloads both final reports; the ESP32s generate/check all packets. It uses explicit `TOPORTK_RUN`, `TOPORTK_TRANSPORT`, `TOPORTK_SECONDS`, `TOPORTK_RATE`, `TOPORTK_MODE` environment settings. Hardware scripts need already-flashed, powered, antenna-connected instruments at the documented bench addresses. They are development verification, not a required field PC component. See the [dated evidence](../../../tests/2026-09-12-standalone-tablet/README.md).
