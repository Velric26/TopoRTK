# Correction transport stage 1 — 2026-09-12

Scope: portable framing, sender queue, one-slot reassembly, CRC32/RTCM CRC24Q validation, explicit source/session, non-replayed sequence, known-age/reassembly expiry and a local fault self-test. No SiK data-path adapter, actual RTCM forwarding, command retry channel or BLE is connected. Both UM980s remain disconnected.

## Evidence

- `native-results.txt`: 22 deterministic cases, all 2048 individual envelope bit flips, byte-order fixture, latest queued message, 10000 mutated envelopes and bounds/canary invariants. C++11 compilation with `-Wall -Wextra -Werror`. Native workspace 4760 bytes; sender 1060; stream/receiver 1360.
- `browser-results.json` and `diagnostic-*.png`: production page with controlled states, phone/tablet layout and the new separate local self-test/download. The mocked report is not hardware evidence.
- `build.txt` and `flash.txt`: both ESP32 builds and flash hashes verified. Static RAM 91388 bytes (27.9%); firmware about 1.158 MB.
- `hardware-results.json`, `base-selftest.json`, `rover-selftest.json`: real browser-triggered tests on both units, no PIN; 22/22 passed, failed_mask=0, report saved and downloaded. Workspace **4744 bytes on ESP32**. Core suite time **51.045 ms Base / 52.111 ms Rover** (storage/browser latency excluded). Unauthenticated execution rejected; prior radio result and survey data/role/boot unchanged. Controls released afterward.
- `restart-base.txt`, `restart-rover.txt`, `restart-results.json`: esptool read-MAC/hard-reset cycle (no reflashing); boot IDs changed and both identical self-test reports restored. No test is armed/running.

The parser, sender and receiver use fixed buffers without data-path allocation. The browser/JSON/NVS report layer uses the existing instrument services and is outside the core memory figure. CRC is accidental-error detection, not authentication or mathematical proof against every collision. Time limits cover sender queue plus local residence; unmeasured modem/RF transit before first arrival is not included.

The generated RTCM-shaped test payloads validate framing/checksums only, not real satellite observation semantics. A local pass does not qualify RF delivery or RTK/survey accuracy. Existing six-metre RF losses remain unresolved and their historical results are preserved.

Run the native suite from repository root with a C++11 compiler, include `firmware/um980-display-demo/src`, and compile `core_test.cpp`. Browser checks use `check_diagnostic_browser.cjs` with `TOPORTK_TEST_RECORD=tests/2026-09-12-correction-transport`. Real local checks use `check_correction_selftest.cjs` on the two documented bench IPs and release control after completion. The user-facing route is `/diagnostics` → Run local fault checks → Download self-test report.

Next: paired synthetic SiK traffic through the same core, explicit fault injection at its boundaries, fair bounded UART scheduling and end-to-end validation sink. Real UM980 COM2 forwarding follows only after that milestone. Full design and failed-mask case mapping: [transport plan](../../docs/loss-tolerant-transport.md).
