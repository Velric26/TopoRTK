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

UI 0.8 removes the Rover web PIN: the latest accepted takeover becomes the only controller and invalidates the previous bearer. Base PIN protection remains. Both ESP32 application flashes are hash-verified; native/browser and real two-browser takeover checks passed with no survey records or receiver commands changed. The user confirmed that only the ESP32 boards are connected, so this is interface/storage validation, not GNSS/field qualification. See [validation](tests/2026-09-11-rover-takeover/README.md) and [control/Wi-Fi behavior](docs/survey-workflow.md).

### 2026-09-12 standalone link diagnostic checkpoint

Tablet-controlled ESP32 Wi-Fi/SiK diagnostics, latest-report persistence and electronics/field-test documentation are implemented. Both units flashed with hash verification. Wi-Fi and, after correcting reversed TX/RX, SiK each passed 117/117 synthetic packets in both directions at 1000 framed bytes/second for 30 seconds. UM980s were disconnected. SiK antennas were about 10 cm apart: no range or survey qualification is claimed. Continue with tablet-operated field tests and production SiK RTCM integration; BLE remains deferred. Implementation paused at 92% used of the five-hour allowance per the owner's limit. See tests/2026-09-12-transport-test-kit/README.md for evidence and remaining work.
