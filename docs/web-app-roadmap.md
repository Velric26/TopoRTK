# Surveying web/app priorities

Updated 2026-09-11. This is the proposed development order for TopoRTK's offline GNSS surveying interface. Importance reflects correctness, protection of field records, everyday use and dependencies. It is our project ranking, not a vendor ranking. Items with hardware or coordinate-model dependencies must not be presented as working before validation.

The existing Rover status page and password-protected phone Wi-Fi provide the starting point. The user reported that the initial Android connection worked. That does not yet establish all phone/tablet models, recovery scenarios or field performance. The user then requested **ranks 2–5 together** and selected **UTM**. Their initial implementation is now UI 0.3; see [scope, limitations and validation](survey-workflow.md). Zapopan starter values (WGS84 UTM 13N) are convenience defaults and still require explicit confirmation. UI 0.6 completes the agreed instrument subsets: rank 6 review/plot, rank 7 export/backup and small targets, rank 8 checks/repeats, rank 9 basic stakeout, and rank 10 manual line tags. The deferred phone/app scope through rank 12 remains outstanding. See [features 6–12 checkpoint](roadmap-6-12-progress.md).

## Responsibility review

The [ESP32/Android responsibility decision](esp32-android-feature-split.md) sets the current implementation scope. Keep bounded target records, check/repeat occupations, basic point stakeout and manual line tags on the instrument. Defer bulk imports/restore, continuous collection, localization, full COGO and rich project processing to the phone/app. Web JavaScript already runs on the phone, so an APK is not required for computation alone.

## Ranked backlog

