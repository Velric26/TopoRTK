# TopoRTK Project Definition

> **Living document:** Keep this file as the concise source of truth for project scope, current hardware, validated decisions, and next steps. Put detailed wiring, test results, protocols, and implementation notes in separate documents as the project grows.

## 1. Purpose

TopoRTK is a low-cost, high-precision GNSS RTK surveying system for topographic fieldwork in Mexico. It consists of:

- One stationary RTK base.
- One mobile RTK rover.
- A direct base-to-rover correction link that does not require cellular coverage.
- A smartphone or tablet as the main field interface.
- Local controls and status displays on both instruments.
- Raw-data and result logging for validation and post-processing.

The system must support the existing field requirement for **UTM coordinates**, not only latitude and longitude.

The first objective is a prototype that can be tested alongside established survey equipment. It must not replace proven equipment for paid, cadastral, or legally significant work until accuracy, repeatability, coordinate handling, and failure behavior have been demonstrated.

## 2. Golden Rule

> Golden Rules: **Validate-first, incremental.** Every change is flashed and confirmed on real hardware before moving on. Do not batch multiple unverified behavioral changes.

This applies to firmware, wiring, power, GNSS settings, RTCM, radio settings, storage, user interfaces, and system integration.

- Ask all known questions together up front. If further user input is required, ask and end the turn; do not stay active while waiting.
- Pause implementation if the **five-hour** usage allowance reaches 10% remaining; update documentation, commit and push. Weekly usage is not this threshold.

### Firmware update policy

**OTA over Wi-Fi is the preferred flashing/update method.** Known local-router addresses are Unit A **192.168.100.20** and Unit B **192.168.100.19**; verify hardware identity because roles and DHCP addresses can change. Use the [OTA operator guide](docs/ota-operator-guide.md) for the web procedure, build/package commands and supported `run_ota_live.cjs --install` commands. USB/serial is the fallback for unavailable OTA, network-unreachable devices, initial provisioning and recovery. Full flash backups are optional for routine development updates.

The existing OTA runner validates the selected unit but still requires explicit URLs. Identity-based discovery across the two known addresses is recommended and documented as a follow-up, not an implemented capability. Implementation paused at the five-hour usage threshold; this update changes documentation only.

### Debug checkpoint — 2026-09-13

