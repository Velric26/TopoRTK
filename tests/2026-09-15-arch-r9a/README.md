# R9a — paired quick tests over the operation service (0.11.28-arch-r9a5, 2026-09-15)

R9a wires the two existing paired-test engines into the R6 pair-operation service. No new packet-test engine was written: `link_operation` gained a **run phase**, `link_service` admits `link.test` and owns the verdict, and `link_diagnostic` exposes a device-local quick-test wrapper with per-medium reports.

## What is implemented

**Operation core (`link_operation`)**
- `Kind::Test` is admitted instead of refused. A test stages the tested medium exactly like a selection, then enters `Phase::Run` instead of `Phase::Commit` and **never adopts it**: no `persist_confirmed`, no `commit`, `committed` stays false, and the durable revision does not move.
- Reasons `TestFailed` (ran and did not pass) and `TestUnavailable` (could not start), reached only through `test_result(pass)` / `test_unavailable()`.
- The requested profile rides the PLC1 body byte next to the kind (Request/Prepare only: 0 clean, 1 injected), so no message was added.
- A test on the medium that is already selected still runs; the "already selected, settle immediately" shortcut applies to selections only.
- The coordinator settles `Succeeded` only when **both** verdicts pass; a missing peer verdict never passes.
- The run window is 60 s: it has to cover handing the run to the delegate, the canonical 30 s profile and the delegate's verdict coming back. (40 s was too tight and failed a passing run.)

**Service (`link_service`)**
- Admits `link.test` with a profile; the injected profile is refused for Wi-Fi (`503 test_operation_not_available`) and an unknown profile string is `400 profile_required`.
- The queue carries the profile; `operation.profile` is published.
- Both peers arm the **same run id** — the engines pair only on an identical id — derived from the operation tag both sides share: `100000 + tag % 900000`.
- Production input and output stay off the tested medium while the run owns it.
- The verdict is sampled when it is **final** (`diagnostic_quick_test_ready`), not the instant the engine's run ends: `pair_pass` depends on the peer's own report, so sampling early published `test_failed` for runs that passed moments later.
- `kSettingsCapacity` raised 1536 → 2048 for the larger per-medium `last_tests`.

**Diagnostic engines (`link_diagnostic`)**
- Wi-Fi-first admission deleted: advanced diagnostics and quick tests take the medium they are started on.
- `diagnostic_quick_test_start/busy/finished/ready/pass/cancel` wrap `correctiontest` (SiK, clean/injected) and `linktest` (Wi-Fi, canonical 30 s / 1000 ms / base-to-rover, clean only).
- Per-medium persistence `report_wifi` / `report_sik` plus a `latest` pointer, with the legacy `report` key as migration source; `last_tests` survives a reboot and the reboot marker records the tested medium so an interrupted run lands in the right slot.
- A latent defect fixed: the per-medium summary parsed reports with a 1024-byte document while a real paired report serializes to 1022 bytes, so `last_tests.sik` could never populate; both parse sites use 4096.
- `quick_test_error` names the branch that refused a start, so "could not start" is never confused with "ran and failed".

**Settings page** copy is kind-aware ("Preparing the tested link…", "Running the paired quick test on X …", "Quick test passed/failed on X. The selected route is unchanged."). R9b adds the buttons, countdown and durable result presentation.

## Defects the runs found

1. **No quick test could start.** `link_service` holds the diagnostic reservation for the operation's staging; the wrapper asked for that same reservation and refused. Fixed by making it best-effort in the quick-test path and tracking the run with its own flag.
2. **A failed arm was reported as a failed test.** Now `test_unavailable`, proved by a portable case.
3. **The verdict was sampled before it was final**, so every passing run published `failed/test_failed`. Fixed with `diagnostic_quick_test_ready`.
4. **The run window was too short** (40 s) for the delegate's verdict to come back; widened to 60 s.
5. **Acceptance drivers read stale state**: the instrument keeps reporting the last operation after it ends (both the R8 and R9a drivers had this), and a cancel must use the operation's own id — a fresh id is refused as `conflict`.

## Verified on hardware (0.11.28-arch-r9a5, both units, Wi-Fi selected, revision 10)

| Case | Expectation | Observed |
|---|---|---|
| Quick test on Wi-Fi | 202, operation succeeds, engine passes | **succeeded/applied**; `last_tests.wifi = done/complete, pair_pass true, received 117, errors 0` |
| Quick test on SiK | 202, both peers run on the tested link | **passed** in one run (`done/complete, pair_pass true, received 15`) and under bench loss truthfully reported `pair_pass false, received 10` with the operation failing — a lossy link is never a pass |
| Injected profile on Wi-Fi | Refused on the radio-only profile | `503 test_operation_not_available` |
| Cancel an admitted test | Finite outcome, route untouched | `202` → `cancelled/cancelled`, `selected_transport wifi`, revision 10 |
| Selected route and revision | Never move for a test | Wi-Fi, revision 10 before and after every case |
| Receiver traffic | No generated frame reaches the UM980 | 0 of 32 Debug-log entries on receiver channels |
| Per-medium results | Independent slots that survive a reboot | `last_tests` carries separate `wifi` and `sik` runs |

Local: both environments build; `run_link_operation_tests.py` passes with seven new Test cases plus the existing codec boundaries; `run_settings_http_tests.py` (23 cases, three new profile cases), `run_host_tests.py`, `run_survey_tests.py`, `run_ota_http_tests.py`, `run_pair_session_tests.py`, `run_update_tests.py` all pass; `check_settings_browser.cjs` passes with the kind-aware copy.

## Carried into R9b

- The Settings page buttons, per-test warnings, phase countdown and durable result presentation (the plan's R9 step 3).
- Acceptance rows that need those buttons: warning cancellation sending no command, peer reboot and browser lease loss during an admitted test, and the unselected-medium case driven from the UI.
