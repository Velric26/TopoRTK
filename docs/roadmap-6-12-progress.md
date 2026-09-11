# Features 6–12 implementation checkpoint

User request: continue through feature 12. **Pause implementation at 90% used in the five-hour Codex window**, document the exact state, commit and push. Weekly usage is not the stop condition. Do not consume usage-reset credits automatically.

Branch: `codex/survey-roadmap-through-12`; baseline `ccda366` includes the user-approved Zapopan starter setup. Those convenience values remain intact and require measurement/reference confirmation.

## Historical 2026-09-10 pause checkpoint

Implementation paused on 2026-09-10 when the **300-minute window reached 90% used (10% remaining)**. The weekly window was not used for this decision. No new feature work after this threshold; only final verification, documentation, commit and push. Resume with feature 7 CSV import, then ranks 8–12.

## Current state

| Feature | State | Scope / remaining work |
|---|---|---|
| 6 — point review and offline plot | Implemented; host/browser and A/B read-only hardware checkpoint passed | Paginated point index, ID/code search, original observation/setup inspection, metadata notes and audited soft deletion/restoration, revision conflicts, current-setup north-up plot and quality-approved live Rover marker. |
| 7 — import/export/backup | Export/backup implemented; host/browser and A/B hardware checkpoint passed | CSV export with full original observation JSON and a checksummed complete job journal. CSV import/mapping/duplicate policy and an operator restore workflow remain. |
| 8 — checks and repeat occupations | Instrument core implemented and bench checked | Frozen control/repeat reference, phase, explicit tolerances, residuals/pass-fail, separate observations and CSV report. Advanced analysis deferred to phone/app. |
| 9 — point stakeout | Basic instrument/web core implemented and bench checked | Fresh N/E/height guidance and preserved design/as-staked result. Large designs/surfaces deferred. |
| 10 — codes/linework/continuous topo | Manual line core implemented and bench checked; automation deferred | Bounded start/continue/end tags, continuity and permanent break handling, paged line status and CSV/backup provenance. Library/continuous-session work remains phone/app scope. |
| 11 — localization/grid-ground | Deferred to phone/app | Approved control, residuals/exclusions/holdout and model lifecycle. No firmware transform added. |
| 12 — COGO/offsets | Deferred to phone/app | Browser-first computation can precede Android; no firmware geometry engine added. |

## Feature 6 architecture

The original journal remains authoritative and backward compatible. Recovery builds a small point index containing the source record number and current metadata. Original coordinates, quality and configuration are never overwritten. `point.edit` journals the before/after metadata, reason and expected metadata revision; deleted IDs remain reserved. Reads of a corrupt point disable writes.

`GET /api/v1/data` supports `view=points` (25 rows per page) and `view=point`. A journal-sequence `at` cursor rejects mixed snapshots when another command changes the data. Data reads run on the survey worker, under the shared SD mutex, through a bounded read request/response slot; HTTP never touches the card directly. Responses are bounded to 64 KiB and read waits to two seconds. This is separate from the small frequently polled status snapshot.

The `/survey-tools.js` asset extends the existing offline interface. User-entered metadata is rendered as text, not executable markup. The plan uses only the active setup revision to avoid mixing coordinate systems/units; other revisions remain available in the list. The live marker disappears if current quality checks or connection freshness fail.

Validation artifacts: `tests/2026-09-10-roadmap-6-12`. Synthetic native/browser observations are fixtures; no fabricated observations are written to either physical instrument.


## Feature 7 export checkpoint

CSV includes all points (including deleted records), per-row units/reference/revision, and full original observation/configuration/quality JSON. Formula-like metadata columns are prefixed with an apostrophe for spreadsheet handling; exact values remain in `observation_json`. Mixed revisions are explicitly retained per row, not silently converted.

`view=backup` pages up to three matching job records and scans at most 32 journal slots per request. It returns the exact JSON strings plus original sequence and CRC32. Stable `at` cursors cover all pages. Backups retain metadata edit reasons/before values and original observations. An offline verifier is available at `firmware/um980-display-demo/tools/verify_survey_backup.py`; it does not restore or modify instrument data.

Validation details and limits: [checkpoint record](../tests/2026-09-10-roadmap-6-12/README.md). Both instruments ran UI 0.4 at that historical export checkpoint. Unit A's two existing jobs remain intact; no field point or synthetic point was created on hardware.

## Previous resume order (superseded by the 2026-09-11 scope review)