**2026-09-14 R1 parser extraction (0.11.6-arch-r1 deployed):** The [architecture review](docs/architecture-review.md) R1 checkpoint moved UM980 ASCII sentence parsing into a standalone `gnss_parser` module behind the planned `GnssParseStats` contract — behavior-identical by design, with a version bump so the refactor image is distinguishable from stock 0.11.5. Host tests, standalone BESTNAV valid/invalid/CRC fixtures and both PlatformIO builds pass on the deployed source. Both units then took 0.11.6-arch-r1 via the scripted OTA with acknowledged peer notices, verified new boots, Debug On after restart and unchanged saved survey state; see the [deployment record](tests/2026-09-14-arch-r1-deployment/README.md). R1 touches no UI or link behavior; the operator confirmed both touchscreens render unchanged.
**2026-09-14 R2a UI extraction (0.11.7-ui-r2a deployed):** The [architecture review](docs/architecture-review.md) R2a checkpoint moved touchscreen rendering, primitives, repaint cache, backlight and the touch state machine out of `main.cpp` into `ui_theme`/`ui_display`/`ui_screens`/`touch_input` modules; the composition root builds one pre-formatted `UiFrame` per render tick and executes typed gestures. Behavior is provably unchanged: all host tests pass and all seven host-rendered previews are byte-identical to the pre-refactor baseline. Both units took 0.11.7-ui-r2a via the scripted OTA with verified new boots and unchanged saved state; see the [deployment record](tests/2026-09-14-ui-r2a-deployment/README.md). R2b (light high-contrast palette and paged details) is next on this seam.
**2026-09-14 R2b sunlight-readable UI (0.11.8-ui-r2b deployed):** The [architecture review](docs/architecture-review.md) R2b checkpoint applied the light high-contrast palette, the size-2 typography floor with concise non-truncated strings, two-pixel control outlines, and two-page GPS/Link detail rows with PREV/NEXT on both instruments. Host tests, on-screen bounds asserts and the repaint cache pass; rendered previews were reviewed page by page, and two truncation defects were caught and fixed in review. Both units took 0.11.8-ui-r2b via the scripted OTA with verified boots and unchanged saved state; see the [deployment record](tests/2026-09-14-ui-r2b-deployment/README.md). Direct-sunlight readability on the physical panels remains the open human acceptance gate.
**2026-09-14 R2b field-feedback revision (0.11.9-ui-r2b2 deployed):** Operator feedback on the first R2b panels: the GPS PREV/NEXT pair was redundant and the Link-page Phone button glitched. GPS details now use one full-width page-flip button, and the Phone screen is the third Link page (rows → counters → phone) with PREV/NEXT across the three pages; tapping the active tab returns to its first page. Host tests updated for the new navigation and all pass; both units took 0.11.9-ui-r2b2 via the scripted OTA with verified boots and unchanged saved state.
**2026-09-14 Link key-overlap repair (0.11.12-ui-r2b4 deployed):** PREV/NEXT previously retained counter rows and cached controls when changing Link detail layouts, overlaying the Phone key controls. Detail navigation now clears the previous layout once while periodic renders remain cached; paging away hides the key and cancels replacement. Phone role/address fields no longer depend on a prior Setup visit, and the unreachable standalone Phone renderer was removed. The failing-before/passing-after framebuffer regression, all six Link transitions, key action/cancellation checks, Base/Rover rotation-0/2 smoke and both builds pass. Both instruments received the final guarded build via acknowledged OTA with verified boots, verified receiver profiles and unchanged saved survey state; see the [deployment record](tests/2026-09-14-ui-r2b-deployment/README.md). Physical panel confirmation remains an operator check.
**2026-09-14 R3 transport ownership (0.11.14-arch-r3 deployed):** The [architecture review](docs/architecture-review.md) R3 checkpoint gave UART2 framing to a sole `radio_transport` owner (shared RTM1/RTC1/RTDG decoder with whole-envelope consumption and native framing regressions), consolidated the three UDP channels in `wifi_transport`, and isolated the AP/STA lifecycle in `network_service` (network restart no longer touches corrections, GNSS or survey state). Consumer migration covered `main.cpp`, diagnostics, peer updates and the rover AP; Wi-Fi v2 wire formats, ports and correction safety gates are unchanged. Hardware validation on the bench: ATI probes, a paired synthetic radio test (15/15 both directions, `pair_pass` TRUE, zero errors) and live SiK session select/join through the new owner; live RTCM forwarding remains gated behind the standing outdoor SiK acceptance (no indoor RTCM source). Both units took `0.11.14-arch-r3` via guarded OTA with verified boots, Debug On and unchanged saved survey state — including one catch by the runner's identity check that exposed a lost `service_survey()` call during integration. See the [deployment record](tests/2026-09-14-arch-r3-transport/README.md).
**2026-09-14 repository restructure (housekeeping):** `firmware/um980-display-demo/` — the production firmware that grew out of the first display demo — was flattened to `firmware/`, and the early bring-up prototype `firmware/waveshare-board-demo/` was archived to `archive/waveshare-board-demo/`. Living documentation, tooling paths and script-relative depths were updated; dated records under `tests/` intentionally keep the historical paths. Host tests, both PlatformIO builds and packaging verified from the new root; no firmware behavior change. Mapping and steps: [restructure record](docs/firmware-folder-restructure.md).
**2026-09-15 R5 automatic pair negotiation (0.11.16-arch-r5 deployed):** The [architecture review](docs/architecture-review.md) R5 checkpoint removed manual correction-session handling. A portable `pair_session` core owns current-boot negotiation over the new 40-byte `PLC1` control body on route 0 (untargeted Hello; a solicited reply binds the observed boot and echoes its discovery token, which the Base retires at establishment, so replayed or delayed Hellos cannot restart a proven session); connectivity requires rolling bidirectional heartbeats within the existing four-second window, and a proven boot/role change clears session, replay and queue state while a same-boot outage retains it. A new `link_service` owns the selected link, the live bridge and COM2 output, reads a checked local preference from the new `topolink` NVS namespace, and the Wi-Fi wire format moved to version 3 with sender/receiver boot and session identity plus session-scoped sequence replay protection (version 2 is rejected once, not silently accepted). Notice exchange consumed by OTA now rides the proven session on the selected medium, and the old post-update manual rejoin is gone. Manual live-session entry was removed from Diagnostics/Debug, the hardware drivers and the living docs; the advanced synthetic test keeps its internal run ids until R9. Both units took the image **over OTA only**: Unit A with an acknowledged notice, Unit B with the operator-authorized explicit unconfirmed override (R4 and R5 cannot acknowledge each other mid-cutover; `run_ota_live.cjs` gained `--allow-unconfirmed`, without which an unacknowledged preparation still aborts before writing flash). Post-update hardware verification: automatic Wi-Fi pairing with matching session `102127552` and cross-confirmed boots, a fresh radio session (`197291837`) after selecting Radio locally on both units, a further fresh Wi-Fi session (`3923464542`) on return, `rssi_dbm:null` on the radio route, and unchanged saved jobs/records; see the [deployment record](tests/2026-09-15-arch-r5/README.md).
**2026-09-15 R10b, designed restart and R11 staging (in tree, builds verified; deployment pending the bench):** R10b bounded the per-turn input work and took the optional CSV writers off the correction loop's critical path, from measurements rather than guesses: the receiver now reads a 512-byte budget per turn and, past it, only while the backlog exceeds a 256-byte margin (so a burst is still drained within the turn and the plan's 2,048-byte cap is honoured as the ceiling, with receiver bytes never discarded), and the optional log commits at most one solution slot plus two event rows per turn with every drop counted and no card open inside the receiver's observer call. It deviates from the plan's prescribed 16-record queue and dedicated writer task (8 records, bounded inline commits) and per plan step 5 stays **not accepted** until a measured saturated-link/logging run on hardware shows no new UART overruns, bounded backlog and no correction-age/touch/HTTP/durability regression. The optional CSV writer now matches the plan's prescription (16-record queue, per-record destination, counted oversized/full drops, a card failure that disables optional logging behind a bounded retry window), with one reasoned deviation recorded: it is a bounded lowest-priority drain stage on the loop rather than a second task, since a second task could only contend on the SD mutex or block on the card. The instrument can now restart itself in software — `POST /api/v1/diagnostic {"op":"restart","confirm":true}` or the Debug page's two-tap control both queue the same `esp_restart()`, gated on the controller lease, no transfer in progress, no run or probe, and the survey/diagnostic reservation free, with a 500 ms grace window that holds the reservation and a published `restart_pending` - which removes the power-cycle requirement for recovery and makes the G01 restart-only-one-unit rows remotely drivable. Wiring it exposed two real layout defects: `draw_button` places a subtitle at `rect.y+46`, so both the new restart button (56 px) and the Link page's recovery button (48 px) drew their subtitles outside their own rects; both rects are now tall enough. The R11 gate is staged with release candidates `0.11.33-arch-r10` built and hashed, `run_pair_matrix.py` covering identity/roles, convergence, cutovers, tests, cancellation, restarts, refusals and an optional guarded OTA, and the runbook in the [R11 gate record](tests/2026-09-15-r11/README.md); the pair still needs one physical action to take the new build.
**2026-09-15 R10a extraction complete (in tree, builds verified; deployment pending the bench):** `main.cpp` fell from 2128 to **630 lines** with no behavioural drift: `instrument_status` (status/readiness policy behind an explicit input struct), `gnss_service` (sole UART1 owner with a published snapshot and typed profile/command requests), `correction_service` (COM2 output path, bounded queue, station guard, reference capture, health and counters), `device_settings` (config/base records with the same NVS encodings and checked readback; the link preference stays with `link_service`), `diagnostic_log` (optional CSV writers with identical rows and best-effort drops), `board_hardware` (display/expander/I2C/touch/LCD reset, no domain policy), `usb_console` (UART0 surface with the same allowlist and refusals), `correction_wifi` (Wi-Fi envelope identity, replay admission, legacy rejection, Base transmit and Rover receive), `ui_presenter` (snapshot to pre-formatted `UiFrame`) and `status_surface` (web-status JSON), plus the survey bridge in `survey_service`. Each slice was proven by comparing the host suite's status snapshots and all UI previews byte-for-byte against the pre-change commit — the only delta anywhere is the suite's own simulated clock — with two PlatformIO builds, eight Python suites and seven browser checks passing after every slice. What stays in the root is deliberate: the `setup()`/`loop()` order, `is_base()` and the installed service contracts, the single `instrument_status::Inputs` composition, the gesture dispatch and the render entry points.
**2026-09-15 F05 advanced tests through the coordinator (in tree, builds verified; deployment pending the bench):** F05 completes the R5–R6 step that removes manual two-instrument test matching: the operation wire carries the test shape (profile, duration, rate, direction) on the kinds that carry one, a single `test_parameter_refusal` rule names what a medium does not offer and is shared by the settings API (400 `request_refused` before admission) and the device start, `link_service::request_test` shares one admission body with `request_selection`, the Settings quick-test path and page are unchanged, and the diagnostics page and the three bench drivers request paired tests from one instrument instead of typing a shared code — see the [F05 record](tests/2026-09-15-f05/README.md). In parallel, R10a began: the status/readiness policy moved into `instrument_status.cpp` behind an explicit input contract, and the UM980 receiver service moved into `gnss_service.cpp` as the sole UART1 owner with a published `GnssSnapshot` and typed profile/command requests, keeping the allowlist and acknowledgement-before-verified semantics. Both moves are proven behaviour-identical by comparing the host suite's status snapshots and all 28 UI previews byte-for-byte against the pre-change commit (the only delta is the suite's own simulated clock), with eight Python suites, seven browser checks and both PlatformIO builds passing. Remaining extraction: the correction service, the settings/storage split, the Wi-Fi correction framing and a composition-only `main.cpp`.
**2026-09-15 R06-R remediation (0.11.32-arch-r06r2 built; Unit A on 0.11.31-arch-r06r, Unit B awaiting a power cycle):** The [R5 review](docs/r06-r.md) findings were repaired: UART2 ownership after a Radio test is an explicit bounded flag and a real Radio cutover now succeeds immediately after a SiK quick test (F01); stored-selection loading distinguishes absence from corruption, accumulates validation failures and migrates the R5 record with a sealed checked write (F02a/F02b); the radio output-fault latch is scoped to Radio and cleared at teardown (F03); control envelopes are bound to their physical ingress medium (F04); a dead-boot candidate is reclaimed after a 3 s grace, cutting the stale-Hello delay from 21.25 s to 4.25 s (F06); and a recognized unsupported protocol version reaches the incompatible-classification path (F07). A new `run_link_service_tests.py` compiles the production service against an NVS double and both portable cores (17 cases), and each repaired finding's case fails against a reverted variant. The acceptance then exposed and fixed a further defect (F08): `recovery_required` kept the staging reservation, so the prescribed local recovery was refused and the instrument was wedged until reboot — every settle now releases the reservation while retaining the durable pending record. Offline: eight Python suites and seven browser checks pass, both builds package. Hardware: the F01 chain (SiK quick test → `radio_reserved` false → real Radio cutover applied → Wi-Fi restored) is verified on both units; an intermittent Wi-Fi cutover failure on the bench left Unit B in `recovery_required` on the pre-fix build, which needs one physical power cycle before the final re-deployment. See the [remediation record](tests/2026-09-15-r06r/README.md).
**2026-09-15 R06-R R5 implementation review (documentation only, repairs not implemented):** The [R5 review](docs/r06-r.md) assessed the current `0.11.30-arch-r9c` implementation against the agreed pairing, persistence, test and OTA contracts using source review and temporary host-only reproductions, without firmware changes, flashing or device commands. The portable pairing core passed its existing suites on both media, but the integration has blocking defects: a completed or cancelled SiK test retains UART2 ownership and blocks automatic Radio bootstrap and OTA notices until a local reselection or reboot; corrupt, truncated or unreadable link records silently fall back instead of reporting `recovery_required`, and a valid R5 Radio migration leaves production and the operation engine on different medium baselines; and a latched radio output fault inhibits a subsequently proven Wi-Fi link. The review also records cross-medium control admission, the still-manual advanced diagnostic matching workflow, a bounded stale-candidate delay and one incompatible-version classification gap, plus the isolated-medium cold-boot/restart/role/replay matrix still required for a genuine R5 sign-off. Remediation sequence, acceptance criteria and evidence limits are in [R06-R](docs/r06-r.md).
**2026-09-15 R6 pair-wide selection and Settings API (0.11.17-arch-r6 deployed):** The [architecture review](docs/architecture-review.md) R6 checkpoint adds a portable `link_operation` core (fixed ≤256-byte engine) on top of R5's pairing: one pair-wide operation at a time, coordinated by the Rover unit, with `negotiating → running → restoring → succeeded/failed/cancelled/interrupted/recovery_required` states, bounded 20/10/10-second windows and cancellation tombstones. Operation control (`Request/Prepare/Ready/Commit/Done`) rides the PLC1 body on the operation's own medium; a second staging engine proves the candidate medium while production keeps running and the proven session is adopted at commit. Durable records in `topolink` (`confirmed{transport, revision}` and `pending{kind,target,previous,tag,revision,committed}`) use magic/version/CRC32 with write-and-readback, migrate the R5 single-key preference, report an uncommitted record as `interrupted` at boot, and turn a readback failure into `recovery_required` when readiness is inhibited. `GET/POST /api/v1/settings` publishes `revision`, `selected_transport`, `candidate_transport`, `peer_connected`, `corrections_fresh`, the operation (with `phase_remaining_ms`, `reason`, `committed`, `coordinator`) and per-medium `last_tests`; `link.select` and `link.cancel` queue with 202, `link.test` returns 503 until R9, and stale/busy/conflicting/malformed/unauthorized requests keep their documented 409/400/401/403 codes. HTTP never writes NVS, and a post-202 refusal is published as that operation's `failed` outcome instead of a retroactive status. All offline suites pass, including two new ones (`run_link_operation_tests.py`, `run_settings_http_tests.py`); both builds package as 1,260,944-byte `0.11.17-arch-r6` images. Two OTA uploads of the image failed mid-transfer on the bench link (`Upload connection closed or receive error` at 70 %, `Upload stalled for 12 seconds` at 25 %) and left both instruments healthy, so both were flashed over USB instead. Hardware acceptance then passed on the Wi-Fi side: equal revision and `selected_transport` on both roles, no-op selection reporting `succeeded/applied` without moving the revision, a Base-forwarded request reaching the Rover coordinator, 409 `stale_revision`, 503 `test_operation_not_available`, 400 for malformed id and missing confirmation, cancellation of an admitted operation leaving both units unchanged, and a radio candidate failing as `peer_unreachable` without any false success. Cutover acceptance then passed on hardware in **both directions** after the bench switch and a radio power cycle: Wi-Fi→Radio and Radio→Wi-Fi, each issued as a pair-wide `link.select` (one from the Rover, one forwarded from the Base), every one `succeeded/applied` with `committed true`, both units agreeing on the medium and advancing the durable revision together (0→1→3→4→5→6), and each cutover adopting the proven candidate session as production (sessions `3224978904`, `3512594722`, `2830885222`) with the units left on Wi-Fi. The hardware runs also exposed and fixed five real service defects, each with a portable regression: a stale admission clock, commit retries resetting an applied operation, a retried prepare re-adopting a finished operation (plus an inherited `committed` flag), a stale baseline after a local recovery selection, and discovery traffic unicast to a stale learned peer address that stranded the Wi-Fi candidate (operations and pair bootstrap are now broadcast on the medium in use). Only the post-commit restoration path remains unexercised on hardware (covered by fault injection); see the [checkpoint record](tests/2026-09-15-arch-r6/README.md).
**2026-09-15 R9b quick tests on the Settings page (0.11.30-arch-r9c deployed):** The second R9 half puts the paired quick tests on the Settings page: two warned buttons (Radio/Wi-Fi) with an injected-faults option for the radio test, a warning that must be confirmed before anything is sent and whose cancellation sends **no request at all**, the device-owned phase countdown on the operation's own 60 s window, and each medium's durable stored result (verdict with frames sent and received, errors, run id, state/reason) rendered after reload. Failure copy separates a test that could not start from one whose peer never reported from one that simply did not pass, and never claims the route changed. The live acceptance drove a real quick test from the page in a browser on the Rover and it reported `Quick test passed on Wi-Fi. The selected route is unchanged.` with the operation `succeeded/applied`; a deterministic check now covers 31 cases including the injection refusal, the countdown, the three failure copies and a test surviving takeover. The same run exposed a real defect: an ArduinoJson lifetime bug in the per-medium summary (a linked `const char*` pointing into a destroyed parse document) scrambled the stored strings while counters survived — fixed by copying those fields and by refusing to publish a body that is not a whole report, after which both units self-healed their derived summaries. See the [checkpoint record](tests/2026-09-15-arch-r9b/README.md).
**2026-09-15 R9a paired quick tests (0.11.28-arch-r9a5 deployed):** The R9 service half wires the two existing paired-test engines into the R6 pair-operation service with no new test engine: `link_operation` gained a run phase that stages the tested medium and **never adopts it** (no confirmed record, no revision change), `link_service` admits `link.test` with a clean/injected profile and withholds production input/output from the tested medium while the run owns it, and `link_diagnostic` wraps `correctiontest` (SiK) and `linktest` (Wi-Fi) behind a quick-test API with per-medium `report_wifi`/`report_sik` persistence and a new `quick_test_error` field that names the refusing branch. Both peers arm the same run id, derived from the shared operation tag. Three defects were caught by running it rather than assuming it: the wrapper asked for the diagnostic reservation the service already holds (no quick test could start), a failed arm was reported as a failed test, and acceptance drivers read the instrument's still-reported *previous* operation as the current outcome. Verified on hardware: `link.test` returns 202 instead of 503, a Wi-Fi quick test ran and passed on both instruments (117 frames, zero errors, `pair_pass true`), the selected route stayed Wi-Fi at revision 10 throughout, and no RTDG/RTC1/PLC1 reached the UM980. Hardware acceptance then passed on both media: the Wi-Fi test ran and the operation reported `succeeded/applied` with `pair_pass true`, 117 frames and zero errors; the SiK test passed in one run (15 frames) and under bench loss truthfully failed instead of inventing a pass, with the radio test coordinated over the tested medium; the injected profile is refused on Wi-Fi; cancelling an admitted test ends it `cancelled` with the route and revision untouched; the selected route moved in no case; and no generated frame reached the UM980. Four defects were found by running it rather than assuming it, including a verdict sampled before the peer's report made every passing run publish a failure. R9b (buttons, warnings, countdown, durable result presentation) follows; see the [checkpoint record](tests/2026-09-15-arch-r9a/README.md).
**2026-09-15 R8 Settings page (0.11.23-arch-r8b deployed):** The [architecture review](docs/architecture-review.md) R8 checkpoint exposes the R6 pair-selection service to an operator on both roles at `/settings`, reachable from every other page. The page keeps three things visibly separate — the durable `selected_transport`, the pending `candidate_transport` with its live operation (state, reason, coordinator, phase countdown) and the pair's `peer_connected`/`corrections_fresh` — and a switch is only ever sent from an explicit confirmation carrying a fresh 32-hex id plus the revision the page last read; `202` renders as queued, never as success, and the outcome is read back from the same endpoint so a reload retrieves it instead of resubmitting. Losing the stream disables every write control rather than guessing, a takeover locks the previous browser immediately, and Cancel is matched by the same FNV-1a tag the instrument uses; every refusal has its own sentence and `recovery_required` points at a local selection. Two real defects were caught before release: the live run showed a finished (still-reported) operation permanently locking the route buttons, now gated on the in-flight states and pinned by an assertion proven to fail pre-fix, and the new check's author found the first stylesheet overflowing at 200 % text because rem grid minimums exceeded the viewport — fixed with `minmax(min(…,100%),1fr)` after moving the page's typography to rem, which also made that acceptance rule meaningful. All seven browser checks pass, on board 22/22 served URLs are byte-identical to their canonical sources and all four real switches driven from one Rover-hosted browser completed as `succeeded/committed/applied` with both units advancing one revision together (7→8→9→10), resting on Wi-Fi at revision 10 with roles and jobs untouched; see the [checkpoint record](tests/2026-09-15-arch-r8/README.md).
**2026-09-15 R7 browser assets (0.11.18-arch-r7 deployed):** The [architecture review](docs/architecture-review.md) R7 first step moved every page and script out of C++ string literals: seven blobs (status, survey, survey tools, diagnostics, Debug page and navigation, update UI) now live byte-identically under `firmware/web/` and are described by `web/assets.json` (source, MIME, URLs). `tools/embed_web_assets.py` runs as a PlatformIO pre-build script, validates the manifest and generates one `web_assets.h` per environment with flash-resident arrays plus the route table; `web_http.cpp` serves from that table, keeps the role-dependent root, 404s unknown asset URLs and derives `max_uri_handlers` from the asset and API tables instead of the previous fixed 24. The six embedded-header sources are deleted and the browser checks read the canonical files. Both units took 0.11.18-arch-r7 by acknowledged OTA with verified boots and unchanged jobs/points; every served URL was compared byte-for-byte with its canonical source on board (`served-assets.json`) and the pages render styled with no JavaScript errors — see the [test record](tests/2026-09-15-arch-r7/README.md). Still open in R7: explicit navigation and one shared controller client. **R7 step 2 followed in `0.11.21-arch-r7d`:** the three page-local control clients are replaced by one shared `web/api.js` (single control token, claim/release, JSON requests that drop the bearer on any 401 and notify subscribers, so a takeover invalidates the previous controller) and `web/navigation.js` replaces the injected `debug-nav.js` with an explicitly declared, gated Debug tab. Each page reconciles its displayed ownership with the instrument's `X-Controller` answer, because a read-only poll never 401s. Five checks that seeded the now-retired per-page token keys were silently degrading (two of them release control in hardware teardown) and were updated; the on-board comparison shows 20/20 served URLs byte-identical to their canonical sources and a real browser run of all five pages reports the shared client present, no legacy keys and the gated Debug tab working. **R7b shipped earlier in `0.11.19-arch-r7b`:** the Debug card is titled `Updating Firmware` and carries the live percentage while bytes are in flight (observed live as `Updating Firmware – 7%` → `– 14%` during a deliberately aborted transfer whose instrument counters read 180224/1262720, with the firmware retained); a bystander page cannot see it because the instrument pauses web access during the transfer.
**2026-09-14 R4 transport-aware status (0.11.15-arch-r4 deployed):** The [architecture review](docs/architecture-review.md) R4 checkpoint introduced `instrument_status`, one fixed-size snapshot with peer/corrections/GNSS/readiness facets computed from explicit inputs, consuming the pre-R4 ready/age thresholds verbatim. The LCD dashboard and Link page, the web JSON and the CSV logger all read the same interpretation: during SiK the Link page reports `LINKED` with `N/A (RADIO)` signal, the web JSON keeps `rssi_dbm:null` (never a fabricated Wi-Fi dBm), and `solution.csv` gains a `link_transport` column with an empty `link_rssi_dbm` when unknown; `Engine::quality` remains the separate job authority and JSON keys are unchanged. Host coverage gained a memory-backed SD double and a transport-agreement block (LCD/web/CSV asserted consistent in radio and Wi-Fi modes). Both units took `0.11.15-arch-r4` via guarded OTA with verified boots, Debug On and unchanged saved survey state; see the [deployment record](tests/2026-09-14-arch-r4-status/README.md).
**2026-09-14 timeout changes (0.11.3 deployed):** Two operator-requested behavior changes shipped as 0.11.3: the OTA transfer deadline is raised from 120 to 300 seconds (the verified transfer took 118.602 s, nearly exhausting the old limit; the 12-second no-progress stall limit is unchanged), and Debug no longer expires after 15 idle minutes — it stays On until disabled on the instrument or web, or until restart; the `activity` keep-alive operation and browser extend control were removed with the timer. All host/HTTP/service suites and both browser regressions pass (Node.js 24.19.0 / Playwright 1.63.0 installed for the browser checks; see [test record](tests/2026-09-14-timeout-changes/README.md)). Both units then received 0.11.3 over Wi-Fi OTA using the standard browser workflow, each with acknowledged paired notices, verified new boots, Debug Off after restart and unchanged saved survey state — **both instruments are now aligned on 0.11.3**. The 300-second deadline itself governs the next update cycle; deliberate rollback acceptance and the SiK bench session remain the next P0 items.

