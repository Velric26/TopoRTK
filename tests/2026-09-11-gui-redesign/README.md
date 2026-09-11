# UI 0.7 redesign checkpoint

See [design and screen organization](../../docs/web-gui-design.md). This is an interface change; survey engine, journal, receiver and quality logic are unchanged.

## Checks

- Production HTML/JavaScript with the existing native C++ fixture engine: create/configure/collect/restart, failed-quality rejection, checks/repeats, stakeout, manual line continuity, point editing and deletion/restoration, exports, stale response protection and offline command locking passed. Required fields inside collapsed setup groups revealed themselves on validation. The fixture engine is the unchanged native binary from the preceding validated manual-line checkpoint.
- Independent CSV/backup verifier passed full observation round trips, formula handling, checksum/audit checks, comparison reports and manual-line topology/break provenance.
- Layout suite passed every survey screen at 320/390/768/1280 px, 390 px with 200% text, keyboard focus, default collapsed secondary panels, visible phone navigation, and active-tab hover contrast. Representative phone/tablet/desktop screenshots were visually reviewed.
- Live-status suite passed 320/390/768/1200 px layouts, stale GNSS, disconnect, frozen data, invalid JSON and recovery, with only local GET requests and no JavaScript errors. `browser-results.json` records whether the live instrument portion also ran.
- Both firmware builds passed (`build-gui.log`). Both application flashes were hash-verified (`flash-gui-a.log`, `flash-gui-b.log`); SHA-256 and image sizes are in `firmware-sha256.json`.
- A/B read-only browser check passed (`hardware-final.json`): roles, existing jobs and journal counts preserved, new assets loaded, exports worked, unconfigured collection remained locked and neither unit rebooted during its checks. No NVS/SD erase, synthetic target or observation on hardware.

The sample North field screens and exported observations are fixtures. Hardware screenshots are desktop Edge at phone width, not actual Android-device qualification. Real phone keyboards, touch handling, sunlight contrast, long field sessions and survey accuracy remain unqualified by this checkpoint.

## Reproduce

From `firmware/um980-display-demo`, set `TOPORTK_TEST_RECORD=tests/2026-09-11-gui-redesign`. With Playwright/Edge and the native fixture executable available, run:

1. `node test/check_survey_browser.cjs`
2. `python test/check_survey_exports.py`
3. `node test/check_gui_layout.cjs`
4. `node test/check_web_browser.cjs` (optional Rover URL adds live reload/counter checks)
5. `pio run -e unit_a -e unit_b -j 4`

For the read-only A/B checkpoint, set `TOPORTK_HARDWARE_RECORD=hardware-final.json` and `TOPORTK_CHECK_EXPORT`, `TOPORTK_CHECK_TARGETS`, `TOPORTK_CHECK_CHECKS`, `TOPORTK_CHECK_STAKE`, `TOPORTK_CHECK_LINES` to `1`, then run `node test/check_point_hardware.cjs`. Review its stored baseline before running it after field data changes.

Implementation and hardware verification completed with 15% of the five-hour allowance remaining at the final pre-commit check. The 10% stop condition was not reached during implementation; weekly usage was not used.