| Rank | Priority | Feature | Initial scope and why it belongs here |
|---|---|---|---|
| 1 | Essential foundation | Instrument status and connection recovery | Keep the existing fix/link/uncertainty view. Add correction age, base identity, horizontal/vertical quality, logging/SD health and battery warnings as validated telemetry becomes available. Clearly distinguish technical RTK readiness from readiness to save a survey point. Never retain an old green state after a disconnect. Basic status and local Wi-Fi are implemented. |
| 2 | Essential foundation — initial scope implemented | Jobs, durable records and controlled writes | Create/open/resume a job on Rover SD; show job name, units and configuration completeness. Add acknowledged commands, request IDs that prevent duplicate actions after retry, a single active editing controller, and a durable audit trail. A point is saved only after the Rover confirms its record is durable. UI 0.3 implements the bounded prototype; physical failure validation remains open. |
| 3 | Essential before measurements | Coordinate and height setup | Select the actual project CRS: datum/reference frame, epoch where applicable, UTM zone/hemisphere or approved local grid, units, geoid/vertical datum and grid/ground treatment. Label ellipsoidal and orthometric heights separately. Confirm against independent known examples. Preserve the configuration revision with each observation. Never silently reinterpret existing points after settings change. |
| 4 | Essential before measurements | Base setup, rover setup and antenna heights | Known-coordinate base, verified prior setup and explicitly marked temporary survey-in; base identity and correction source. Enter base and rover antenna model/reference point, measured height and vertical/slant method. Require confirmation for role/base-coordinate changes, and reset observation readiness afterward. Begin with the current correction transport; SiK settings depend on hardware validation. |
| 5 | Core field workflow | Quality-controlled point collection | Point number, code, description, pole height, occupation duration/sample count, horizontal/vertical uncertainty and correction-age limits. Require the selected fix state and configuration completeness throughout an occupation. Show progress and why collection is blocked; cancel or restart after quality loss. Retain raw observed position, derived ground/grid coordinates and all quality metadata. Start with deliberate stationary topo points, then longer control occupations. |
| 6 | Core field workflow | Point list, review and basic offline plot | Find a point by ID/code; inspect coordinates, time, base, antenna height and quality. Provide duplicate-ID handling, notes and audited edits/soft deletion. Add a simple local plan view of points and rover position. Satellite imagery and online basemaps are optional enhancements. |
| 7 | Core field workflow | Import, export and backup | CSV import preview with column/order/unit/CRS validation, clear rejected-row reporting and duplicate-ID policy. Export point CSV plus job/configuration/quality metadata; download a complete job backup from Rover SD. Re-import and round-trip comparison are acceptance checks. Add DXF and other formats once the required office workflow is confirmed. |
| 8 | Required quality control | Check points and repeated occupations | Compare measured points with independent control, report horizontal/vertical deltas and pass/fail against job tolerances. Preserve independent repeat occupations rather than averaging them invisibly. Provide start/end-of-job checks and an exportable quality report. Needed before treating collected coordinates as verified survey results. |
| 9 | Core field workflow | Point stakeout | Select/import a design point; show distance, north/east offsets, elevation difference/cut-fill and tolerance. Record measured as-staked position, design point and residuals. Keep guidance valid at low speed: use coordinate offsets unless heading/orientation is independently available. Preserve the original design coordinates. |
| 10 | Everyday productivity | Feature codes, linework and continuous topo | Code library, attributes, start/continue/end line, points along boundaries or utilities, and interval-by-time/distance collection. Respect the same quality limits and explicitly mark gaps after a lost fix. Essential for efficient topography after manual collection is trustworthy. |
| 11 | Project-dependent | Site localization and grid/ground tools | Fit a site transformation from approved control, display residuals, allow control exclusion with reasons, keep hold-out check points and version the result. Promote ahead of collection/stakeout when an actual job uses a local site grid; otherwise defer until the first standard CRS workflow passes. |
| 12 | Productivity | COGO and offsets | Inverse distance/bearing, area/perimeter, intersections, offset points and line extensions. Identify grid versus ground distances, calculation inputs and derived points. Useful for common field calculations but depends on correct coordinates and point management. |
| 13 | Reliability and expanded connectivity | Diagnostics and correction-source management | Readable event history, satellite/RTCM diagnostics, configuration backup/restore, recovery steps and safe firmware maintenance. Add SiK configuration after radio validation and NTRIP only when an Internet-based correction workflow is requested. Basic power/storage/correction warnings remain rank 1; they are not deferred here. |
| 14 | Advanced workflows | Line/arc/alignment/surface stakeout | Station/offset, slopes, cut/fill against surfaces and LandXML/design import. Develop only when an identified construction or engineering job requires them. Reuse validated point stakeout and coordinate handling. |
| 15 | Control and post-processing | Raw observations and PPK/RINEX workflow | Start/stop observation logging, storage/time/event status, receiver metadata and downloads for independent processing. Validate that exported data can actually be processed. Promote this earlier if establishing base control becomes the project's immediate bottleneck. Existing diagnostic CSV logging is not a raw-observation archive. |
| 16 | Hardware-dependent enhancement | Pole orientation, electronic level and tilt compensation | A validated level indication can help field use; compensated coordinates require separate IMU alignment, calibration and metrology validation. The planned BNO085 experiment does not justify displaying tilt-corrected survey points yet. |
| 17 | Optional delivery/convenience | Cloud sync, online maps, APK and team services | Add only for a demonstrated need. Keep jobs usable locally and never make a cloud account or Internet service mandatory. A browser shortcut precedes APK packaging; native code is justified by specific file, USB/BLE or background-operation needs. |

Ranks 2–8 form the first usable collection workflow. Rank 9 completes the initial collection-and-stakeout release. Later ranking can change with a real job; localization, raw logging and surface work are especially dependent on the task.

## Current checkpoint and remaining acceptance

**UI 0.3 implements the initial scope of ranks 2–5 at the user's request.** The original recommendation was to deliver the job foundation alone; the explicit four-feature request sets this checkpoint's scope. Supported coordinates are WGS84 standard-zone UTM/geographic only, with no datum transform and no installed geoid grids. The following original acceptance criteria remain the basis for hardware/field qualification; implemented code is not proof of field accuracy.