**2026-09-14 SiK bench session (P0 complete):** The production SiK correction link was validated end-to-end with real receivers for the first time. Both radios probed as `RFD SiK 2.0 on HM-TRP` on UART2; the paired synthetic radio test passed 15/15 both directions with zero loss; a live SiK session (Base A session 367058285, Rover B joined) carried **~4.0 RTCM frames/s continuously** — the full UM980 profile even indoors — with ~94% delivered and every delivered frame forwarded into the Rover UM980 (zero output faults), wire errors ~3.6% of envelopes. A new reusable driver (`test/run_sik_bench.py`) automates probes, paired tests, session selection and counter observation over HTTP. Findings: bench-range loss matches the historical close-range signature (re-measure outdoors); the Base link card shows `Radio Disconnected` during working SiK (pre-existing no-ACK semantics — refinement candidate); peer recovery confirmation requires the Rover GNSS gate (indoors shows "Update overdue", by design). The session was left live for on-screen observation. See [bench evidence](tests/2026-09-14-sik-bench/README.md). Remaining: outdoor RTK FIXED/recovery/range validation.

**2026-09-14 rollback acceptance (P0 complete):** The OTA safety paths host doubles cannot prove were accepted on hardware using flag-guarded fault-injection images (distinct version strings, inert in normal builds). A pending boot forced to fail the health gate was marked invalid and rolled back by the bootloader (~30 s); a pending boot that hung was recovered by the task watchdog; a raw-socket mid-upload disconnect (RST at 4,308 of 1,239,120 bytes) was caught by the receive-error path and the 12-second stall detector, retaining firmware, boot and records. Unit A finished on a clean stock 0.11.5 flash with a fresh receipt; all records/jobs preserved throughout. Findings: peer recovery confirmation requires the Rover's GNSS-quality gate, so indoors the peer shows "Update overdue" after an update (by design); stale rollback receipts only affect the cosmetic boot line; package-from-stale-build almost shipped a wrong image — version verification before flashing is now mandatory practice. See [rollback acceptance evidence](tests/2026-09-14-rollback-acceptance/README.md). Remaining P0: SiK bench session with real UM980s. Hotspot OTA remains untested.

