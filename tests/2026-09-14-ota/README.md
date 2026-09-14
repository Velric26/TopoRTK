# Debug OTA 0.11.0 software checkpoint

Date: 2026-09-14. Baseline: `f311444` plus this implementation. **No firmware flashed. Hardware acceptance pending.** The user asked to finish this software step and stop; implementation does not continue into USB/OTA deployment in this checkpoint.

## Results and evidence

- Both Unit A/B PlatformIO builds pass: [build.txt](build.txt). A prior attempt hit Windows `cmd.exe: Permission denied` while archiving the framework; retry with two build jobs passed. This was a local process-launch failure, not a firmware diagnostic or an approval rejection.
- Production main-loop host regressions pass: [host.txt](host.txt), including the added OTA pause rejecting input/discarding pending output, while review alone leaves forwarding available. Hardware/transport stubs are explicit in the runner.
- Portable notice core, production OTA service and two production peer adapters pass: [update-host.txt](update-host.txt). The service has 19 admission, ownership, interruption, target/identity, digest/write/end/NVS/boot-selection and boot-health fault cases. One thousand seeded notice-loss/reorder schedules terminate correctly. The two-peer simulation tests HELLO echo, phase acknowledgement, new-boot recovery, quality gating and continued heartbeat traffic. UART demultiplexing consumes a whole valid envelope before dispatch.
- Browser checks pass: [ota-browser.json](ota-browser.json), [debug-browser.json](debug-browser.json), [survey-browser.txt](survey-browser.txt). They cover 320/390/768/1280 layouts, wrong-unit selection, no mutation on file choice, explicit interruption confirmation, unconfirmed-peer override, one upload only after readiness, and new-boot verification before success. Existing passive Debug and survey workflows retain their regression coverage.
- Compiled-image package validation and bridge replay/queue regression results are saved in [packages.txt](packages.txt). Build/package SHA-256 identities are saved in [artifacts.json](artifacts.json); the binaries themselves remain in ignored `.pio` because they can contain build-time network configuration.
- Phone-width screenshots were visually reviewed. All test jobs/coordinates/exports here are synthetic host fixtures. Actual instrument jobs and receiver/radio configuration were not changed.

## Reproduction

Use the build/package commands in the [operator guide](../../docs/ota-operator-guide.md). Run `python firmware/um980-display-demo/test/run_host_tests.py` and `python firmware/um980-display-demo/test/run_update_tests.py`. The latter compiles the production OTA source against explicit flash/NVS/clock/task/SHA failure-injection doubles; it does not emulate the ESP32 bootloader. Python hashlib independently verifies the generated packages' real SHA-256 digest.

With Node, Playwright and Edge available, run `test/check_ota_browser.cjs`, `test/check_debug_browser.cjs`, and `test/check_survey_browser.cjs` under `firmware/um980-display-demo`. Set `TOPORTK_TEST_RECORD=tests/2026-09-14-ota` for the survey output directory. Debug/OTA browser endpoints are controlled HTTP fixtures, not the ESP-IDF server. The survey suite also exercises the production C++ engine.

## Hardware acceptance still required

1. One-time USB installation/readback on both units, including the configured rollback-capable bootloader; verify version, stored jobs/configuration and actual physical Debug enable/expiry.
2. Server authorization, final-confirmation warning and takeover/abort behavior on the real instruments. Opening Debug must leave live UART/RTCM forwarding healthy under load.
3. Real local-Wi-Fi and hotspot updates on both roles. Test missing peer, lost acknowledgement, interrupted/truncated upload and failed startup/rollback while USB recovery remains available.
4. Paired Wi-Fi and SiK update messages, notice deadlines and reconnected/fresh-quality behavior. SiK correction traffic must remain blocked after OTA until a fresh Base session is joined on Rover. No automatic SiK correction recovery is claimed.
5. Validate retained jobs/settings/SD contents and post-update collection admission without recording a field point. Outdoor RTK quality/accuracy and the previously deferred packet-loss root cause are separate acceptance tasks.

Read-only preflight found the expected USB IDs on COM4 (A) and COM10 (B); both HTTP survey endpoints reported no active collection and verified receiver profiles, with no usable GNSS fix. No inference of OTA hardware success is made from those observations.