Deliver a Jobs screen that creates a named job, lists jobs, opens/resumes one and displays its configuration checklist. New jobs may show a clearly labeled Zapopan starter profile (WGS84 / UTM 13N, metres, ellipsoidal height and installed HA-609 model) as editable convenience values, but must not inherit an assumed datum confirmation, geoid, pole height or trustworthy base coordinate. Store a versioned manifest and audit log on Rover SD. Keep the existing read-only status accessible while jobs are used.

Before enabling writes, define controller authorization and ownership: pairing/session handling, one active writer, read-only access for additional devices, explicit command outcomes and bounded execution. Network membership alone should not silently grant every connected browser the right to alter base coordinates. Receiver commands remain a typed allowlist. Settings that can invalidate measurements need a concrete confirmation and audit record.

Acceptance for that checkpoint:

1. Create/open a job, close Chrome and reconnect; the same saved job reopens.
2. Restart the instrument; committed job data and active-job selection recover.
3. Retry a timed-out request and double-tap Create; exactly one job is created.
4. Remove/fill/fail the SD card during a write; no false success and no destruction of an existing job.
5. Use phone and tablet together; controller ownership prevents conflicting edits.
6. Validate job names, sizes and paths; imported or user-entered text cannot escape job storage or become executable UI content.
7. Keep Base traffic, GNSS parsing and live status responsive during all operations.
8. Reopen every committed manifest after an interrupted write; recover or clearly quarantine incomplete data with an actionable message.

The agreed instrument scope through rank 10 now passes native/browser tests and read-only two-instrument bench checks in UI 0.6. Next are field qualification and the deferred phone/app backlog: bulk file workflows, rich linework/continuous collection, localization (11) and COGO (12). The complete target remains **create job → configure → observe point → inspect → export → independently check**. Complete the current hardware/field acceptance before treating measurements as validated survey results.

## Basis in established survey software

Trimble documents topo, observed-control and rapid points, plus coded measurement, continuous topo and check points. Those workflows support the emphasis on deliberate point collection and quality control before automation. [Trimble Access: GNSS measurement methods](https://help.fieldsystems.trimble.com/trimble-access/latest/en/gnss-measure-methods.htm).

Base/rover antenna-height handling is a separate setup concern in commercial systems, including the distinction between the ground mark, antenna reference point and model offset. TopoRTK needs its own verified antenna model and receiver-coordinate interpretation; Emlid-specific offsets must not be copied. [Emlid: antenna height for RTK](https://docs.emlid.com/reachrs4/rtk-quickstart/antenna-height-rtk/).

Common stakeout workflows extend from points to lines, arcs, alignments and surfaces. Trimble also warns that changing coordinates after stakeout creates inconsistent results; configuration revision and design preservation therefore precede advanced stakeout. [Trimble Access: stakeout](https://help.fieldsystems.trimble.com/trimble-access/latest/en/stakeout.htm). Recording the design/staked pairs and residuals is a practical reporting pattern. [Emlid: stakeout reports](https://docs.emlid.com/emlid-flow/survey-with-ef/points/stakeout-report/).

Emlid's published field-software feature set includes survey projects, coordinate systems, collection/stakeout, import/export and more advanced mapping/design tools. This supports a focused first release followed by task-specific expansion. [Emlid Flow features](https://emlid.com/emlid-flow/).

The priorities, architecture choices, acceptance criteria and TopoRTK-specific limitations above are our recommendations, informed by those workflows and the current hardware. They are not a claim that the prototype already meets commercial equipment specifications.


## UI 0.7 interface refresh

The [web GUI redesign](web-gui-design.md) reorganizes existing workflows for phones and larger screens. It adds no new surveying feature or coordinate model. Both units run the verified UI 0.7 application; the previously documented phone/app deferrals and field acceptance requirements remain.