**2026-09-14 Debug default-on (0.11.5 deployed):** Operator request: Debug is now **On by default at startup** on both units — a constructed session starts enabled, a manual disable persists only until the next restart, and there is still no idle timer, no stored preference and no HTTP enable path. Host tests, both browser regressions and both builds pass; packages were built and verified. Both units then took 0.11.5 over Wi-Fi OTA with acknowledged peer notices, verified new boots and unchanged saved survey state, and the post-reboot Debug state was asserted **On** on both units — the touchscreen enable step is no longer needed before future updates. See [test record](tests/2026-09-14-debug-default-on/README.md).

**2026-09-14 merged link card (0.11.4 deployed):** On both instrument screens, the Correction Link and Link Signal cards are merged into one **BASE/ROVER LINK** indicator: transport first (`Wi-Fi` or `Radio`), then `Disconnected` or the signal strength in dBm (`Radio Connected` for a linked SiK, which reports no RSSI). The dashboard reflowed to three equal cards; web status is unchanged. Host tests and both builds pass (host-rendered preview verified the new layout), and both units took 0.11.4 over Wi-Fi OTA with acknowledged peer notices, verified boots, Debug Off after restart and unchanged saved survey state — the first update cycle governed by the new 300-second deadline (~68 s and ~46 s transfers). See [test record](tests/2026-09-14-merged-link-card/README.md). Both units are unplugged from USB; all flashing was over Wi-Fi.


