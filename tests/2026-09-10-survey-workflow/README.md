# UI 0.3: jobs, UTM setup, antennas/base and collection

Date: 2026-09-10. User requested roadmap ranks 2–5 together and selected UTM. Scope and operating limits: [survey workflow](../../docs/survey-workflow.md).

## Results

- **Native production engine/store/control tests: PASS.** Create/configure/replay; duplicate command and point IDs; changed-payload rejection; restart interruption; durable cancellation; failed/ambiguous writes; missing storage; record corruption/gaps; real-file sync/rename/readback; stale configuration rejection; fix/uncertainty/correction/station/reference/epoch failures; slant geometry, local geoid radius and international-foot conversion; Base verification, queue rejection and timeout; PIN rate limits, single writer and lease expiration.
- **Independent coordinate oracle: PASS.** 96 standard UTM examples across zones 1/14/31/60, north/south, zone edges and the supported latitude range; worst difference from PROJ **0.760 mm**. ECEF/geodetic round trips below 1 micrometre in tested cases. These are software tests, not an accuracy claim for GNSS observations.
- **Actual main.cpp hardware-double regressions: PASS.** BESTNAV CRC, GPS epoch, MSL-plus-undulation ellipsoidal height, base ID/differential age, invalid fields and fixed-base command formatting. Existing touch, profile verification, NVS failures, brightness, phone AP/key handling, dashboard bounds/cache and status-snapshot tests remain passing.
- **Browser + production C++ engine: PASS.** Actual embedded HTML creates a job, configures UTM/base/antennas, collects a synthetic fixed point, reloads/restarts, and aborts another point on quality loss. Checks cover edits surviving polls, offline write locks, Base view, no JS exceptions and no horizontal overflow at 390/768/1280 pixels. HTTP pairing is a test double here; the actual control class is tested natively.
- **Both hardware variants built and flashed, hash verified.** Unit A: Rover / COM4, Unit B: Base / COM10. Both report UI 0.3.0, saved Night mode, existing Local Router transport and verified receiver profiles. Both survey APIs report SD ready. No role, correction transport, antenna/control coordinates or receiver base settings were changed by the hardware tests.
- **Real Rover SD/job restart check: PASS.** Paired using the display PIN; created `Bench test - persistence`, retried the exact command (one journal record), then reset the ESP32 through esptool. The same active job/record recovered with the receiver profile verified. The job remains unconfigured and has **zero points**. See [machine-readable result](hardware-job-recovery.json).
- **Live browser: PASS.** Actual pages on both units rendered at phone width without JavaScript errors; Unit A showed the recovered bench job, Unit B its Base setup. Unpaired controls were disabled. See [Unit A](live-unit-A.png) and [Unit B](live-unit-B.png).
- **Six-minute HTTP/USB soak: completed.** 59 samples; COM4 and COM10 remained present throughout. Unit B had one continuous boot ID. Unit A's only boot-ID change was the deliberate reset in the job-recovery test. One initial Rover HTTP timeout and one request failure during that reset recovered; this is not a zero-network-error result. Free heap stayed around 223–225 kB on A and 226–228 kB on B. Both profiles and SD remained ready, and the Rover's peer packet count advanced. Unauthenticated writes returned 401 and cross-origin writes returned 403 on both instruments. See [hardware-soak.json](hardware-soak.json). No spontaneous reset or USB disappearance was observed after reconnection during this window; the underlying intermittent USB cause is not proven.

## USB interruption and recovery

The user reported Unit A disconnecting after a few minutes. Its first upload lost USB while the esptool stub was writing and failed verification. A high-rate Unit B upload also disconnected. This occurred while application firmware was not running and therefore does not establish a defect in the survey application. The user reconnected Unit A with a different cable/port; application images were then written at a requested 115200 baud on each USB-Serial/JTAG port, with successful flash hash verification. USB bulk transfer speed is not necessarily limited by the selected serial baud.

Two build attempts also hit transient Windows `cmd.exe: Permission denied` process-launch errors. Retrying serially completed the build. Neither failed upload was counted as a successful deployment.

## Reproduce

From `firmware/um980-display-demo`:

```powershell
pio run -e unit_a -e unit_b -j 1
python -m pip install --target .pio/proj-test pyproj
python test/run_survey_tests.py
python test/run_host_tests.py
$env:NODE_PATH='C:/Users/el_sp/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules'
node test/check_survey_browser.cjs
C:/Users/el_sp/.platformio/penv/Scripts/python.exe test/check_survey_hardware.py 360
```

The browser suite uses Edge headless and a local native engine test process. Test coordinates, antenna descriptions and generated points belong only to fixtures, not to a real job. The hardware soak is read-only and does not open serial ports or save credentials. The one actual instrument job was explicitly named as a bench test. Control tokens/PINs are not in test artifacts.

## Not established

- Real open-sky occupation and independently surveyed check-point agreement.
- Actual antenna measurement/reference offsets and geoid/reference-frame suitability for the first job. The browser now supplies Zapopan starter values (zone 13N, WGS84, epoch metadata, metres and ellipsoidal height), but the operator must review/confirm the source and epoch and supply the real measurements and control information.
- Fixed-base coordinate application and RTCM reference agreement on hardware with antennas connected. Native tests cover the state machine and main-loop command formatting; the hardware Base remains in its previous temporary mode.
- Power-cut/card-removal/full-card behavior on the physical SD controller. Native tests exercise analogous write uncertainty/corruption paths, and the physical SD recovered a committed job after an ESP32 reset.
- Full point management, export, independent check-point reports, installed geoid grids, datum transformations, raw carrier-phase/RINEX logging or stakeout.
- Full physical Android model/version/recovery matrix. Phone and tablet viewport tests do not substitute for field use.

## Screenshots

- [UTM setup, tablet width](setup-768.png)
- [Last committed synthetic point, phone width](point-phone.png)
- [Base setup, phone width](base-phone.png)
