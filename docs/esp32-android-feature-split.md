# Instrument and Android responsibilities through feature 12

Decision date: 2026-09-11. Replaces the previous instruction to put every remaining feature into the instrument firmware. The user requested this review before implementation.

## Recommendation

Keep the ESP32 responsible for timely receiver/correction handling, quality gates, bounded commands and durable observations. Use the phone for large datasets, transformations, rich graphics, file management and reporting. The existing instrument-hosted web page already executes JavaScript on the phone: an APK is not needed merely to move calculations away from the ESP32. Native Android becomes useful for a maintained local project database, large file workflows, device integration and a separately installed offline client. Do not begin an APK in this checkpoint.

The ESP32-S3 has two LX7 cores up to 240 MHz and 512 KB on-chip SRAM ([Espressif datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)). This project's configuration uses 16 MB flash and external PSRAM; the last bench snapshot reported about 220 KB free heap and 8.26 MB free PSRAM. Those idle measurements are **not** a CPU/latency budget or evidence of performance on large jobs. Firmware currently limits jobs to 16, durable command receipts to 512 and journal records to 1024; commands are 4 KiB, records 8 KiB and paged reads 64 KiB. Capacity, SD latency and restart replay are immediate constraints alongside CPU time.

Browser scripts and optional Web Workers can process files/geometry on the phone ([MDN Web Workers](https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API)). The current web client should not promise background operation or continued access to instrument data after disconnect. Radio/GNSS processing and the authority to accept a measurement stay on the instrument.

## Review by feature

| Rank | Keep on ESP32 / implement now | Phone or Android scope to defer | Reason |
|---|---|---|---|
| 7 — import/export/backup | Existing paged export/backup; small immutable control/design targets entered through a typed, revision-checked command. Preserve source and coordinate reference; reserve IDs. | Bulk CSV/DXF/LandXML parsing, mapping/preview, duplicate resolution, project database, bulk synchronization and guided backup restoration. | Parsing and managing whole projects can consume journal capacity and block SD access. A phone can prepare bounded target commands later. Small manual target entry unblocks checks/stakeout now. |
| 8 — checks/repeats | Quality-gated independent check and repeat occupations, frozen reference coordinates, explicit horizontal/vertical tolerances, start/intermediate/end labels, signed residuals and pass/fail saved with each independent observation. | Multi-session statistics, plots, formatted check reports and adjustment analysis. | A few differences and a square root are cheap. Acceptance must be recorded alongside the actual observation. A passing repeat against a prior observation is repeatability, not independent control. |
| 9 — point stakeout | One selected target, fresh N/E/distance/height guidance in the current UTM grid, normal quality-gated as-staked occupation with design and residuals preserved. | Large design search/maps, surfaces, alignments, route planning, phone orientation integration and rich stakeout reports. | Constant-size point arithmetic is suitable; graphics/datasets and unreliable heading are the harder parts. No arrow based on an assumed heading. |
| 10 — codes/linework/continuous topo | Existing point code/description; bounded manual line ID and start/continue/end tags with continuity validation and immutable observation provenance. | Code-library/attribute-schema editor, connected line rendering/editing, continuous time/distance collection and its session management. | Tags are cheap. Continuous capture would rapidly exhaust the current journal/receipt limits and needs a redesigned streaming log plus physical load/power-failure validation. It is not deferred solely because of arithmetic cost. |
| 11 — localization/grid-ground | Preserve WGS84 observations and their setup revision. Reject unsupported coordinate spaces. No site transformation introduced now. | Approved-control management, fitting, robust exclusions, holdout tests, residual visualization, geoid grids and versioned site models. | These require careful geodesy, numerical validation and model lifecycle management. Later firmware could apply an approved fixed transform cheaply, but only with a versioned contract and independent checks. |
| 12 — COGO/offsets | Preserve source points and accept only explicitly identified derived results through a future typed interface. No geometry engine introduced now. | Inverse, areas/perimeters, intersections, extensions/offsets and derived-point workflow. Prefer browser-first computation where practical, then share with Android. | Basic COGO is not too hard for the ESP32; it simply has no reason to compete with acquisition or enlarge the trusted firmware. The phone can compute it and retain provenance. |

## Current implementation sequence

1. Small control/design target storage and manual web entry (feature 7 subset), with explicit current-reference confirmation, bounded counts and duplicate rejection; never label imported coordinates as measured RTK points.
2. Check/repeat occupations (feature 8 core), retaining separate observations and all quality gates.
3. Basic one-point stakeout and saved as-staked result (feature 9 core).
4. Manual line tags and continuity validation (feature 10 subset).

Each checkpoint needs native/browser tests, A/B builds, application flash verification and an honest hardware test record before the next behavior is added. Synthetic observations stay in test fixtures. Real field accuracy, control independence and measured-point operations need field qualification.

This is an intentional scope split: finishing the list above does not mean bulk import, continuous topo, localization or COGO are implemented. Track them as deferred phone/app work, not completed ranks. Pause implementation at 90% used in the five-hour allowance, document exact progress, commit and push; weekly usage is not this stop condition.