**2026-09-14 OTA success:** Unit B successfully updated over local Wi-Fi from 0.11.1 to 0.11.2, rebooted and passed startup acceptance; saved survey state is unchanged and Debug is Off. Unit A remains 0.11.0 and displayed preparation, updating and reconnection notices. The transfer took 118.602 seconds with no receive-timeout retries, close to the 120-second deadline. Performance margin and deliberate rollback remain outstanding. Version 0.11.2 fixes a reproduced stale-clock race that could expire new update requests immediately. See [hardware evidence](tests/2026-09-14-ota-success/README.md). Stop after documentation/commit/push; five-hour allowance was 13% remaining at the checkpoint.

**2026-09-14 upload recovery:** 0.11.1 adds bounded receive-timeout retries, closes failed uploads promptly, and pauses in-flight browser Debug requests. Unit B is USB-installed and verified at 0.11.1 with saved state preserved; Unit A remains 0.11.0. Software checks pass. Next: physically re-enable Unit B Debug and repeat Wi-Fi OTA with USB recovery available. See [validation and remaining limits](tests/2026-09-14-ota-recovery/README.md).

**2026-09-14 update:** 0.11.0 implements the Debug OTA workflow and paired update notices. Build/host/browser checks pass. Both units are USB-installed and verified at 0.11.0 with saved survey state preserved. The first Unit B Wi-Fi OTA upload timed out after 37,336 bytes; the old boot remained running, update lock/pause cleared, and settings/jobs were unchanged. Unit A displayed preparation and Updating notices. Successful OTA and rollback acceptance remain pending; see the [live evidence](tests/2026-09-14-ota-live/README.md). Full flash backups are optional for routine development; prefer a small settings snapshot when practical. SiK correction recovery requires a fresh Base session and Rover rejoin after an update. See the [operator guide](docs/ota-operator-guide.md) and [USB evidence](tests/2026-09-14-ota-usb/README.md). Earlier entries below describe historical checkpoints.

Follow-up: the portable paired update-notice codec/state machines now pass corruption, replay/reorder, bounded retry/deadline and recovery tests. Transport/UI integration and actual OTA remain outstanding. See the [notice core evidence](tests/2026-09-13-update-notice/README.md). The user accepted completing OTA before the combined USB installation; no additional flash has been performed.

The 0.10.5 source adds touchscreen-enabled **Debug**, a 15-minute user-idle timeout, a disabled-with-instructions web tab, and bounded passive communications capture. Both targets build and host/browser checks pass, but this increment is **not flashed or hardware accepted**; the deployed baseline remains 0.10.4. OTA and paired update notices remain planned, with upload disabled. See the [complete Debug/OTA plan](docs/debug-and-ota.md) and [validation record](tests/2026-09-13-debug/README.md). A combined USB installation after OTA preparation is a recommendation to reduce battery-holder access, not evidence of hardware validation.

### Field-interface rules

- Keep the main display limited to correction-link state, required GNSS fix, link quality, horizontal uncertainty, and an actionable warning. Put GNSS and network diagnostics one swipe away.
- Use centralized, explicit definitions of `READY`, `CONNECTED`, and `GPS FIXED` across banners, text, warnings, logs, and future mobile interfaces.
- Use green banners only for the required ready/connected/fixed state and grey otherwise; always repeat the state in text and never rely on color alone.
- Optimize for direct sunlight with high contrast, consistent typography, and concise labels.
- Keep equivalent dashboard information sections equal in height and spacing, and repaint only changed regions to avoid visible LCD flicker.
- Use checksum-validated GNSS UTC date/time, show project-local time at fixed `UTC-6`, retain UTC in diagnostics, and show a clear waiting state when time is unavailable.
- With no onboard ambient-light sensor, use gradual GNSS-time-based brightness and bias toward full brightness whenever daylight is possible or time is uncertain. Preserve a manual override.

Detailed screen layout, state logic, navigation, and brightness behavior belong in `docs/interface.md`.

## 3. Project Goals

- Produce repeatable high-precision RTK positions for topographic surveying.
- Operate as a self-contained base-and-rover pair without internet access.
- Provide a clear, offline smartphone/tablet field workflow.
- Cost substantially less than conventional proprietary survey equipment.
- Prefer open protocols, documented interfaces, and replaceable components.
- Make both instruments field-serviceable and interchangeable where practical.
- Log enough raw data, configuration, and quality information to audit results.
- Support point collection, antenna height, UTM coordinates, stakeout, and common export formats.

## 4. Current Hardware

**Temporary receiver exception (2026-09-11):** one UM980 carrier has a COM1 transmit fault. A replacement is planned. Recommended interim assignment: known-good receiver as Base, suspect receiver as Rover using COM2 and the existing ESP32 Wi-Fi bridge. No role change has been applied by this note. Direct SiK use of the suspect RXD1 remains untested, and an ESP32-to-SiK serial bridge remains unimplemented. See the [temporary implementation and replacement plan](docs/hardware/unicore-um980/temporary-com1-fault-plan.md).

| Qty. | Component | Role | Current status |
|---:|---|---|---|
| 2 | Unicore UM980 RTK GNSS modules | RTK engine; one per instrument | Both passed USB and bidirectional TTL2; Unit A generated live base RTCM and Unit B reached `RTK FIXED` over Wi-Fi |
| 1 pair | Holybro SiK Telemetry Radio, long-range 1 W, 915 MHz, open source | Base-to-rover RTCM transport | USB configuration backed up and saved/restart-verified; ESP32/RTCM integration, range, and legal use pending; see [radio record](docs/radio.md) |
| 2 | Waveshare ESP32-S3 3.5-inch capacitive touch display boards, 320 x 480, Wi-Fi and Bluetooth 5 | Control, local UI, logging, and phone/tablet connectivity | Both displays, TTL2 links, automatic A/B profiles, and Wi-Fi RTCM bridge validated; touch hardware works but is not required for startup |
| 2 | K700 full-band L1/L2/L5 BeiDou/GPS/GLONASS/Galileo survey GNSS antennas | Primary base and rover antennas | Validation on hold: purchased cable has the wrong antenna-side center-contact gender; exact connector must be verified before replacement |
| 2 | GNSS HA-609 helix antennas | Compact prototypes and comparison testing | First two-unit open-sky Wi-Fi RTK test reached `RTK FIXED`; controlled accuracy and K700 comparison pending |
| 2 | BNO085 IMUs | Orientation experiments and possible future pole-tilt work | Available; not accepted as survey tilt compensation |

The UM980 modules are mounted on BDRTK-980 carrier boards. The seller manual is archived under `docs/hardware/unicore-um980/`; the exact physical PCB revision, active-antenna supply behavior, USB-to-UART channel mapping, and full power budget still require bench verification.

## 5. System Architecture

```text
BASE                                             ROVER

K700 or HA-609                                  K700 or HA-609
GNSS antenna                                    GNSS antenna
      |                                               |
    UM980                                           UM980
      | COM2                                          | COM2
 Waveshare ESP32-S3 -- SiK ))) 915 MHz ((( SiK -- Waveshare ESP32-S3
 local display + log                            local display + log
                                                      |
                                             local Wi-Fi/Bluetooth
                                                      |
                                             smartphone/tablet UI
```

### Responsibility split

- **UM980:** satellite tracking, base observations, RTCM generation, and rover RTK solution.
- **Holybro SiK pair:** transparent correction-data transport.
- **ESP32-S3:** device configuration, monitoring, storage, coordinate/workflow logic, local display, and mobile-interface hosting.
- **Smartphone/tablet:** main project, collection, stakeout, review, and export interface.
- **BNO085:** experimental orientation input only until a complete calibration and accuracy-validation process exists.

