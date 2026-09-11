# Features 6–12 implementation checkpoint

User request: continue through feature 12. **Pause implementation at 90% used in the five-hour Codex window**, document the exact state, commit and push. Weekly usage is not the stop condition. Do not consume usage-reset credits automatically.

Branch: `codex/survey-roadmap-through-12`; baseline `ccda366` includes the user-approved Zapopan starter setup. Those convenience values remain intact and require measurement/reference confirmation.

## Pause checkpoint

Implementation paused on 2026-09-10 when the **300-minute window reached 90% used (10% remaining)**. The weekly window was not used for this decision. No new feature work after this threshold; only final verification, documentation, commit and push. Resume with feature 7 CSV import, then ranks 8–12.

## Current state

| Feature | State | Scope / remaining work |
|---|---|---|
| 6 — point review and offline plot | Implemented; host/browser and A/B read-only hardware checkpoint passed | Paginated point index, ID/code search, original observation/setup inspection, metadata notes and audited soft deletion/restoration, revision conflicts, current-setup north-up plot and quality-approved live Rover marker. |
| 7 — import/export/backup | Export/backup implemented; host/browser and A/B hardware checkpoint passed | CSV export with full original observation JSON and a checksummed complete job journal. CSV import/mapping/duplicate policy and an operator restore workflow remain. |
| 8 — checks and repeat occupations | Not implemented yet | Independent control comparison, explicit tolerances and retained repeat observations/report. |
| 9 — point stakeout | Not implemented yet | Design selection, reliable N/E/height guidance and preserved as-staked result. |
| 10 — codes/linework/continuous topo | Not implemented yet | Code library, line actions, interval triggers and quality-loss gaps. |
| 11 — localization/grid-ground | Not implemented yet | Approved control, residuals/exclusions/holdout, versioned fit and coordinate-space safeguards. |
| 12 — COGO/offsets | Not implemented yet | Inverse, polygon area/perimeter, intersections, extensions/offsets and provenance. |

## Feature 6 architecture

The original journal remains authoritative and backward compatible. Recovery builds a small point index containing the source record number and current metadata. Original coordinates, quality and configuration are never overwritten. `point.edit` journals the before/after metadata, reason and expected metadata revision; deleted IDs remain reserved. Reads of a corrupt point disable writes.

`GET /api/v1/data` supports `view=points` (25 rows per page) and `view=point`. A journal-sequence `at` cursor rejects mixed snapshots when another command changes the data. Data reads run on the survey worker, under the shared SD mutex, through a bounded read request/response slot; HTTP never touches the card directly. Responses are bounded to 64 KiB and read waits to two seconds. This is separate from the small frequently polled status snapshot.

The `/survey-tools.js` asset extends the existing offline interface. User-entered metadata is rendered as text, not executable markup. The plan uses only the active setup revision to avoid mixing coordinate systems/units; other revisions remain available in the list. The live marker disappears if current quality checks or connection freshness fail.

Validation artifacts: `tests/2026-09-10-roadmap-6-12`. Synthetic native/browser observations are fixtures; no fabricated observations are written to either physical instrument.


## Feature 7 export checkpoint

CSV includes all points (including deleted records), per-row units/reference/revision, and full original observation/configuration/quality JSON. Formula-like metadata columns are prefixed with an apostrophe for spreadsheet handling; exact values remain in `observation_json`. Mixed revisions are explicitly retained per row, not silently converted.

`view=backup` pages up to three matching job records and scans at most 32 journal slots per request. It returns the exact JSON strings plus original sequence and CRC32. Stable `at` cursors cover all pages. Backups retain metadata edit reasons/before values and original observations. An offline verifier is available at `firmware/um980-display-demo/tools/verify_survey_backup.py`; it does not restore or modify instrument data.

Validation details and limits: [checkpoint record](../tests/2026-09-10-roadmap-6-12/README.md). Both instruments currently run this UI 0.4 export checkpoint. Unit A's two existing jobs remain intact; no field point or synthetic point was created on hardware.

## Resume order

1. Finish feature 7 CSV import: bounded file/row limits, explicit column mapping and preview, source UTM zone/hemisphere/frame/epoch/units/height reference confirmation, row validation, duplicate skip/reject policy and immutable imported/design provenance. Use paired typed commands and expected job revision; retain request IDs across uncertain writes. Never overwrite measured points or fabricate RTK quality for imported coordinates. Add CSV import/export round-trip and invalid-source tests, then flash/verify before feature 8.
2. Define and validate an instrument restore workflow before presenting restore controls. Current downloads and the offline verifier do not constitute a restore UI.
3. Implement ranks 8 through 12 in roadmap order with individual hardware checkpoints. Each requires explicit coordinate-space compatibility and original observation preservation.

Existing limits remain 16 jobs, 512 durable command receipts, 1024 journal records, 4096-byte commands and 8192-byte records. Point imports will need an explicit point count cap and a deliberate batch/index layout; do not assume one point per record once imports exist. The active job's starter values remain WGS84 UTM 13N/metres/ellipsoid, with confirmations and measured antenna heights required.