1. Finish feature 7 CSV import: bounded file/row limits, explicit column mapping and preview, source UTM zone/hemisphere/frame/epoch/units/height reference confirmation, row validation, duplicate skip/reject policy and immutable imported/design provenance. Use paired typed commands and expected job revision; retain request IDs across uncertain writes. Never overwrite measured points or fabricate RTK quality for imported coordinates. Add CSV import/export round-trip and invalid-source tests, then flash/verify before feature 8.
2. Define and validate an instrument restore workflow before presenting restore controls. Current downloads and the offline verifier do not constitute a restore UI.
3. Implement ranks 8 through 12 in roadmap order with individual hardware checkpoints. Each requires explicit coordinate-space compatibility and original observation preservation.

Existing limits remain 16 jobs, 512 durable command receipts, 1024 journal records, 4096-byte commands and 8192-byte records. Point imports will need an explicit point count cap and a deliberate batch/index layout; do not assume one point per record once imports exist. The active job's starter values remain WGS84 UTM 13N/metres/ellipsoid, with confirmations and measured antenna heights required.


## 2026-09-11 resumed scope

Follow the [ESP32/Android split](esp32-android-feature-split.md). Implement bounded targets, check/repeat occupations, point stakeout and manual line tags; defer the listed phone/app responsibilities. Five-hour allowance began this resumed turn at 1% used. The 90% stop condition remains active.


## UI 0.5 instrument checkpoint

- Feature 7 instrument subset: 64 bounded immutable control/design targets, manual source/reference confirmation, revision and duplicate checks, restart/dedup recovery. Bulk imports and guided restore remain deferred to the phone/app.
- Feature 8 instrument core: independent-control and repeat occupations, frozen references, start/intermediate/end labels, explicit tolerances, stored signed residuals and pass/fail, readable CSV report columns. Native/browser and A/B read-only physical checks passed. Independent source means operator-confirmed provenance; field independence/accuracy remains unqualified.
- Feature 9 instrument core: basic point stakeout/as-staked storage implemented; native/browser/export tests, A/B builds, hash-verified flashes and read-only hardware checks passed. Guidance uses phone-side differences from the existing quality-approved snapshot; it expires after a conservative 1.5-second age including transport time. No heading assumption or grid-to-ground conversion.
- Feature 10 manual line tags remain the next instrument implementation. Continuous collection/code-library/linework management is deferred per the scope review.
- Features 11 and 12 remain deferred phone/app work, not completed firmware features.

The residual tests exposed rounding in the original ArduinoJson number formatter. UI 0.5 journal/snapshot serialization retains 15 significant digits; exact backup JSON is unchanged. New receipts have precision-format version 2, while legacy receipt retries retain their original canonicalization. See [validation and limitations](../tests/2026-09-11-instrument-scope/README.md).


## 2026-09-11 pause and handoff

The five-hour meter was 89% used before final flashing and 92% at the next reading. Implementation paused when that reading crossed the requested 90% stop point. Only verification/documentation/commit/push followed. Manual line tags were not started. Both instruments now run the final UI 0.5 target/check/stakeout checkpoint, with the same pre-existing jobs and roles.

Resume with feature 10's bounded manual line ID + start/continue/end tags and continuity validation. Keep observation coordinates immutable; define how deletion and setup revision changes break lines. Do not enable continuous capture against the current 1024-record/512-receipt journal. Features explicitly deferred by the responsibility decision are not pending ESP32 implementation and must not be marked completed.


## UI 0.6: agreed instrument scope complete

The next resumed turn completed feature 10's manual line subset. Both instruments now run UI 0.6.0: native/browser/export tests, A/B builds, hash-verified application flashes and read-only hardware checks passed. Existing roles, jobs and journal counts were preserved. See the [manual-line contract, evidence and limitations](../tests/2026-09-11-manual-lines/README.md).

Lines are limited to 64 per instrument. Each vertex is a new quality-gated topo observation with immutable line ID/action/previous vertex/original code. Continuations require the same setup revision, feature code and base reference. Interrupted or failed accepted occupations, vertex deletion/code editing and setup changes break continuity; restoring metadata cannot reconnect a broken line. End does not imply polygon closure. No continuous-capture or connected-line drawing was introduced.

This completes the four ESP32 implementation steps selected by the responsibility review. Ranks 7 and 10 remain partial, and 11/12 remain deferred phone/app features. The next work is physical field qualification and a separately scoped phone/app implementation of the deferred features. The five-hour allowance had more than 10% remaining when implementation completed; the weekly allowance was not used as a stop condition.