The selected SiK integration now uses a separate ESP32 UART (TX GPIO17 / RX GPIO18) at each end and keeps UM980 COM2 on GPIO43/44. The owner has no camera and does not plan one, releasing the shared camera GPIO17/18 for this purpose. This avoids the suspect COM1 and supports later status/control traffic; unlike the earlier direct-receiver proposal, ESP32 restart will interrupt corrections. The bridge firmware, stream separation and transport-aware readiness checks remain unimplemented. See [radio wiring and configuration](docs/radio.md).

Base and rover should use the same enclosure and electronics layout where practical. Their role should be selectable in software so either instrument can serve as base or rover.

## 6. Initial Interface Direction

The chosen first implementation is an **offline, rover-hosted responsive web application** reached through local Wi-Fi. During development, both instruments can join a configured local 2.4 GHz router so a PC can reach the Rover directly; the saved `wifi local` / `wifi direct` toggle restores the self-hosted link when needed. For field use without a router, the Rover provides a password-protected access point and the tablet or phone opens the same local interface. This requires no cellular service, internet connection, app-store account, or device-specific installation.

Wi-Fi is the primary phone/tablet link for live status, configuration, point collection, and log/export transfer. Once the SiK link is validated, it is the preferred base-to-rover RTCM transport; Wi-Fi RTCM remains a test/fallback transport rather than a dependency of the field interface. The rover remains the source of truth: jobs, point records, configuration audit data, and logs are persisted on its SD card, not solely in the browser.

Bluetooth LE is a later secondary link for provisioning, recovery, or compact diagnostics. It is not the primary survey UI or log-transfer transport. A browser-home-screen shortcut is sufficient initially; a full PWA is deferred because a local HTTP device address does not provide the HTTPS/service-worker environment needed for reliable PWA installation. A WebView/Capacitor-style Android APK may package the proven web UI later if native BLE, USB, filesystem/share integration, background behavior, or a dedicated field-app experience proves necessary. A fully native Android app is deferred until a concrete requirement cannot be met by the shared UI and an Android bridge.

Minimum interface functions:

- Select and clearly show base or rover role.
- Display `NO FIX`, `FLOAT`, and `FIXED` state prominently.
- Show estimated horizontal/vertical precision, correction age, satellite count, baseline, radio/connectivity state, battery, and logging state.
- Create and manage survey jobs.
- Enter antenna type and measured antenna height.
- Collect, name, code, average, review, and delete points.
- Show UTM coordinates and the active coordinate-system configuration.
- Support stakeout.
- Export documented interoperable formats.
- Warn when a measurement fails configured quality limits.

The onboard displays provide setup, status, diagnostics, and recovery. They are not required to duplicate the complete mobile workflow.

## 7. Initial Technical Baseline

These are starting points, not validated final settings.

### GNSS and corrections

- RTCM 3.x correction data.
- Start with a measured, multi-constellation MSM4 stream at 1 Hz.
- Enable only messages required by the UM980 rover and available radio bandwidth.
- Measure actual RTCM bytes per second before changing the radio air rate.
- Log raw GNSS observations where supported for independent checking and PPK.

### Communications

Current wiring and radio-control scope are consolidated in [electronics architecture](docs/electronics-architecture.md). The [reusable transport field test](docs/transport-field-test.md) runs synthetic SiK/Wi-Fi tests on the two ESP32s, with tablet arming, paired counters and a persistent downloadable report. The production SiK RTCM bridge and BLE adapter remain future work.

| Link | Initial purpose |
|---|---|
| UM980 UART | Configuration, NMEA/proprietary status, RTCM, and raw observations |
| SiK 915 MHz | Base-to-rover RTCM correction stream |
| Wi-Fi | Local-router development/browser access plus Rover-hosted field UI, local status/control, and log/export transfer; current RTCM path retained only as a validated test/fallback transport |
| Bluetooth 5 LE | Later provisioning, recovery, or compact diagnostics; not the primary survey UI or log-transfer path |

Begin radio testing on the bench at low RF power. Validate serial framing, packet flow, correction age, loss recovery, interference, and legal settings before range tests.

### Storage

Each unit should record, as available:

- Hardware and firmware versions.
- GNSS, radio, and coordinate-system configuration.
- UTC time and base coordinates.
- Raw observations and RTCM/configuration records.
- Point data, antenna height, quality state, and job metadata.
- Errors, restarts, correction outages, and storage health.

### Mechanical design

- Center the survey antenna over the pole axis.
- Define and mark a repeatable antenna reference point (ARP).
- Document the offset from the ARP/mount to the antenna reference or phase center.
- Keep GNSS coax short, secured, 50-ohm, and free of unnecessary adapters.
- Keep the 915 MHz antenna and noisy digital/power electronics away from the GNSS antenna.
- Preserve access to USB/UART and removable logs for recovery and validation.
- Validate display readability, controls, weather sealing, strain relief, balance, and full-day power in field conditions.
- Add shielding or extra filtering only in response to measured interference.

### Power

Each unit now has a dedicated 3S 18650 pack and a 12 V-to-5 V, 15 W buck converter. The provisional design feeds the Holybro 1 W radio directly from the 3S pack and the ESP32/BDRTK carrier from regulated 5 V. Use a 3 A time-delay fuse near each battery positive lead for the initial design, subject to measured peak current and wiring limits. Matched cells, reverse-insertion protection, undervoltage protection, a physical switch, clean GNSS power, locking connectors, a BMS before field use, and safe USB/back-feed behavior remain mandatory.

See the detailed [power architecture](docs/power.md).

Do not connect a 3S pack to a Waveshare single-cell battery input. Confirm every board's allowable input voltage before assembly.

## 8. Base Coordinate and Reference Workflow

RTK measures the rover relative to the base. A `FIXED` solution can still be wrong if the base coordinate, antenna height, datum, epoch, projection, localization, or geoid model is wrong.

Use these base-position methods in order of preference:

1. **Known control point:** occupy a verified monument and enter its coordinate and antenna height correctly.
2. **Static GNSS control:** log sufficient raw observations and establish the point through a documented post-processing workflow tied to appropriate INEGI/RGNA control.
3. **Autonomous survey-in:** use only for local testing or relative work unless later tied to known control.

Every base setup record must include:

- Point identifier and source.
- Latitude/longitude/ellipsoidal height used by the receiver.
- Reference frame, datum, and coordinate epoch.
- UTM zone, hemisphere, units, and any localization/grid-to-ground settings.
- Geoid/vertical model and resulting orthometric-height treatment.
- Antenna model, ARP, measurement method, and antenna height.
- Occupation time, raw files, operator, and acceptance check.

## 9. Mandatory Field Quality Sequence

Before storing an accepted survey point:

1. Confirm the correct job, coordinate system, units, base point, and base antenna height.
2. Confirm corrections are current and the radio link is healthy.
3. Require a stable RTK `FIXED` state; do not treat `FLOAT` as survey quality.
4. Check estimated precision, satellite/geometry indicators, and configured tolerances.
5. Keep the pole centered, stable, and level unless validated tilt compensation is active.
6. Average observations for the configured duration.
7. Store point ID, code, antenna height, solution state, quality values, UTC time, and base ID.
8. Reobserve important points independently and close on known checks.

No accuracy claim is accepted solely because the receiver reports `FIXED`.

## 10. Validation Plan

### Phase 0 — Inventory and bring-up

- [ ] Record exact models, revisions, connectors, voltage levels, pinouts, and firmware.
- [ ] Power and communicate with every component independently.
- [ ] Establish reproducible build, flash, configuration, and log-retrieval procedures.

### Phase 1 — Wired RTK

- [ ] Obtain standalone output from both UM980 modules.
- [ ] Send base RTCM directly to the rover over a cable.
- [ ] Demonstrate repeatable `FLOAT`/`FIXED` reporting and raw logging.
- [ ] Measure RTCM message content and bytes per second.

