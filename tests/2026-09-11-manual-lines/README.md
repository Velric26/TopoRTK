# Manual lines, UI 0.6

This completes the instrument scope selected in the [ESP32/Android review](../../docs/esp32-android-feature-split.md). Synthetic points in CSV, backups and browser screenshots are fixtures. They are not field results and were never written to the physical instruments.

## Contract and limits

The Collect page offers standalone points or manual line start/continue/end for topo occupations in a UTM job. Each action collects a new quality-gated observation. Saved points retain immutable `line_id`, `line_action`, `line_previous` and `line_code` alongside their original coordinates, setup and quality. End terminates a line; it does not add a polygon-closing segment. The Points page lists line state, original code, vertex count, last point and setup revision. CSV includes the tags and current line state/reason; full observation JSON and checksummed journal backups preserve provenance.

At most 64 lines exist per instrument. IDs are at most 32 printable characters and remain reserved within their job, including closed/broken lines. Continue/end require an open line, the current last vertex, the same code and job setup revision, and the original RTCM station/base reference (within 2 mm ECEF). Check/repeat/stakeout occupations cannot be tagged as topo line vertices. Existing point/target/receipt/journal limits are unchanged. Line reads are bounded to 25 rows per page with the existing journal snapshot cursor; SD access remains on the survey worker.

An accepted continuation/end that fails quality, is cancelled or is interrupted by restart breaks the line. Deleting a vertex, changing its feature code or reconfiguring an open line's job also breaks continuity. Restoring a deleted vertex or reverting its code does not reconnect the line. Notes/description edits do not break it. A failed first occupation creates no line because there is no saved vertex. Restart while idle after a confirmed vertex leaves the line open, subject to all continuation gates. Manual lines do not monitor quality between occupations: a rejected start request does not itself break an existing line. Continuous capture, connected-line rendering, code libraries and editing line geometry remain deferred.

## Validation

| Check | Result |
|---|---|
| Native survey suite | PASS: start/continue/end chain; stale previous vertex, wrong code and reused ID rejection; interrupted/failed/cancelled occupations; deletion/code restore stays broken; setup/base change; uncertain saved-write recovery/dedup; 64-line cap and pagination. Existing quality, journal, target, comparison, stakeout and precision tests also passed. |
| Projection oracle | PASS: 96 PROJ comparisons, worst difference 0.760 mm. |
| Main host suite | PASS: existing profile, persistence, touch and JSON regressions. |
| Production browser with native fixture engine | PASS: manual chain, failed continuation, deletion/restoration across restart, exports, no JavaScript errors; existing 390/768/1280 layouts and workflow checks passed. Phone line screens visually reviewed. |
| CSV/backup verification | PASS: immutable chain tags, current broken state after restore, original JSON agreement, failure intent and journal CRC envelopes. |
| A/B firmware builds | PASS; see `build-lines.log`. |
| A/B application flashes | PASS, esptool verified image hashes; see `flash-lines-a.log`, `flash-lines-b.log` and `firmware-sha256.json`. NVS/SD were not erased. |
| Physical read-only browser checkpoint | PASS on final run; see `hardware-before.json` / `hardware-final.json` and hardware screenshots. A stayed Rover with two unconfigured jobs, zero points and two journal records. B stayed Base with no jobs/records. Storage ready, no page errors, new line endpoint/UI available, unconfigured collection blocked. |

The first physical browser run timed out opening Unit B. A subsequent run hit a socket hang-up reading Unit A's line endpoint. Both USB ports remained present; HTTP reads and the final complete browser run succeeded without further flashing/resetting. The cause of these intermittent transport errors is unconfirmed. They are not evidence of continuous connection reliability. The final test also verified that neither unit rebooted during its checks.

Final idle readings: A free heap 219,752 bytes (minimum 211,696), PSRAM 8,257,391; B free heap 224,400 (minimum 215,440), PSRAM 8,259,543. These are small-job idle observations, not worst-case memory/latency qualification.

Physical checks used desktop Edge at phone width. Actual Android/field collection of line vertices, independent control accuracy, maximum-job load, power interruption and SD failure behavior remain unqualified. Neither instrument had a usable GNSS fix during this checkpoint. No observation or target was created on hardware.

## Reproduction

From `firmware/um980-display-demo`, run `python test/run_survey_tests.py` and `python test/run_host_tests.py`. Set `TOPORTK_TEST_RECORD=tests/2026-09-11-manual-lines`, run `node test/check_survey_browser.cjs`, then `python test/check_survey_exports.py`. The browser harness requires Playwright and Edge. Build with `pio run -e unit_a -e unit_b -j 4`.

For the read-only physical checkpoint, additionally set `TOPORTK_HARDWARE_RECORD=hardware-final.json` and `TOPORTK_CHECK_EXPORT`, `TOPORTK_CHECK_TARGETS`, `TOPORTK_CHECK_CHECKS`, `TOPORTK_CHECK_STAKE`, `TOPORTK_CHECK_LINES` to `1`; run `node test/check_point_hardware.cjs` with both units reachable. Its baseline assertions deliberately match the saved bench jobs/roles and must be reviewed before using it against later field data.

The five-hour usage allowance remained above the requested 10% stop threshold when this implementation finished. Features explicitly deferred to the phone/app remain deferred, not completed firmware features.
