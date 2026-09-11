# Instrument-suited surveying scope, UI 0.5

See the [ESP32/Android review](../../docs/esp32-android-feature-split.md) for the scope decision. Synthetic coordinates/observations in this folder are test fixtures, not field results.

## Checkpoints

| Change | Native/browser | Physical instruments |
|---|---|---|
| Bounded control/design targets | Passed: independent-source/reference confirmation, duplicate/revision rejection, 64-target limit, restart/retry and phone layout | A/B application flashes hash-verified; existing jobs/roles preserved; target/read/export endpoints and unconfigured-job write lock passed. No artificial target was created on hardware. |
| Check/repeat occupations and coordinate precision | Passed, including metre/foot residuals and readable CSV report | A/B builds/flashes and read-only checks passed; jobs/roles preserved |
| One-point stakeout | Native/browser/CSV tests and A/B builds passed | A/B final flashes hash-verified; read-only UI/export/job persistence and unavailable-guidance lock passed |
| Manual line tags | Not implemented yet | Pending |

## Data contracts

`target.create` requires the current job/configuration revision and an immutable target with ID, kind (`control` or `design`), documented source, UTM easting/northing/ground height, matching source reference and explicit confirmation. Control independence is an operator declaration, not independently verified by software. At most 64 targets exist per instrument, and target IDs cannot be overwritten. They do not increase measured-point counts. All targets enter job backups.

`collect.start.comparison` selects `check` with a control target or `repeat` with a non-deleted measured point in the same setup revision. It requires `start`, `intermediate` or `end` phase and positive horizontal/vertical tolerances in job units. The start record freezes reference coordinates and provenance. All usual RTK/base/epoch/uncertainty/antenna checks remain active throughout the occupation. A valid observation outside comparison tolerance is saved and clearly labelled as failed comparison; a lost-quality occupation is rejected without saving a point. Repeats are separate measured points and never silently averaged into earlier records.

CSV has readable comparison purpose/reference/phase/residual/tolerance/result columns plus the full observation JSON. Signed deltas are measured minus reference. Distances are UTM grid distances; no grid-to-ground correction or localization is implied.

## Precision and compatibility

The residual tests found that ArduinoJson 6's default number formatter rounded large projected coordinates to roughly ten significant digits (about millimetres at UTM northings). The instrument serializer now uses 15 significant digits, retaining sub-micrometre resolution over supported coordinate ranges. Seventeen digits were evaluated but exposed insignificant repeated parsing drift in ArduinoJson 6. Tests use a 10-nanometre comparison tolerance for fixture coordinate round trips and verify JSON keys/strings/arrays/64-bit integers separately. Full job backups retain the exact original journal JSON and checksum.

New receipt records identify precise request canonicalization as `payload_format=2`. Legacy receipts with no marker use the original formatter when checking retries. New request IDs distinguish the tested 0.01 mm coordinate change. Existing point files are not rewritten; this change cannot recover their earlier lost digits.

## Environment

A is Rover at 192.168.100.20 / COM4; B is Base at 192.168.100.19 / COM10. Baseline in `hardware-before.json`: A has two unconfigured jobs and no measured points; B has no jobs. The existing data are preserved. Some Windows compiler/binary-generation processes failed to launch (`Permission denied` / `CreateProcess`); retries are recorded. No security configuration was changed. A slow full serial build was interrupted and restarted with four build jobs.

Physical tests are read-only interface/storage checks with a 390 px browser viewport, not actual Android-device or field-accuracy qualification. Live measured-point/control/stakeout checks, larger-job latency, power interruption and SD failure behavior still need physical qualification.


## Stakeout implementation

Select the stakeout occupation method and a target from the current setup revision. Guidance displays direction by N/E coordinate offsets, grid distance and design-height difference (higher/fill or lower/cut). Computation runs in the phone browser using the existing snapshot, avoiding additional polling endpoints or instrument geometry work. The UI expires guidance when snapshot transport time plus GNSS age plus elapsed local time exceeds 1500 ms, or quality/connection/target selection is invalid. The normal fixed-pole occupation records the frozen design target and actual residuals on SD; it does not use the browser guidance result as an accepted measurement. Designs remain immutable.


## Final result and reproduction

Implementation stopped after the five-hour reading crossed from 89% used to 92% used. Manual line tags are not implemented. Both instruments passed the final application flash hashes and read-only browser checks; see `hardware-final.json` and `firmware-sha256.json`. No field observation or synthetic target was written on hardware. `stakeout-phone.png` shows the synthetic browser/native-engine fixture; `hardware-stakeout-a.png` shows the real unconfigured Rover with guidance unavailable and collection locked.

Native validation: `python test/run_survey_tests.py` and `python test/run_host_tests.py`. Browser/CSV validation: set `TOPORTK_TEST_RECORD=tests/2026-09-11-instrument-scope`, run `node test/check_survey_browser.cjs`, then `python test/check_survey_exports.py`. Playwright/Edge are required. For read-only physical checks, additionally set `TOPORTK_HARDWARE_RECORD=hardware-final.json` and `TOPORTK_CHECK_EXPORT`, `TOPORTK_CHECK_TARGETS`, `TOPORTK_CHECK_CHECKS`, `TOPORTK_CHECK_STAKE` to `1`, then run `node test/check_point_hardware.cjs`.

Remaining physical qualification includes actual control independence, repeated occupation residuals, design/as-staked position checks, load under large SD jobs, disconnect/power failure and field accuracy. Code/browser fixtures and idle web checks cannot substitute for those tests.