### Phase 2 — Radio RTK

- [x] Back up both SiK USB configurations; save transparent framing and low bench power; verify complete settings after software restart (2026-09-11).
- Current radio UART setting: **57600 baud** on both, saved/restart-verified. Repeats at approximately 60 cm failed at both 115200 and 57600; cause remains unresolved. GNSS COM2 baud is unchanged.
- [ ] Resolve USB binary-transfer byte loss and pass both directions before integrating RTCM; see [bench evidence](tests/2026-09-11-sik-radio-configuration/README.md).
- [ ] Insert the SiK pair at low power on the bench.
- [ ] Compare transmitted and received correction streams.
- [ ] Measure latency, correction age, dropouts, recovery, and usable range.
- [ ] Test RF interference with Wi-Fi, display, storage, and power conversion active.

### Phase 3 — ESP32-S3 integration

- [ ] Implement transparent data paths before automated behavior.
- [ ] Add logging and diagnostics.
- [ ] Add local display functions one verified behavior at a time.

### Phase 4 — Mobile workflow

- [x] Validate `wifi local` / `wifi direct` development switching, local-router peer discovery, and Direct-Link fallback. Last-mode restoration across a physical power cycle remains pending.
- [x] Implement the read-only Rover browser status page and `GET /api/v1/status`; PC/local-router bench validation completed on Unit A on 2026-09-10. See the [test record](tests/2026-09-10-rover-web-status/README.md).
- [x] Implement a password-protected Rover Wi-Fi access point and touchscreen connection instructions. Initial Android connection reported working by the user on 2026-09-10; full device/recovery matrix remains open. See [phone Wi-Fi validation](tests/2026-09-10-rover-phone-wifi/README.md).
- [x] Implement the initial scope of web roadmap ranks 2–5: SD jobs, controlled commands, WGS84 UTM/height setup, base/antenna setup and quality-controlled point collection. See [workflow and limits](docs/survey-workflow.md) and [validation](tests/2026-09-10-survey-workflow/README.md). Field accuracy and physical storage-failure acceptance remain open.
- [x] Complete the agreed ESP32 scope through rank 10: review/plot, export/backup, small reference targets, checks/repeats, basic stakeout and manual line tags (UI 0.6).
- [ ] **Next:** physical field qualification and the deferred phone/app backlog through rank 12; see [responsibility split](docs/esp32-android-feature-split.md).
- [ ] Serve static UI assets and a versioned local API from the Rover; use a live status channel plus bounded request/response commands.
- [ ] Implement status, configuration, projects, point collection, UTM, log/export download, and survey-quality warnings.
- [x] Add an on-device connection aid identifying the Rover SSID, local address and UI version; show/hide the key locally and support confirmed replacement.
- [ ] Test reconnection, accidental browser close, Android no-internet warnings, low battery, and SD failure without losing rover-side records.
- [ ] Decide whether the proven UI needs an Android APK wrapper for native BLE/filesystem/USB or background behavior; do not create an APK solely for packaging.
- [ ] Reproduce the essential workflow and settings of the existing survey system.

### Phase 5 — Field prototype

- [ ] Build serviceable, pole-mounted base and rover enclosures.
- [ ] Validate power, thermal behavior, weather resistance, controls, RF layout, and ergonomics.

### Phase 6 — Survey validation

- [ ] Repeat known points across multiple days and satellite geometries.
- [ ] Compare results with established professional equipment.
- [ ] Compare K700 and HA-609 performance.
- [ ] Test open sky, trees, walls, multipath, radio obstruction, and fix recovery.
- [ ] Establish supported accuracy claims and operational limits from recorded evidence.

For every test, save the configuration, reference coordinates, antenna setup, environment, raw data, results, pass/fail criteria, and conclusion.

## 11. Safety, Regulatory, and Survey Constraints

- Confirm that the selected 915 MHz hardware, firmware, power, antennas, duty cycle, and operating mode comply with current Mexican IFT requirements before field transmission.
- Higher RF power is not automatically better; begin low and increase only when testing justifies it.
- Never power or connect unidentified legacy cables without mapping every conductor.
- Verify UART electrical levels before interconnecting boards.
- Protect power inputs against reverse polarity, shorts, undervoltage, and unsafe charging.
- IMU pole-tilt compensation is a separate metrology feature requiring alignment, calibration, temperature testing, and independent accuracy validation.
- Legally consequential surveying may require qualified personnel, calibrated equipment, documented control, and prescribed procedures regardless of receiver performance.

## 12. Current Decisions and Open Items

### Current direction

| Decision | Status |
|---|---|
| Two UM980 receivers for interchangeable base/rover instruments | Both passed USB and bidirectional TTL2; Unit A generated live RTCM as a temporary base and Unit B reached `RTK FIXED`; controlled base coordinates and accuracy remain unvalidated |
| K700 antennas as primary survey antennas | Selected; validation on hold until the correct cable is obtained after exact connector identification |
| HA-609 antennas for compact tests | Standalone acquisition and first two-unit `RTK FIXED` session passed; controlled accuracy and K700 comparison still required |
| Holybro SiK 1 W 915 MHz correction link | Settings saved/restart-verified and backed up; USB binary tests found byte loss, so link acceptance, RTCM integration, range and compliance remain pending |
| Waveshare ESP32-S3 touch boards as controllers/displays | Display, UART, automatic role/profile recovery, BESTNAV horizontal-accuracy parsing, Wi-Fi RTCM path, and SD mount/read-back validated on both units; structured logging is ready for a two-sided field session |
| Android tablet/phone interface | Rover-hosted responsive browser UI over local Wi-Fi selected as the first implementation; local-router mode and Direct-Link fallback passed a two-unit hardware transport test, with last-mode power-cycle restoration still pending; BLE is secondary and an APK/native app is deferred until a demonstrated requirement justifies it |
| BNO085 orientation/tilt experiments | Deferred until the basic level-pole RTK system is validated |

The direct ESP-to-ESP Wi-Fi checkpoint passed on 2026-09-06 using Unit A as an access point and Unit B as a station. Checksummed, sequenced test packets reached Unit B without detected gaps or invalid packets. The CRC-validated RTCM bridge then passed its antenna-less bench checkpoint. In the subsequent HA-609 open-sky test, Unit A generated live corrections and Unit B reached `RTK FIXED` with 27-28 satellites, HDOP 0.5, increasing RTCM counters, and zero displayed RTCM/network or NMEA checksum errors. This validates one functional RTK session, not absolute accuracy, repeatability, recovery, range, or security.

Default temporary role assignment: Unit A = base-test; Unit B = rover. The touchscreen Setup page now selects Base or Rover on either instrument, with Wi-Fi and RTCM direction following that selection. Each ESP32 saves role, brightness mode, and the base RTCM enable flag in NVS and reapplies the selected volatile UM980 profile after startup or receiver-link loss. Commands require acknowledgement and final role verification. Unit B passed bench role reversal and settings restoration across ESP32 hardware resets; physical touch usability, full power-off/on, and two-unit reversed-role RTK validation remain pending. The redesigned display includes Home/GPS/Link/Setup tabs and cached high-contrast status cards. The safe USB console remains available. The unit letter identifies hardware, not a permanent role; web role selection and full survey-safe base configuration remain open work. See the [validation record](tests/2026-09-08-touch-role-settings/README.md).

### Open items

