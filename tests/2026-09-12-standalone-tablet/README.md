# Tablet diagnostic workflow and both-role takeover — 2026-09-12

## Current implementation

UI 0.9.1 gives Base and Rover the same latest-request takeover, without a web control PIN. Every accepted claim rotates the bearer and invalidates the previous controller, even for the same client. Old releases cannot revoke a newer owner. The existing same-origin, typed-client and authenticated-write checks remain; accepted survey operations and quality gates are unchanged. No Base PIN is generated or exposed on either touchscreen or web page.

The diagnostic page shows connected role, controller ownership, live progress, packet metrics and clearly labelled restored reports. Controls lock on network failure or a stalled snapshot. A request timeout explains that its state must be checked before retrying. Controller/client state survives browser reload; polling renews only the current bearer. Downloads prefer the current run, including failure, over any older pass. Queued messages clear when the requested state arrives. No new production SiK RTCM bridge, BLE adapter, radio power setting or receiver configuration is implemented here.

## Verification

- Both final uploads passed flash hash verification: `final-flash-both.txt`. COM4/Unit A/192.168.100.20 is currently Base; COM10/Unit B/192.168.100.19 is currently Rover. Roles and DHCP addresses can change. UM980s remain disconnected.
- `takeover-results.json`: real two-browser checks pass for both roles, including previous-bearer rejection, repeated-client rotation, stale release, diagnostic/survey ownership agreement and unchanged jobs/role/boot. Controllers released at the end.
- `browser-results.json`: actual embedded diagnostic page with controlled transport states; connection/role, confirmation, arm, reload, offline/stale locks, current-versus-saved download, both roles without PIN, no ineffective probe cancellation, and 320/390/768/1280 px plus 200% text. Screenshots `diagnostic-*.png` use synthetic data, not RF measurements.
- `host-tests.txt`: production main firmware against hardware doubles, including diagnostic configuration/readiness lock, saved settings, profile behavior, Wi-Fi key lifecycle, touch actions and display bounds.
- `survey-tests.txt`: production engine/store/control suite and 96 independent PROJ UTM comparisons passed. Use Python 3.13 for the existing `.pio/proj-test` binary dependency; the PlatformIO Python 3.11 runtime does not match that dependency.

## Radio runs and interruptions

1. Initial run 913201: two-minute bidirectional SiK at 1000 framed B/s per sender. Base was armed through USB while its legacy PIN behavior was still present; Rover was armed through the real browser. Browser disconnection for 45 seconds did not stop the test; reconnection/download worked and no Rover reboot or survey record changes occurred. **RF pair failed:** Base received 466/468 with 366 parser errors; Rover received 463/468 with 851 parser errors. Parser errors are discarded bytes/invalid frames, not an RF bit-error rate. See `sik-base.json`, `sik-rover-browser-download.json`, `hardware-browser-results.json`. Antenna spacing was not reconfirmed for this run; the previous reported position was close bench spacing.
2. Attempted run 913203 after the user reported moving one unit about five metres: the second web page timed out before either instrument was armed. No five-metre RF result exists. `sik-913203/before-0.json` is only a preflight snapshot. Do not treat the attempt as a failed RF transfer.
3. The user then reconnected both units to the PC and confirmed approximately **30 cm** separation, requesting that bench work finish and stop before a later six-metre move. The final matched run 913204 completed at this 30 cm separation: 120 seconds, 1000 framed B/s per sender, both directions. Base received **467/468** (122 parser errors); Rover **465/468** (471 parser errors). Both reports were saved and downloaded through the actual instrument browser pages. Both browser contexts were offline for 20 seconds during the run. **RF pair did not pass.** See `sik-913204/result.json`, `base.json`, `rover.json` and screenshots. The browser workflow passed without USB commands or survey/role/boot changes.

Earlier `flash-takeover.txt` records a missing COM10 before upload; `final-flash-both.txt` supersedes it. Earlier screenshots `before-*.png` show the old Connecting/PIN display issues. They are historical evidence, not the final GUI.

## Next physical step

Bench work is complete. Neither diagnostic is armed/running; browser controls were released. See `stopped-state.json`. No six-metre test has been started. Move the unit whose screen says Rover about six metres away, keeping Base stationary (either direction is electrically viable). The moved ESP32 must keep its own supported power supply; powering only its SiK radio is insufficient. Keep both SiK antennas connected. Confirm that the moved ESP32 screen remains on and its browser is reachable before arming a new matching two-minute test. A standalone USB power bank can power the ESP32 without a PC data connection.

Distance/power qualification and an actual Android phone/tablet field check remain pending. Browser tests at tablet viewport sizes do not establish physical Android behavior or RTK/survey accuracy.

## Six-metre follow-up completed

Run 913205 used the same two-minute, 1000 framed B/s bidirectional settings. Both ends received 465/468 packets (three missing each); pair acceptance failed. Browser disconnect/reconnect, saved downloads and no survey/role/boot changes passed. Six metres did not eliminate loss. See [full comparison](sik-913205/README.md). No test remains armed/running. Investigation paused with five-hour allowance at 3% remaining; results committed and pushed.
