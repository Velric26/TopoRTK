# F05 — advanced tests through the coordinator (2026-09-15)

Implementation of finding F05 from the [R06-R review](../../docs/r06-r.md), completing step 5 of the roadmap's R5–R6 section: the advanced diagnostic test is no longer armed twice by an operator who types the same six-digit code into two pages. One instrument requests a test; the pair-operation service admits it, both peers run the same parameters, and the device owns the phases and the verdict.

## What changed

- **Wire**: the operation `Message` carries the test shape (`profile`, `seconds`, `rate`, `mode`) in the PLC1 body's previously reserved bytes, and it rides only the kinds that carry one — Request, Prepare and Run. Ready/Commit/Done carry zeros, so an acknowledgement can never smuggle a stale shape, and a non-Test operation must carry zeros.
- **One refusal rule**: `link_operation::test_parameter_refusal(transport, profile, seconds, rate, mode)` names the offending parameter, and both the HTTP layer and the device start use it — an API caller and the instrument agree on what a medium offers (SiK: profile 0–1, 1000 B/s, one direction; Wi-Fi: profile 0 only, 200/1000/3000 B/s, either direction). `link_service::request_test` shares one admission body with `request_operation`, whose existing signature is unchanged, so the Settings quick-test path and its page are untouched: an omitted shape still means the canonical 30 s / 1000 B/s / one-direction profile.
- **Settings API**: `link.test` accepts the optional `seconds`/`rate`/`mode`, type-checks them (400 `invalid_request` for a non-integer), resolves omitted fields to the canonical values, and refuses a combination the medium does not offer with 400 `request_refused` **before** admitting anything. The published `operation` object now carries `seconds`/`rate`/`mode` beside `profile`.
- **Diagnostics page**: rebuilt around the service. No six-digit test code, no per-instrument arm/pairtest controls, no "select Wi-Fi first" prerequisite; the page requests a paired test (medium plus the shape that medium offers, behind the existing confirmation checkbox), renders the device-owned phase text and verdict, and still downloads the stored report. The wiring probe and the local fault self-test remain local actions, and the local correction selection is labelled as the local recovery path.
- **Drivers**: `run_sik_bench.py`, `check_diagnostic_hardware.cjs` and `check_correction_pair.cjs` now request tests through the settings API from one instrument and poll the operation outcome; the browser diagnostic check was migrated (its behavioural cases kept) and the live-bridge hardware check only dropped the removed control ids from its locator list.

## Verification

| Check | Result |
|---|---|
| `run_link_operation_tests.py` | PASS — parameter round-trip through reserve/forward/commit, non-Test kinds still zero, refusal rule, published snapshot values |
| `run_link_service_tests.py` | PASS — including the new case that the settings JSON publishes `seconds`/`rate`/`mode` for an admitted test and the canonical profile when the shape is omitted |
| `run_settings_http_tests.py` | PASS — the new 400s and the pass-through |
| `run_pair_session_tests.py`, `run_update_tests.py` | PASS |
| Browser checks (web, survey, GUI layout, diagnostic, debug, OTA, settings) | PASS, 7/7 — the diagnostics page rebuilt, and the Settings quick-test expectations unchanged |
| `pio run -e unit_a -e unit_b` | PASS — both environments build, and the change ships in `0.11.33-arch-r10` |

## Behaviour change to record

A request naming a shape the tested medium does not offer is now refused with **400 `request_refused` before admission**, where the R9a-era evidence showed the same combination reaching the device and failing as 503 `test_operation_not_available`. Nothing in the product depended on the later refusal — the Settings page never sends a Wi-Fi test with an injected profile — and the earlier refusal is what the contract asks for (a request the medium cannot run is a malformed request, and conflicts/refusals must fail before disruptive work). The R9a record keeps its historical observation.

## Still to do

- PlatformIO builds, packaging and deployment once the concurrent extraction lands; then a hardware pass with `test/run_pair_matrix.py`, which requests tests through exactly this path.
- The physical bench is on hold until Unit B takes one power cycle (its pre-fix `recovery_required` inhibits OTA in both directions), so no part of this feature is hardware-verified yet.