- Exact UM980 carrier-board revision and complete electrical interface.
- Exact K700 connector family and required cable-end shell/contact genders; current cable does not mate with the antenna.
- Final RTCM message set, serial rate, radio air rate, and update frequency.
- Exact K700/HA-609 specifications, phase-center information, and active-antenna requirements.
- UTM zone, reference frame/epoch, geoid model, localization, codes, stakeout, and export requirements from an existing job.
- Final point-quality thresholds and observation durations.
- Phone/tablet browser compatibility and offline recovery behavior.
- BNO085 purpose and a defensible calibration method if tilt compensation is pursued.
- Battery state-of-charge monitoring: pick the cheapest simple option and bench-validate it ([power architecture](docs/power.md#state-of-charge-monitoring-open--find-the-cheap-simple-way)); a pack ran flat unnoticed on 2026-09-15, and the dark panel was first mistaken for a dead board.
- Final batteries, regulators, filtering, connectors, field controls, enclosure, and pole mount.
- Mexican homologation and permitted settings for the 915 MHz radio system.

## 13. Project Documentation

Create focused documents as details become real and testable:

```text
PROJECT.md                 Project scope and current system decisions
docs/hardware/             Hardware index plus one self-contained folder per device
docs/firmware.md           Build, flash, configuration, and firmware architecture
docs/gnss-rtcm.md          UM980 configuration and RTCM message set
docs/radio.md              SiK connector, configuration, compliance, and range tests
docs/power.md              Battery, regulator, fuse, BMS, and USB power architecture
docs/coordinates.md        UTM, datum/epoch, geoid, localization, and base control
docs/interface.md          Local and mobile UI behavior
docs/validation.md         Test procedures and acceptance criteria
docs/decisions.md          Short architecture decision records
tests/                     Test template and dated test records
```

Only create these files when their content is needed; avoid duplicating information between them.

See the [architecture review and implementation roadmap](docs/architecture-review.md) for the current ownership assessment, proposed behavior contracts, priorities, difficulty/model assignments, and validation gates. See the [R06-R implementation review](docs/r06-r.md) for the reviewed R5/R6/R9 integration findings, off-target evidence, prioritized remediation sequence and the outstanding isolated-medium hardware acceptance gate.

## 14. References

- [Unicore UM980](https://en.unicorecomm.com/products/um980/)
- [Waveshare ESP32-S3 Touch LCD 3.5](https://www.waveshare.com/esp32-s3-touch-lcd-3.5.htm)
- [Holybro SiK Long Range 1 W radio](https://holybro.com/products/sik-telemetry-radio-1w)
- [RTKLIB](https://rtklib.com/)
- [INEGI Red Geodesica Nacional Activa](https://www.inegi.org.mx/temas/geodesia_activa/)
- [INEGI RGNA RINEX data](https://www.inegi.org.mx/app/geo2/rgna/)
- [INEGI Mexican gravimetric geoid](https://inegi.org.mx/temas/geoide/)
- [IFT-008-2015](https://sidof.segob.gob.mx/notas/docFuente/5411997)
- [NOM-208-SCFI-2016](https://www.ift.org.mx/sites/default/files/d-gob-02-nom208scfi.pdf)

## 15. Revision History

| Date | Change |
|---|---|
| 2026-09-04 | Consolidated the original TopoRTK charter with `SurveyRTK.md`; retained current hardware, offline/UTM workflow, architecture, validation plan, and Mexico-specific constraints while removing duplicate purchasing analysis. |


### 2026-09-10 web interface checkpoint

UI 0.4 implements roadmap feature 6 point review/offline plotting and the export/backup portion of feature 7. Both A (Rover) and B (Base) are flashed with verified application hashes; read-only browser/persistence/export bench checks passed. CSV import and features 8–12 remain outstanding. See [implementation state](docs/roadmap-6-12-progress.md) and [validation evidence](tests/2026-09-10-roadmap-6-12/README.md). No field accuracy claim follows from these bench checks.


### 2026-09-11 instrument / Android scope review

The [responsibility review](docs/esp32-android-feature-split.md) defers bulk project/file processing, continuous-topo session management, localization and full COGO to the phone/app. The web client already executes on the phone. UI 0.5 adds bounded reference targets and check/repeat occupations; basic point stakeout passed its final native/browser/build/flash and read-only hardware checkpoint. UI 0.6 subsequently adds manual line tags and completes the agreed ESP32 scope; A/B flashes and read-only hardware checks passed. Bulk imports, continuous topo, localization and full COGO remain deferred phone/app work. See [manual-line validation](tests/2026-09-11-manual-lines/README.md). See [current implementation state](docs/roadmap-6-12-progress.md); these changes do not establish field accuracy.


### 2026-09-11 web GUI redesign

UI 0.7 simplifies Jobs/Setup/Collect/Points with phone navigation, four expandable setup groups, a compact collection form and separate point tools. Live status shares the new light field palette. Full browser workflow/export checks, responsive and enlarged-text checks, A/B builds, hash-verified flashes and read-only hardware persistence checks passed. See [GUI design](docs/web-gui-design.md) and [evidence](tests/2026-09-11-gui-redesign/README.md). Real Android/field acceptance remains open.


### 2026-09-11 Rover control takeover

UI 0.8 removes the Rover web PIN: the latest accepted takeover becomes the only controller and invalidates the previous bearer. Base PIN protection remained at that checkpoint; the 2026-09-12 follow-up removes it so both roles use latest-request takeover. Both ESP32 application flashes are hash-verified; native/browser and real two-browser takeover checks passed with no survey records or receiver commands changed. The user confirmed that only the ESP32 boards are connected, so this is interface/storage validation, not GNSS/field qualification. See [validation](tests/2026-09-11-rover-takeover/README.md) and [control/Wi-Fi behavior](docs/survey-workflow.md).

### 2026-09-12 standalone link diagnostic checkpoint

Tablet-controlled ESP32 Wi-Fi/SiK diagnostics, latest-report persistence and electronics/field-test documentation are implemented. Both units flashed with hash verification. Wi-Fi and, after correcting reversed TX/RX, SiK each passed 117/117 synthetic packets in both directions at 1000 framed bytes/second for 30 seconds. UM980s were disconnected. SiK antennas were about 10 cm apart: no range or survey qualification is claimed. Continue with tablet-operated field tests and production SiK RTCM integration; BLE remains deferred. Implementation paused at 92% used of the five-hour allowance per the owner's limit. See tests/2026-09-12-transport-test-kit/README.md for evidence and remaining work.

### 2026-09-12 tablet workflow and takeover follow-up

Both Base and Rover now use latest-request takeover without a web PIN; the old Base PIN path and touchscreen prompts are removed. Both units flashed with hash verification and passed real two-browser ownership checks. Diagnostic UI now handles live/stale connection state, reload, browser disconnection and current-versus-saved report download; native and browser checks pass. A longer SiK run found losses (466/468 received at Base, 463/468 at Rover), so the earlier short pass does not qualify sustained delivery. The five-metre attempt never armed because the moved unit's web page was unavailable. The owner reset the setup to both units connected by USB at about 30 cm and requested bench completion followed by a stop before a six-metre move. See [evidence and next step](tests/2026-09-12-standalone-tablet/README.md). Production SiK RTCM and BLE remain pending.

Final bench checkpoint: both units updated and takeover/browser workflows verified; the 30 cm, two-minute SiK run received 467/468 at Base and 465/468 at Rover and did not pass RF acceptance. Reports saved/downloaded; no tests remain armed or running. Work stopped for the owner to move an independently powered unit six metres away.

Six-metre follow-up (run 913205): both ends received 465/468 packets during the same two-minute bidirectional SiK test, so increased separation did not eliminate loss. Saved browser downloads and independent instrument execution passed; RF delivery remains unqualified. No test remains armed/running. See tests/2026-09-12-standalone-tablet/sik-913205/README.md. Work paused at 3% remaining of the five-hour allowance after documenting this result.

## Loss-tolerant transport plan

The approved [transport development plan](docs/loss-tolerant-transport.md) separates fresh RTCM delivery, acknowledged duplicate-safe commands and periodic status. Start with a fixed-memory core and on-instrument fault self-tests, then paired synthetic SiK transport, then real UM980 COM2/RTK validation. Keep both UM980s disconnected through the synthetic stages.

Stage 1 is now implemented and flashed to both ESP32s: all 22 local transport fault checks pass, saved reports survive restart, and earlier RF results remain separate. Use `/diagnostics` → **Run local fault checks**. The core uses fixed memory; its full ESP32 test workspace is 4744 bytes. Next is paired synthetic SiK integration; no real correction forwarding is connected and UM980s remain disconnected. Evidence: `tests/2026-09-12-correction-transport/README.md`.
