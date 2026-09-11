# Rover takeover, UI 0.8

The Rover accepts `POST /api/v1/control` with `{client}` and no PIN. Each valid request generates a fresh bearer token and replaces the previous owner, including requests with the same client ID. Claims are serialized under the control guard; latest means the latest request accepted by the instrument. Invalid requests do not displace the owner. Old tokens cannot authorize commands, renew control or release the new owner. The 120-second inactivity lease, explicit release, same-origin/JSON checks, reboot and role/network revocation remain.

Already accepted commands/occupations continue through a takeover. The new owner can cancel a running occupation through the existing command path. No receiver, quality, coordinate or journal behavior changed. The browser never automatically submits takeover requests while polling. On its next status response, the displaced browser shows View only and locks write controls; the server rejects its old token immediately.

Base control retains the six-digit display PIN, incorrect-PIN throttle and exclusive active-owner checks. The Rover hides the PIN input and replaces its touchscreen PIN line with a Take control instruction. Wi-Fi passwords are unchanged. Anyone able to reach the Rover web interface can request control, including clients on its separate router path.

## Validation

- Native tests passed latest-owner replacement, old-token denial, old-owner release rejection, same-client token rotation, input bounds, expiry, reset/release and independence from Base PIN throttling. Existing Base control, survey/journal, projection and main firmware host suites also passed; logs included.
- Production browser/native-engine workflow passed two isolated browser takeovers and old-bearer HTTP rejection, followed by create/configure/collect/restart, quality rejection, checks/repeats/stakeout, linework, metadata editing, CSV/backup and offline locking. These observations are synthetic test fixtures only.
- CSV/backup verification and responsive/enlarged-text GUI tests passed. `layout-results.json` records the layout checks.
- Both A/B builds passed and both application flashes were hash-verified. See build/flash logs and `firmware-sha256.json`.
- Real two-browser Rover test passed: takeover/re-takeover, same-client rotation, old-token 401, stale release isolation, foreign-Origin and malformed-client rejection, explicit release and Base PIN retention. `takeover-results.json` contains no credentials. Only deliberately invalid old-token commands were sent; no valid survey command or receiver setting was submitted.
- Final A/B read-only check passed: original roles, two unconfigured jobs on A, zero jobs on B, active job and journal counts retained; new UI/data/export checks and collection lock passed. See `hardware-before.json`, `hardware-final.json` and hardware screenshots.

The user confirmed **only the ESP32 boards are connected**. This is web/control/storage validation without an attached receiver, antenna or other surveying peripheral. No GNSS fix, correction generation, physical occupation or field accuracy was tested.

Two initial persistence-test runs hit socket resets on a reused test HTTP connection. The instrument already limits HTTP to three sockets with idle-connection eviction. The read-only test helper now closes its separate API connections instead of retaining a pool alongside the browser; the final run passed. Production connection limits were not changed, and this does not establish long-duration multi-device reliability.

## Wi-Fi fallback

Existing **New key → Confirm** saves a new password, disables/restarts the Rover phone hotspot and revokes the control lease. Hotspot clients must rejoin with the new password. Devices connected through a separate local router remain connected and can request control again. Changing a password alone is not a universal disconnect guarantee; this firmware explicitly restarts its AP. Password rotation was inspected and covered by the existing host regression tests, but was not triggered on hardware during this checkpoint.

## Reproduce

From `firmware/um980-display-demo`, run `python test/run_survey_tests.py` and `python test/run_host_tests.py`. Set `TOPORTK_TEST_RECORD=tests/2026-09-11-rover-takeover` and run `node test/check_survey_browser.cjs`, `python test/check_survey_exports.py`, and `node test/check_gui_layout.cjs`. Playwright and Edge are required.

After flashing, `node test/check_takeover_hardware.cjs` deliberately takes and releases control on the Rover and checks Base PIN retention; it never submits a valid survey command. For the shared read-only checkpoint, set `TOPORTK_HARDWARE_RECORD=hardware-final.json` and `TOPORTK_CHECK_EXPORT`, `TOPORTK_CHECK_TARGETS`, `TOPORTK_CHECK_CHECKS`, `TOPORTK_CHECK_STAKE`, `TOPORTK_CHECK_LINES` to `1`, then run `node test/check_point_hardware.cjs`. Review saved baseline expectations before testing later field data.

The five-hour allowance remained above the requested 10% stop threshold throughout implementation and verification.
