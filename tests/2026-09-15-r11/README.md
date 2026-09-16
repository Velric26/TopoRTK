# R11 release gate — scripted pair matrix and the physical checks it cannot cover

**Status: staged and waiting for one bench action.** The scripted driver is written and the release candidates are built and hashed; the run itself is on hold because one instrument runs a build that cannot accept an update (see *Why it is on hold*). Everything in this document is a command an operator or Main can run once the bench is reachable.

## What this gate is

The roadmap's R11 row is the release gate that runs **after the affected stages** and, finally, over everything: a two-unit startup/switch/test/OTA/outage matrix, the physical sunlight check, and the outdoor SiK RTK acceptance. This document covers the scripted matrix and names the physical checks as operator work; it does not claim any of it has run.

## Release candidates

| Unit | Package | Size | SHA-256 |
|---|---|---|---|
| A (Base) | `.pio/fw-a-0.11.33-arch-r10.tpk` | 1,313,632 B | `3005d8b8…25793a1` |
| B (Rover) | `.pio/fw-b-0.11.33-arch-r10.tpk` | 1,313,648 B | `f7f072c4…9fa90c9` |

`0.11.33-arch-r10` contains everything since the last deployment: the R06-R repairs (F01–F08), F05, R10a (composition root, `main.cpp` 2128 → 630 lines), R10b (bounded input work, optional CSV off the critical path), the designed restart, and the Link-mode/Debug UI work.

## Why it is on hold

Unit B runs `0.11.31-arch-r06r` and is in `recovery_required` with the staging reservation held — the pre-fix build. In that state it can neither **prepare** an update notice (its own link snapshot is inhibited) nor **acknowledge** one from Unit A, so OTA is blocked in both directions. The physical SiK pair underneath is healthy: both units are on Radio with one live session, and only B's operation state overrides its snapshot.

**The single bench action:** power-cycle Unit B, or on its touchscreen go to Link → Link mode → `USE WI-FI` → confirm (or `APPLY LOCAL / FOR RECOVERY`). Either clears the state; the reservation is RAM-only, so a power cycle is enough. After that every remaining step below is remote, including unit restarts — the new build's designed restart replaces the power-cycle requirement for future recovery.

## Runbook

```powershell
# 1. Deploy (from firmware/; the runner refuses an unacknowledged notice unless
#    --allow-unconfirmed is passed, and that override is an operator decision)
node test/run_ota_live.cjs --install A http://192.168.100.20 http://192.168.100.19 .pio/fw-a-0.11.33-arch-r10.tpk
node test/run_ota_live.cjs --install B http://192.168.100.19 http://192.168.100.20 .pio/fw-b-0.11.33-arch-r10.tpk

# 2. Scripted pair matrix (restarts both units through the designed restart at the
#    end of its restart phase, so the matrix itself exercises that path)
python test/run_pair_matrix.py --record tests/2026-09-15-r11 `
    --ota-a .pio/fw-a-0.11.33-arch-r10.tpk --ota-b .pio/fw-b-0.11.33-arch-r10.tpk

# 3. The R06-R acceptance (F01 chain) and the full offline set against the deployed image
python .pio/r06r-accept.py
python test/run_host_tests.py           # plus the other seven Python suites
$env:NODE_PATH='C:/Users/el_sp/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules'
foreach ($t in 'check_web_browser','check_survey_browser','check_gui_layout','check_diagnostic_browser','check_debug_browser','check_ota_browser','check_settings_browser') { node "test/$t.cjs" }
```

`run_pair_matrix.py` covers, in order: build identity and opposite roles on both units, the stored selection converging after boot, the local selection path on both roles, four pair-wide cutovers with revision agreement, a test on each medium (requiring a *fresh* stored run so a refusal cannot pass as evidence), cancellation leaving route and revision untouched, the designed restart of the Rover and then the Base with the pair renegotiating by itself, the documented 400/409 refusals, and — when the `--ota-*` packages are supplied — a guarded update of both units with jobs and records preserved. Its output lands as `pair-matrix.json` plus per-unit OTA logs in the record directory.

## Acceptance rows and who owns them

| Row | Owner | Status |
|---|---|---|
| Startup, selection, tests, cancellation, refusals, restarts | scripted driver | staged, not run |
| Guarded OTA of both units with saved-state preservation | scripted driver (`--ota-*`) | staged, not run |
| F01 chain (SiK test → stream handed back → real Radio cutover) | scripted acceptance | staged, not run |
| Isolated-medium matrix (radio-only and Wi-Fi-only boot orders, role change while idle, replay injection) | operator (media isolation) | not started |
| Designated physical sunlight readability on the real panels | operator | not started |
| Outdoor SiK RTK/recovery: 6 m baseline, 120 s stable fresh corrections, the 20 s Base-GPIO17 break and its bounded recovery | operator (field, antennas, inline break) | not started |
| Touchscreen verification of the Link-mode and restart controls on the panel | operator | not started |

## Record contents expected

`pair-matrix.json` (every case with its raw detail), `ota-A.log` / `ota-B.log`, the R06-R acceptance JSON, the browser-check JSON/PNG artifacts, and a short README stating what passed, what was skipped and which rows above remain operator work.
