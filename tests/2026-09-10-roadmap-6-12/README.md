# UI 0.4: point review and export checkpoint

Date: 2026-09-10. Branch: `codex/survey-roadmap-through-12`.

## Implemented

- Feature 6: paginated ID/code search, full observation/setup inspection, notes, metadata revision checks, audited soft deletion/restoration and an offline north-up plot. Original measured coordinates stay unchanged; deleted IDs stay reserved.
- Feature 7, export portion: CSV with full observation/configuration/quality JSON, and complete per-job journal backups with original record CRC32 checksums. Consistent snapshot cursors prevent mixed exports while records change. CSV import and instrument restore remain pending.

## Results

- `python test/run_survey_tests.py`: pass, including 30-point pagination, metadata edit/restart/retry, deletion/restoration, revision conflicts, complete journal export and replay into a fresh native engine. Existing quality/storage tests and 96 independent PROJ cases passed (worst UTM difference 0.760 mm).
- `python test/run_host_tests.py`: pass for production main/profile/NVS/touch/JSON regressions.
- `node test/check_survey_browser.cjs`: production HTML/JS with native C++ engine passed at 390/768/1280 widths. Exercises collection/recovery, metadata edit/delete/restore, actual CSV/JSON downloads, quality abort, offline write locks, Base UI and no JavaScript errors. A deliberately delayed detail response is discarded when the active job changes.
- `python test/check_survey_exports.py`: independent Python CSV parser and CRC verification passed, including quotes/commas, formula-like metadata and preservation of original observation/setup. Backup verifier rejects corruption, record reordering, missing creation and configuration revision mismatch.
- A/B builds passed. Some Unit A PlatformIO attempts failed with Windows `cmd.exe: Permission denied` during binary generation; retry passed. Logs retain both outcomes.
- Feature 6 and subsequent export firmware were flashed application-only at `0x10000` to A/COM4 and B/COM10. Both esptool hashes verified. Bootloader/NVS/SD were not erased.
- Read-only physical checks passed: A remains Rover with two jobs/two records and the same active job; B remains Base with zero jobs/records. Both storage services/profile states came back. A served repeated point reads and downloaded its active job backup (one job-creation record) and an empty points CSV. Browser checks used a 390 px viewport; this is not another physical Android compatibility test.

`hardware-before.json`, `hardware-feature6.json`, `hardware-export.json`, and `hardware-final.json` capture persistence and runtime state. `hardware-*.png` are physical instrument web pages. Other screenshots and `fixture-*` downloads contain synthetic test observations only. No synthetic observations were written to either instrument.

## Remaining qualification

No live survey point existed on hardware for this checkpoint. Real measured-point edit/export, large SD datasets, interruption during data reads, full power loss, field accuracy and repeated control checks still require qualification. Native fault fixtures do not establish physical SD reliability.

## Reproduction

Run commands from `firmware/um980-display-demo`. Browser tests require Playwright/Edge. Set `TOPORTK_TEST_RECORD=tests/2026-09-10-roadmap-6-12` for browser artifacts. For read-only export hardware checks, set `TOPORTK_CHECK_EXPORT=1` and `TOPORTK_HARDWARE_RECORD=hardware-export.json`, then run `node test/check_point_hardware.cjs` while A/B are at their recorded LAN addresses.

Verify a downloaded backup without modifying it:

```powershell
python tools/verify_survey_backup.py path/to/job-backup.json
```

CRC checks detect accidental corruption, not deliberate modification. The checker verifies the envelope/history, not measurement accuracy or suitability for instrument restoration. Retain the original backup unchanged; do not copy its JSON file directly onto the SD journal directory.

Final delayed-response correction passed browser regression, A/B builds, hash-verified flashes and repeated read-only hardware export checks. `firmware-sha256.json` identifies the final application binaries. Implementation paused at the user-requested five-hour threshold (90% used); feature 7 import and ranks 8–12 remain.
