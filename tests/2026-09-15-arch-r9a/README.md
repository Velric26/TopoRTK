# R9a — paired quick tests over the operation service (0.11.26-arch-r9a3, 2026-09-15, paused)

R9a wires the two existing paired-test engines into the R6 pair-operation service. No new packet-test engine was written: `link_operation` gained a **run phase**, `link_service` admits `link.test` and owns the verdict, and `link_diagnostic` exposes a device-local quick-test wrapper with per-medium reports.

## What is implemented

**Operation core (`link_operation`)**
- `Kind::Test` is now admitted instead of refused. A test stages the tested medium exactly like a selection, then enters `Phase::Run` instead of `Phase::Commit` and **never adopts it**: no `persist_confirmed`, no `commit`, `committed` stays false, and the durable revision does not move.
- New reason `TestFailed` (ran and did not pass) and `TestUnavailable` (could not start), with `test_result(pass)` and `test_unavailable()` as the only ways a verdict reaches the operation.
- The requested profile rides the PLC1 body byte next to the kind (Request/Prepare only, values 0 clean / 1 injected), so the wire still carries no extra message.
- A test on the medium that is already selected still runs: the "already selected, settle immediately" shortcut applies to selections only.
- The coordinator settles `Succeeded` only when **both** verdicts pass; a missing peer verdict fails (`PeerUnreachable`), never passes. The delegate reports through `Done(kApplied|kFailed)`.

**Service (`link_service`)**
- `request_operation` accepts `Kind::Test` with a profile; the injected profile is refused for Wi-Fi (only the radio engine injects faults) as `Unsupported`, and an unknown profile string is a `400 profile_required`.
- The queue carries the profile; the settings payload publishes `operation.profile`.
- Both peers must arm the **same run id** — the engines drop a peer whose id differs — so the id is derived from the operation tag both sides already share: `100000 + tag % 900000`. (Found by the engine agent; per-side random ids would leave both sides in `Armed` until the 120 s peer timeout.)
- While a test owns the tested medium, production input and output are withheld (`test_running`), so generated frames never interleave with corrections.
- `kSettingsCapacity` raised 1536 → 2048 for the larger per-medium `last_tests`.

**Diagnostic engines (`link_diagnostic`)**
- The Wi-Fi-first admission rule is deleted: advanced diagnostics and quick tests take the medium they are started on.
- `diagnostic_quick_test_start/busy/finished/pass/cancel` wrap the existing `correctiontest` (SiK, profiles clean/injected) and `linktest` (Wi-Fi, canonical 30 s / 1000 ms / base-to-rover, clean only) engines.
- Per-medium persistence: `report_wifi` / `report_sik` slots plus a `latest` pointer, with the old single `report` key read once as the migration source; `last_tests` reports each medium's own latest run and survives a reboot. The reboot marker records the medium so an interrupted run lands in that medium's slot.
- Latent defect fixed: the per-medium summary parsed reports with a 1024-byte document while a real paired report serializes to 1022 bytes, so `last_tests.sik` could never populate; both parse sites now use 4096.
- New snapshot field `quick_test_error` names the refusing branch (`sik:radio_unavailable`, `engine_busy`, `cannot_save_test_start`, …), so "could not start" is never confused with "ran and failed".

**Settings page copy** is kind-aware: a test reads "Preparing the tested link…", "Running the paired quick test on X with injected faults…", "Quick test passed/failed on X. The selected route is unchanged." (R9b adds the buttons.)

## Defects the hardware runs found

1. **A quick test could never start.** `link_service` acquires the single diagnostic reservation for the operation's staging and holds it until unstage; the wrapper asked for that same reservation again and refused. Fixed by making the reservation best-effort in the quick-test path and tracking the run with its own flag. This was invisible locally because nothing faked the service's reservation.
2. **A failed arm was reported as a failed test.** Starting a test that could not run now settles `TestUnavailable` (proved by a portable case) instead of `TestFailed`.
3. **The driver read stale operations.** The instrument keeps reporting the last operation after it ends, so an acceptance script that accepts the first terminal state reads the *previous* test's outcome. Both the R8 and R9a drivers had this; the R9a driver now keys on the request id it sent.

## Verified

- Local: both environments build; `run_link_operation_tests.py` passes with seven new Test cases (no adoption, both sides run, failure never passes, missing peer verdict fails, already-selected medium runs, profile bounds, arm-failure reason, cancel on both sides) plus the existing codec boundaries; `run_settings_http_tests.py` (23 cases, including three new profile cases), `run_host_tests.py`, `run_survey_tests.py`, `run_ota_http_tests.py`, `run_pair_session_tests.py`, `run_update_tests.py` all pass; `check_settings_browser.cjs` still passes with the kind-aware copy.
- On hardware, `0.11.26-arch-r9a3`, both units, Wi-Fi selected, revision 10:
  - `POST /api/v1/settings {"op":"link.test","transport":"sik",…}` returns **202 queued** — the R6-era `503 test_operation_not_available` is gone.
  - A **Wi-Fi paired quick test ran and passed**: `last_tests.wifi = {run 842155, state done, reason complete, transport wifi, role ROVER, received 117, errors 0, pair_pass true, local_pass true}` — `pair_pass` requires the peer's own counters, so both instruments ran the test on the tested link.
  - The **selected route never changed** across every attempt (`wifi`, revision 10 before and after) — a test does not adopt.
  - **No synthetic traffic reached the UM980**: 0 of 32 Debug-log entries on receiver channels matched RTDG/RTC1/PLC1.
  - Per-medium persistence works: `last_tests` carries independent `wifi` and `sik` entries.

## Outstanding — resume here

1. **The pair-wide outcome for a passing run.** The Wi-Fi engine reported `pair_pass: true`, yet the operation settled `failed/peer_unreachable`: the coordinator's window expired without accepting the delegate's verdict. The engine side is proven; the `test_result` → `Done` → coordinator settle chain needs one focused look (most likely the delegate's `Done` is sent after its own engine finishes but the coordinator's 40 s window starts when it sends `Commit`, before the delegate has armed).
2. **The SiK quick test has not completed on hardware.** Attempts settled `failed/conflict` and `failed/peer_unreachable` with `quick_test_error` empty, i.e. the start was accepted; the engines then exchanged nothing. Needs the corrected driver (fresh-operation gating) to separate a genuine SiK-side problem from measurement.
3. **R9b** — the two warned buttons, phase countdown and durable result presentation on the Settings page.
4. **R9 acceptance rows not yet exercised**: warning cancellation sending no command, peer reboot and browser lease loss during an admitted test, and "test the unselected medium with no old-medium preparation".

The benches are idle and consistent: both units `0.11.26-arch-r9a3`, Wi-Fi selected, revision 10, one shared session, peers connected, jobs untouched (Base 2, Rover 0), no diagnostic run in flight. The evidence file is `quick-tests-live.json`; the throwaway driver that produced it is `firmware/.pio/r9a-live.cjs` (gitignored).
