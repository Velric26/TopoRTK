# R06-R remediation — F01–F04, F06–F08 (0.11.32-arch-r06r2, 2026-09-15)

Remediation of the [R06-R R5 implementation review](../../docs/r06-r.md), plus the defect that the hardware acceptance itself exposed. Offline evidence is complete; the final re-deployment and the isolated-medium matrix are on hold because one instrument currently needs a physical action (see *Bench state*).

## What changed

| Finding | Repair | Where |
|---|---|---|
| F01 | UART2 ownership is an explicit bounded flag instead of a derivation from the engines' retained run ids: a radio run owns the stream while armed/running plus a bounded terminal result exchange (peer verdict, or 5 s after the terminal state), and the same single value per turn gates the `radio_reserved` argument, every UART2 read/write and the published busy state. Released after the existing `save_result()` settle; no engine state is cleared, so `diagnostic_quick_test_ready()/pass()` keep reporting the finished run to the link owner. New `radio_owned` in the snapshot. | `link_diagnostic.{cpp,h}` |
| F02a | Absent, unreadable, wrong-length and CRC-invalid records are distinguished (`getBytesLength`), every validation accumulates instead of overwriting, and only a genuinely missing record keeps the historical Wi-Fi default; a corrupt legacy record is corruption, not absence. | `link_service.cpp` |
| F02b | The legacy R5 migration builds a sealed `Confirmed` and writes it with the checked write+readback before either owner reads it, so production and the operation engine share one baseline; an unverifiable migration stays indeterminate. | `link_service.cpp` |
| F03 | `Bridge::stop()` clears the output-fault latch (teardown is a session boundary; a same-session outage does not call it) and the snapshot override only inhibits a Radio-selected link. | `correction_bridge.h`, `link_service.cpp` |
| F04 | `route_control` takes an explicit physical ingress and drops any envelope whose declared transport differs from it, before operation, staging, production or notice dispatch; legacy `TPH1` classification and the unselected listener stay scoped to the selected medium. New `cross_medium_dropped` counter. | `link_service.cpp` |
| F06 | A different-boot untargeted Hello may reclaim the single unproved candidate slot only after that occupant's 3 s grace; solicited replies keep their binding and proven sessions are never displaced. | `pair_session.cpp` |
| F07 | A structurally valid RTM1 type-3 envelope carrying a recognized unsupported outer version reaches the existing incompatible-classification path instead of being dropped by the control gate. | `peer_update.h`, `link_service.cpp` |
| F08 | `settle(RecoveryRequired)` now releases the staging reservation (`unstage` on every outcome) while still retaining the durable pending record, and a local recovery selection consumes that record. Without this, `recovery_required` held the survey/diagnostic reservation, the local recovery path was refused, and the instrument was wedged until reboot. | `link_operation.cpp` |

## Offline verification

| Check | Result |
|---|---|
| `test/run_link_operation_tests.py` | PASS, including new assertions that a `recovery_required` outcome releases the reservation, that the record is retained, and that local recovery then returns the engine to a usable baseline and consumes it |
| `test/run_link_service_tests.py` (new) | PASS, 17 cases over the unmodified production service: missing record keeps the Wi-Fi default; CRC-invalid/truncated/oversized records and a masked confirmed record make storage not ok; sealed legacy migration matches the operation baseline; relayed cross-medium PLC1 does not pair while same-ingress does; a recognized version-2 envelope yields `protocol_incompatible`; a radio fault inhibits radio but not a Wi-Fi selection and clears on teardown |
| `test/run_pair_session_tests.py` | PASS; the dead-boot candidate reproduction now pairs in **4250 ms** (bound 6000 ms); the pre-fix copy of the same case aborts, so the regression is real |
| `test/run_host_tests.py`, `run_update_tests.py`, `run_survey_tests.py`, `run_ota_http_tests.py`, `run_settings_http_tests.py` | PASS |
| Browser checks (web, survey, GUI layout, diagnostic, Debug, OTA, settings) | PASS, 7/7 |
| `pio run -e unit_a -e unit_b` + packaging | SUCCESS, `0.11.31-arch-r06r` then `0.11.32-arch-r06r2` |

Each new service case was also rebuilt against a variant with that finding reverted and failed at the reviewed symptom, so no assertion is vacuous (recorded by the service agent).

## Hardware acceptance

`0.11.31-arch-r06r` was deployed to both units over OTA (Unit A's first attempt was refused as an unconfirmed peer notice and succeeded on retry once the pair had re-established; no override was used). The F01 acceptance then ran the real pair:

| Step | Observed |
|---|---|
| Both units on the remediation build | `0.11.31-arch-r06r / 0.11.31-arch-r06r` |
| SiK quick test from the coordinator | 202, settled with a stored report whose run id changed (a clean pass in one run: `pair_pass true, received 15`; a lossy run: `pair_pass false, received 13` — never a pass under loss) |
| UART2 handed back | `radio_reserved` false on both units within the exchange bound |
| Real Radio cutover after the test | 202 → `succeeded/applied`; both units on `sik` with one live session, `radio_reserved` false — the cutover needs exactly the stream the test held, so it is the F01 proof |
| Restored to Wi-Fi | `succeeded/applied`, both units on `wifi`, pair converged |

One run then hit a genuine failure worth recording: the Wi-Fi restore committed, but neither side could prove production on Wi-Fi within the apply window, so the coordinator restored Radio and could not prove that either. It published `recovery_required/restore_failed` (contract-correct) — and that exposed **F08**: the instrument kept the staging reservation, so the diagnostics local-recovery path was refused ("Finish the current test, survey or receiver operation first") and a pair-wide retry was refused 503 `link_storage_unavailable` by design. The state was self-consistent per side but not recoverable without a reboot, which the fix removes.

Two acceptance-script defects were fixed during this work so the evidence means what it says: each request now carries a fresh 32-hex id (a reused id names the previous run and the engines correctly refuse the reuse), the poll correlates the operation **id** rather than any terminal state (an older terminal operation was being mistaken for the new one), and pair convergence is polled instead of read mid-cutover.

## Bench state and on hold

- **Unit B needs a physical action.** It is on `0.11.31-arch-r06r` in `recovery_required` with the reservation held (the pre-fix build), so its local recovery is refused and it cannot acknowledge OTA notices. The physical SiK pair is healthy underneath: Unit A was returned to Radio locally, and both units report the same live session `2254401697` while B's operation state still overrides its snapshot. Power-cycling Unit B (or using the touchscreen Link screen → Link mode → `USE WI-FI` → confirm) clears it; then both units can take `0.11.32-arch-r06r2` over OTA and this acceptance can be re-run.
- **R6b physical taps** — the touchscreen Link-mode page is verified by host-rendered previews (`ui-link-3.png`, `ui-link-3-confirm.png`) and by host cases that assert the request shape, refusal copy, armed-tap expiry and gated local recovery; tapping the real panel is an operator check.
- **G01** — the isolated-medium cold-boot/restart/role/replay matrix from the review remains open; it needs media isolation and power cycles at the bench.

## Evidence

`hardware-acceptance.log` (both the readable case lines and the JSON block), `hardware-acceptance.json` (the first F01 run), `ui-link-3.png`/`ui-link-3-confirm.png` (the new Link-mode page and its armed state), the eight `suite-*.log` files and the seven `browser-*.log` files.
