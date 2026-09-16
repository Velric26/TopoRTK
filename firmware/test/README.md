# Host checks

From `firmware`, after building `unit_b`:

```powershell
g++ -std=c++11 -Wall -Wextra -Werror test/test_config.cpp -o .pio/test_config.exe
.pio/test_config.exe
python test/run_host_tests.py
```

Python 3 and a modern x86_64 `g++` are required. No extra Python packages are used. These checks do not open serial ports or change connected hardware. On this workstation MinGW-W64 16.1.0 (winget `BrechtSanders.WinLibs.POSIX.UCRT`) is installed and sits on the user `PATH`; a shell opened before that change, or any session that cannot see it, can run `.pio/run_with_toolchain.py <runner>` instead. PlatformIO's bundled `toolchain-gccmingw32` (GCC 5.1) cannot compile these sources: C++11 only, and its `cc1plus` needs `i686-w64-mingw32\lib` on `PATH` to start.

## How the native harnesses compile production sources

The native harnesses compile the production translation units themselves: the `src/*.cpp` under test are passed to `g++` unmodified, and the platform headers they include (`<Arduino.h>`, `<WiFi.h>`, `<SD_MMC.h>`, `<Preferences.h>`, `<esp_ota_ops.h>`, `<mbedtls/sha256.h>`, ...) resolve through a per-harness adapter directory whose every header contains nothing but an include of that harness's double:

| Harness | Adapter directory | Double it forwards to |
| --- | --- | --- |
| `run_host_tests.py` | `test/host_adapter/` | `test/host_hardware.h` (`host_hardware.cpp` holds the one `Serial`, `WiFi` and `millis()` clock) |
| `run_update_tests.py`, OTA service | `test/ota_adapter/` | `test/ota_hardware.h` |
| `run_update_tests.py`, peer service | `test/peer_adapter/` | `test/peer_hardware.h` |
| `run_link_service_tests.py` | `.pio/link-service-tests/IPAddress.h` | `test/link_service_hardware.h` |

The text the compiler reads is therefore the text under review: no harness copies a source and strips its hardware includes into `.pio/host_*.cpp` any more. Three consequences worth knowing:

- A platform header a source adds or moves must have a header of that name in the adapter directory too, or the build stops with `No such file or directory` naming it. That failure is the point: a double can no longer define something a device build would not, which is how a moved panel header once left `RGB565_RED` undefined on the target while the copied text still defined it.
- `test/host_adapter/` must lead the include path: it also stands in for `<Arduino_GFX_Library.h>`, which the PlatformIO GFX library directory behind it supplies for real.
- `run_link_service_tests.py` still strips its service's platform includes into a generated file (it needs `IPAddress` from its double); its adapter directory already holds that one header. It is the remaining copy path.

One translation unit is still assembled rather than compiled. `run_host_tests.py` includes `rover_ap.cpp`, `main.cpp`, the owners the cases observe through the composition root (`debug_service.cpp`, `device_settings.cpp`, `diagnostic_log.cpp`, `board_hardware.cpp`, `usb_console.cpp`), its stubs and `firmware_cases.h` into one `.pio/host_firmware.cpp`. The files are `#include`d, never copied, and they have to share a unit: the cases call the root's own internal entry points, and `host_hardware.h` gives each translation unit its own `SD_MMC`, `Wire` and `Preferences` doubles, so the config store, the log's CSV files, the injected FT6336 samples and the console's line buffer are observable only from the unit the test compiles. Splitting it apart would need new boundary declarations in `src/`. `run_update_tests.py` likewise includes `peer_update.cpp` twice, once per simulated unit inside its own namespace; its platform includes are satisfied once at global scope first, and the runner asserts that list stays complete.

`run_pair_session_tests.py` compiles the production `pair_session.cpp` and covers startup orders on both media, lossy proof and backpressure, strict codec/padding/opcode rejection, Hello/session replay, reboot and role isolation, same-boot outage retention, bounded dead-boot candidate reclaim and clock wrap. `run_link_operation_tests.py` compiles the production `link_operation.cpp` and covers pair-wide admission (staleness, busy, conflicting ids, idempotent repeats), cancellation before and after commit, restoration after a failed cutover, `recovery_required`, reboot reporting and the PLC1 operation wire boundaries. `run_link_service_tests.py` compiles the production `link_service.cpp` against an NVS double and both portable cores, covering stored-selection absence versus corruption (truncated/oversized/CRC-invalid, a valid pending record never masking a corrupt confirmed one), the sealed R5 migration, physical-ingress binding for control envelopes, recognized-version classification and radio-fault isolation on teardown. `run_settings_http_tests.py` slices the production settings handlers out of `web_http.cpp` and asserts every documented status code (202/400/401/403/409/503) against admission doubles. `run_update_tests.py` compiles the peer-notice and OTA services against the real pair core.

