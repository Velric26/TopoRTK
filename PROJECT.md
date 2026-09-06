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

| Qty. | Component | Role | Current status |
|---:|---|---|---|
| 2 | Unicore UM980 RTK GNSS modules | RTK engine; one per instrument | Both units passed USB and bidirectional TTL2; Unit A also passed standalone GPS; RTK pending |
| 1 pair | Holybro SiK Telemetry Radio, long-range 1 W, 915 MHz, open source | Base-to-rover RTCM transport | Selected; configuration, range, and legal use must be validated |
| 2 | Waveshare ESP32-S3 3.5-inch capacitive touch display boards, 320 x 480, Wi-Fi and Bluetooth 5 | Control, local UI, logging, and phone/tablet connectivity | Both displays and TTL2 links validated; per-unit A/B builds established; touch validated on Unit A |
| 2 | K700 full-band L1/L2/L5 BeiDou/GPS/GLONASS/Galileo survey GNSS antennas | Primary base and rover antennas | Validation on hold: purchased cable has the wrong antenna-side center-contact gender; exact connector must be verified before replacement |
| 2 | GNSS HA-609 helix antennas | Compact prototypes and comparison testing | Standalone battery-powered GPS fix validated on one unit; comparison pending |
| 2 | BNO085 IMUs | Orientation experiments and possible future pole-tilt work | Available; not accepted as survey tilt compensation |

The UM980 modules are mounted on BDRTK-980 carrier boards. The seller manual is archived under `docs/hardware/unicore-um980/`; the exact physical PCB revision, active-antenna supply behavior, USB-to-UART channel mapping, and full power budget still require bench verification.

## 5. System Architecture

```text
BASE                                             ROVER

K700 or HA-609                                  K700 or HA-609
GNSS antenna                                    GNSS antenna
      |                                               |
    UM980 -- RTCM --> Holybro SiK ))) 915 MHz ((( Holybro SiK --> UM980
      |                                               |
 Waveshare ESP32-S3                             Waveshare ESP32-S3
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

The initial design should route rover RTCM directly from the radio to a UM980 UART where practical. This keeps correction transport working if the ESP32-S3 interface restarts.

Base and rover should use the same enclosure and electronics layout where practical. Their role should be selectable in software so either instrument can serve as base or rover.

## 6. Initial Interface Direction

The preferred first implementation is an **offline, rover-hosted responsive web application** reached through local Wi-Fi. It avoids cellular service, internet access, app-store deployment, and device-specific installation.

A native or hybrid mobile app remains an option if browser limitations prevent a reliable field workflow.

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

| Link | Initial purpose |
|---|---|
| UM980 UART | Configuration, NMEA/proprietary status, RTCM, and raw observations |
| SiK 915 MHz | Base-to-rover RTCM correction stream |
| Wi-Fi | Offline local web UI and data transfer |
| Bluetooth 5 | Optional provisioning, diagnostics, or future native-app integration |

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

The final power system is not yet validated. Any proposed 3S 18650 design must include matched cells, reverse-insertion protection, undervoltage protection, a fuse near the battery, a physical power switch, regulated electronics power, clean GNSS power, locking connectors, and safe USB/back-feed behavior.

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

- [ ] Insert the SiK pair at low power on the bench.
- [ ] Compare transmitted and received correction streams.
- [ ] Measure latency, correction age, dropouts, recovery, and usable range.
- [ ] Test RF interference with Wi-Fi, display, storage, and power conversion active.

### Phase 3 — ESP32-S3 integration

- [ ] Implement transparent data paths before automated behavior.
- [ ] Add logging and diagnostics.
- [ ] Add local display functions one verified behavior at a time.

### Phase 4 — Mobile workflow

- [ ] Prototype the offline web interface.
- [ ] Implement status, configuration, projects, point collection, UTM, and export.
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
| Two UM980 receivers for interchangeable base/rover instruments | Both passed USB and bidirectional TTL2; Unit B is confirmed `MODE ROVER SURVEY`; Unit A passed an ESP32-relayed switch to temporary base mode, but its base position and RTCM remain unvalidated |
| K700 antennas as primary survey antennas | Selected; validation on hold until the correct cable is obtained after exact connector identification |
| HA-609 antennas for compact tests | Standalone GPS acquisition passed; controlled comparison still required |
| Holybro SiK 1 W 915 MHz correction link | Selected; throughput, range, interference, and compliance pending |
| Waveshare ESP32-S3 touch boards as controllers/displays | Selected; pinout, UART, storage, and outdoor tests pending |
| Rover-hosted offline web UI | Preferred first implementation; prototype pending |
| BNO085 orientation/tilt experiments | Deferred until the basic level-pole RTK system is validated |

The direct ESP-to-ESP Wi-Fi checkpoint passed on 2026-09-06 using Unit A as an access point and Unit B as a station. Checksummed, sequenced test packets reached Unit B with no detected gap or invalid packet during the short bench observation. A CRC-validated RTCM frame bridge was subsequently built and flashed to both units, and Unit A accepted the manufacturer-example COM2 MSM4 output commands. The antenna-less bench setup produced no RTCM frames, so live forwarding, sustained throughput, recovery, range, security, and RTK operation remain unvalidated.

Current temporary role assignment: Unit A = base-test; Unit B = confirmed rover. The unit letter identifies hardware, not a permanent role. Allowlisted runtime role switching through the ESP32 USB console is now proven without reflashing; touch/web selection and full survey-safe base configuration remain future work.

### Open items

- Exact UM980 carrier-board revision and complete electrical interface.
- Exact K700 connector family and required cable-end shell/contact genders; current cable does not mate with the antenna.
- Final RTCM message set, serial rate, radio air rate, and update frequency.
- Exact K700/HA-609 specifications, phase-center information, and active-antenna requirements.
- UTM zone, reference frame/epoch, geoid model, localization, codes, stakeout, and export requirements from an existing job.
- Final point-quality thresholds and observation durations.
- Phone/tablet browser compatibility and offline recovery behavior.
- BNO085 purpose and a defensible calibration method if tilt compensation is pursued.
- Final batteries, regulators, filtering, connectors, field controls, enclosure, and pole mount.
- Mexican homologation and permitted settings for the 915 MHz radio system.

## 13. Project Documentation

Create focused documents as details become real and testable:

```text
PROJECT.md                 Project scope and current system decisions
docs/hardware/             Hardware index plus one self-contained folder per device
docs/firmware.md           Build, flash, configuration, and firmware architecture
docs/gnss-rtcm.md          UM980 configuration and RTCM message set
docs/radio.md              SiK configuration, compliance, and range tests
docs/coordinates.md        UTM, datum/epoch, geoid, localization, and base control
docs/interface.md          Local and mobile UI behavior
docs/validation.md         Test procedures and acceptance criteria
docs/decisions.md          Short architecture decision records
tests/                     Test template and dated test records
```

Only create these files when their content is needed; avoid duplicating information between them.

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
