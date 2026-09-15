# R8 — Settings page over the pair-selection API (0.11.23-arch-r8b, 2026-09-15)

`/settings` now exposes the R6 pair-selection service to an operator on **both roles**: one page, reachable from every other page, that switches the correction medium, reports what the pair is actually doing and never dresses a queued request up as a working link.

## What the page does

It polls `GET /api/v1/settings` every second and keeps three things visibly separate — the confusion the checkpoint exists to remove:

- **Selected** — the durable `selected_transport` both instruments store.
- **Pending** — `candidate_transport` plus the live operation: kind, state, reason, coordinator, and a phase countdown bar.
- **Connected** — `peer_connected` and `corrections_fresh` on their own rows.

A switch is only ever sent from an explicit confirmation: the medium card opens `#confirmSection` with the interruption warning for that switch, and Confirm posts `{id, revision, op:'link.select', transport, confirm:true}` with a fresh 32-hex id and the revision the page last read. `202` renders as "queued", never as success; the outcome comes from the same endpoint and reads as complete only with `committed:true`, otherwise as the failure reason with the previous medium still shown as selected. Each refusal has its own sentence (`stale_revision`, `operation_busy`, `conflicting_id`, `operation_cancelled`, `link_storage_unavailable`, `test_operation_not_available`, `claim_control_first`, no-reply), and `recovery_required` points at the Link test page for a local selection on one instrument.

Reconciliation: the page renders the server's operation, so a reload during a switch retrieves it and never resubmits; a lost stream shows "no live settings" with every write control disabled instead of guessing; a takeover drops the shared token and locks the previous browser's controls immediately; Cancel is offered only for a request this instrument issued, matched by the same FNV-1a tag the instrument uses. There is no PIN or password field, and the page works identically when the instrument answers as Base or Rover.

## Two real defects, both caught before release

1. **The live run caught the page locking itself.** The instrument keeps reporting the last operation after it ends (a terminal `succeeded`/`committed` record stays in `GET /api/v1/settings`), and the first version treated any reported operation as active — so after one successful switch every route button stayed disabled forever. Fixed by gating on the in-flight states only (`negotiating`, `running`, `restoring`), and `check_settings_browser.cjs` now asserts that a reported outcome does not lock the route selector. That assertion was proven to fail against the pre-fix page (`AssertionError: a reported outcome must not disable switching back`) and passes after the fix.
2. **The check author caught a 200 % text overflow.** The first stylesheet used `repeat(auto-fit,minmax(15rem,1fr))` for the medium cards and `11rem` for the metric row; at `:root{font-size:32px}` those minimums (480 px and 352 px) exceed a 390 px viewport, so `documentElement.scrollWidth` was 569 px. The author reported the contradiction instead of weakening the assertion. Fixed with `minmax(min(15rem,100%),1fr)`, which keeps readable minimums at normal sizes and collapses at large text; the page's typography, padding and gaps also moved from px to rem, because the original px sizing would have made the 200 % rule pass vacuously.

## Verification

- `check_settings_browser.cjs` (new, 347 lines, 121 assertions, ~19 s): load states for both roles, guarded switch, one POST with the fixture revision/transport/confirm-flag and a 32-hex id, negotiating/running never claiming connection, committed success, `peer_unreachable` keeping the previous route, `stale_revision` and `operation_busy` refusals, reload rendering a peer operation with **zero** POSTs, stream loss disabling every write control and then reading back the reviewed outcome, takeover invalidating the earlier browser, 320/390/768/1280 px, 200 % text with focus reaching the controls, and no PIN/password anywhere.
- Full suite in one run: `check_gui_layout`, `check_web_browser`, `check_survey_browser`, `check_diagnostic_browser`, `check_debug_browser`, `check_ota_browser`, `check_settings_browser` — **all exit 0**. Harness note: `check_web_browser` prints a large report and dies when its stdout is a pipe (`| tail -1` failed 4/4, a file redirect passed 0/2); run these checks with output redirected.
- Both environments build (`embed_web_assets`: 9 sources, 10 routes, 134,984 bytes); host suites unchanged and passing.
- On board (`served-assets.json`): **22/22 served URLs match their canonical sources byte-for-byte** across both units, including `/settings`, with `/debug-nav.js` still 404.
- Deployed to both units by acknowledged OTA with verified boots (`0.11.22-arch-r8`, then `0.11.23-arch-r8b` after the locking fix).

## Live acceptance — real pair switches driven from one Rover-hosted browser

Every switch below was issued by clicking through `/settings` on Unit B (Rover), confirming the warning, and reading the page's own state while the instruments executed it.

| Switch | Page observations | Observed result |
|---|---|---|
| Wi-Fi → Radio | `Contacting the other instrument on Radio (SiK)… · route=Wi-Fi` → `Applying Radio (SiK). Corrections are paused. · route=Wi-Fi` → `Switch complete. Both instruments selected Radio (SiK). · route=Radio (SiK)` | revision 7 → 8, both units `selected_transport: sik`, `succeeded/committed/applied` |
| Radio → Wi-Fi | same three-step progression on the Wi-Fi card | revision 8 → 9, both units `wifi` |
| Wi-Fi → Radio (again, driven in the earlier run) | completed by the service while the script mis-logged the stale text | revision 9, both units `sik` |
| Radio → Wi-Fi (final) | `Contacting… · route=Radio (SiK)` → `Applying Wi-Fi…` → `Switch complete. Both instruments selected Wi-Fi.` | revision 10, both units `wifi`, `succeeded/committed/applied` |

During every switch the page showed the target as *pending* while the selected route stayed on the previous medium, which is exactly the distinction the checkpoint required. End state: both units `0.11.23-arch-r8b`, Wi-Fi selected, revision **10 on both**, one shared session `69610277`, peers connected, roles and jobs untouched (Base 2 jobs, Rover 0).

## Not covered here

- The **unreachable-target** case on hardware (`radio` selected with the radios unpowered) is covered deterministically by the check's `peer_unreachable` fixture; the bench variant needs the SiK radios switched off, and the R6 acceptance already exercised the service path for it.
- **Paired link tests** (`link.test`) still answer `503 test_operation_not_available`; the page renders `last_tests` as "not run yet" until R9/R9b.
- "Radio works without Base Wi-Fi" in the sense of a field topology was not re-proven here: both bench units share the local router. The page drives the same service whose radio path R6 validated; a field-topology run belongs to the R9 acceptance window.