- `test_config.cpp`: round trips for every supported setting, rejected invalid/schema values, clearing prior base outputs, role-dependent RTCM streams, and recorded UM980 response checksums.
- `run_host_tests.py`: compiles the current production `main.cpp` against minimal hardware doubles. It exercises NVS failures/reload/no-op writes, profile acknowledgements/timeouts/retry, stale peer/correction clearing, post-profile GGA grace, actual touch polling and cancellation, and navigation.
- Drawing checks execute production renderers and touch navigation with the dependency's bitmap font, reject drawing/text outside the screen, compare navigated pages with clean renders, and assert that an unchanged render does no drawing. All six directed Link-page transitions through four pages, the link-mode arm/confirm/expiry path, its refusal copy and the gated local-recovery action are covered, together with key reveal/replacement cancellation. Previews are `.pio/ui-0.png` (Home), `ui-1.png` (GPS), `ui-2.png` (Link), `ui-3.png` (Setup), `ui-4.png` (Debug), `ui-link-0.png` through `ui-link-3.png` (the actual Link pages: rows, counters, phone, link mode), `ui-link-phone*.png` (key states), `ui-link-3-confirm.png` (armed medium switch) and `ui-base-selection.png`. There is no separate Phone screen or preview path bypassing Link navigation.

The host rasterizer approximates rounded edges; it is a layout preview, not an LCD capture. Preview GNSS values are synthetic. Hardware touch alignment, daylight readability, Wi-Fi reversal with both units, full power loss, and RTK behavior require physical validation.

## Survey workflow tests

`run_survey_tests.py` compiles the production engine, journal store and controller-lease logic, exercises failure/recovery paths, and checks UTM against independent PROJ. Install its test-only oracle with `python -m pip install --target .pio/proj-test pyproj`.

`run_pair_matrix.py` is the R11 release-gate driver: it claims control on both instruments, checks build identity and roles, polls boot convergence, exercises the local selection path and four pair-wide cutovers, runs a test on each medium (requiring a fresh stored run so a refusal cannot pass as evidence), cancels an admitted test, restarts each unit through the designed software restart, checks the documented 400/409 refusals and, when `.tpk` packages are supplied, performs a guarded update of both units and verifies saved jobs and records. It needs both instruments reachable and writes its evidence to the record directory it is given. `check_survey_browser.cjs` runs the actual embedded page against a native production-engine process using Playwright/Edge, including phone/tablet/desktop layouts. `check_survey_hardware.py 360` performs a read-only HTTP/USB-presence soak on the two bench IPs; it requires pyserial and already-flashed units. See the [survey test record](../../tests/2026-09-10-survey-workflow/README.md).

## Standalone diagnostic and shared takeover checks

`check_diagnostic_browser.cjs` exercises the production embedded page with controlled states: claim/release/reload, both roles without PIN, offline/stale controls, correct current/saved report downloads and responsive layouts. `check_takeover_hardware.cjs` checks both current bench instruments with two browser contexts and releases control afterward, without valid survey commands. `check_diagnostic_hardware.cjs` arms a bounded test through both real browser pages, disconnects/reconnects the browser contexts and downloads both final reports; the ESP32s generate/check all packets. It uses explicit `TOPORTK_RUN`, `TOPORTK_TRANSPORT`, `TOPORTK_SECONDS`, `TOPORTK_RATE`, `TOPORTK_MODE` environment settings. Hardware scripts need already-flashed, powered, antenna-connected instruments at the documented bench addresses. They are development verification, not a required field PC component. See the [dated evidence](../../tests/2026-09-12-standalone-tablet/README.md).

`check_correction_selftest.cjs` exercises the new local transport self-test through each instrument browser, compares the separate downloaded report, preserves the last RF report and verifies no survey/role/boot changes. It does not generate RF/UM980 traffic. The portable native fault suite and mutation checks are in `tests/2026-09-12-correction-transport/core_test.cpp`.
