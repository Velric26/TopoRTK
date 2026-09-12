# Paired synthetic RTCM checkpoint — 2026-09-12

Firmware 0.10.1 implements the first Base → Rover synthetic RTCM fault profile. Both UM980s remain disconnected. This is a diagnostic adapter, not production RTCM forwarding.

- `native-results.txt` / `core_test.cpp`: deterministic injection/recovery, additional simulated radio loss, timer wrap, mismatched sessions, timeout, corrupt control and abort passed with C++11 warnings as errors. Workspace 4184 bytes on native 64-bit, **4164 bytes on ESP32**.
- `build.txt` / `flash.txt`: both builds and flash hashes verified. Static RAM 95548 bytes (29.2%); firmware approximately 1.168 MB.
- `browser-results.json` and layout images: production page against simulated states, including the RTCM arm action, busy controls and dedicated result wording. This is separate from hardware evidence.
- `913225/`: actual instrument pages, 60-second SiK run, browser offline for 20 seconds, authenticated arming without PIN, both reports downloaded and controls released. Prior local self-test and survey records/role/boot remained unchanged.
- `restart-*.txt`, `restart-before.json`, `restart-results.json`: both instrument boot IDs changed and identical radio reports restored, idle and not busy. No test auto-resumed.

## Actual radio outcome — delivery NOT PASSED

Base generated 30 messages and wrote 90 envelopes, deliberately dropping five middle fragments, corrupting five and duplicating five first fragments. Ten messages should therefore be rejected; **20 are eligible for complete delivery**. Rover recovered **19/20**, with **zero invalid sink outputs**, five duplicate fragments, eight wire CRC errors and eleven assembly expirations. Neither instrument reported sender expiry. Both received the peer summary and correctly reported `pair_pass=false`.

The workflow passed; radio delivery did not. Five CRC errors were deliberately injected, while the observed total was eight. The additional corruption/loss is consistent with earlier bench failures, but this run does not identify RF, UART, supply, wiring or modem buffering as the cause. Current antenna spacing was not reconfirmed; do not label this another six-metre acceptance test. Keep antennas fitted.

The expected safety behavior was observed: incomplete/corrupt messages did not reach the synthetic sink, and later valid messages recovered. This is not a proof against every corruption pattern, an RTK accuracy test, Android hardware acceptance or a production freshness guarantee. The fixed synthetic pacing is not a qualification of real RTCM burst capacity.

## Reuse and next work

On both `/diagnostics` pages, take control, set the same new test code and duration, confirm preparation, and choose **Arm RTCM fault test**. It always uses SiK Base → Rover at one 600-byte message every two seconds. The tablet can leave and retrieve saved reports later; download before replacing the latest radio report.

Native: compile `core_test.cpp` with `g++ -std=c++11 -Wall -Wextra -Werror -Ifirmware/um980-display-demo/src`. Browser fixture: run `check_diagnostic_browser.cjs` with `TOPORTK_TEST_RECORD=tests/2026-09-12-correction-pair`. Hardware: `check_correction_pair.cjs`, optional `TOPORTK_RUN` fresh six-digit code and `TOPORTK_SECONDS` (30/60/120/300). The hardware script records delivery failure separately from workflow assertions; inspect `pair_pass`, not just process exit status.

Next, compare a clean traffic profile with the injected profile under recorded spacing/supply conditions and add UART overflow/error evidence before changing radio settings. The current first profile always injects faults; a selectable clean profile and detailed UART fault telemetry are still pending. Resolve/characterize the additional loss before moving to stage 3. Then implement production burst/reference retention, freshness and output backpressure before connecting real UM980 data. See [plan](../../docs/loss-tolerant-transport.md).
