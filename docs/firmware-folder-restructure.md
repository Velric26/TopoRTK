# Firmware folder restructure plan — PLANNED, NOT EXECUTED

Accepted 2026-09-14, deferred until the paused [R3 working tree](../tests/2026-09-14-arch-r3-transport/README.md) is resolved. This document is the execution record; treat it as the source of truth for the restructure when it happens.

## Target layout

```text
TopoRTK/
├── firmware/                      ← the one production PlatformIO project
│   ├── platformio.ini             (was firmware/um980-display-demo/platformio.ini)
│   ├── src/                       (was firmware/um980-display-demo/src/)
│   ├── test/                      (was firmware/um980-display-demo/test/)
│   ├── tools/verify_survey_backup.py
│   ├── README.md
│   └── .pio/                      (ignored; moves intact — contains private OTA evidence)
├── archive/
│   └── waveshare-board-demo/      (was firmware/waveshare-board-demo/)
├── docs/  tests/  tools/  ...
```

Rationale: `firmware/um980-display-demo/` is the production firmware (deployed `0.11.12-ui-r2b4` on both units), not a demo; the stale name goes away without inventing a new one. `waveshare-board-demo` is a historical board bring-up prototype and is archived, which removes the only objection to flattening (a nested project inside the project root).

## Prerequisite

The R3 changes in the working tree (new `radio_transport`/`wifi_transport`/`network_service` modules plus migrated consumers, host suite currently failing) must first be either parked on a side branch with the working record, or reverted. Never mix the restructure commit with the R3 checkpoint.

## Steps

1. **Archive move**: `git mv firmware/waveshare-board-demo archive/waveshare-board-demo`. Fix the relative doc reference inside its README (`docs/hardware/...` becomes `../docs/hardware/...`).
2. **Flatten move**: move the contents of `firmware/um980-display-demo/` (including local-only `src/wifi_credentials.h` and `.pio/`) up into `firmware/`; the emptied folder disappears. `git mv` the tracked files; the ignored files move on the filesystem.
   - `.pio/` must move intact and must not be deleted: it holds the private `ota-live-*` OTA evidence directories referenced by `tests/2026-09-14-ui-r2b-deployment/README.md` and the local credentials.
3. **`.gitignore` in the same commit**: `/firmware/um980-display-demo/src/wifi_credentials.h` → `/firmware/src/wifi_credentials.h`. Check for any other path-specific ignore rules. If this is missed, the real credentials file becomes tracked on the next commit.
4. **Script depth fixes** (fixed `__dirname` counts, not caught by path search): `test/check_live_bridge_hardware.cjs`, `test/check_diagnostic_hardware.cjs` and any other `check_*.cjs` resolving `'../../../tests/…'` become `'../../tests/…'`. Scripts using `Path(__file__).resolve().parents[1]` (e.g. `run_host_tests.py`) need no change. `run_ota_live.cjs` evidence paths are `__dirname`-relative and stay correct.
5. **Living docs sweep** (`firmware/um980-display-demo/` → `firmware/`): `docs/ota-operator-guide.md` (build/package/scripted-OTA/USB commands), `docs/firmware.md`, `docs/interface.md`, `docs/architecture-review.md`, `docs/hardware/unicore-um980/README.md`, `docs/transport-field-test.md`, `docs/roadmap-6-12-progress.md`, and the self-references in `firmware/README.md` and `firmware/test/README.md`.
6. **Dated evidence is NOT rewritten**: ~42 records under `tests/2026-09-*` reference `firmware/um980-display-demo/` verbatim. They were true when written; the commit message of the restructure records the mapping (`firmware/um980-display-demo/ → firmware/`, `firmware/waveshare-board-demo/ → archive/waveshare-board-demo/`).
7. **Verification**: `python test/run_host_tests.py` from `firmware/` (all 7 blocks), `pio run -e unit_a -e unit_b` (both SUCCESS), one `tools/package_firmware.py` dry run producing a valid `.tpk`, and a `grep` sweep confirming no remaining living-document references to the old path.
8. **Record**: one dated entry in `PROJECT.md` noting the restructure and the old→new mapping.

## Effort estimate

2–3 hours, difficulty 2/5 (the moves are mechanical; the reference sweep and script depth fixes are the work). No firmware behavior change; version string unchanged.
