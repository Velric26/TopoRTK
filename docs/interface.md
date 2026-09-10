# User Interface

> Status: Living UI rules and prototype behavior. The long-term direction remains an offline rover-hosted web interface over local Wi-Fi.

## Users and Field Workflow

Describe job setup, base setup, rover connection, point collection, stakeout, checks, and export.

## Mobile Delivery and Connectivity Roadmap

### Chosen first path

The Rover serves a responsive browser interface to an Android tablet, phone, or development PC. During development, both instruments can join a known 2.4 GHz local router; the Rover's assigned local address is shown on the Link screen. For field use without a router, the Rover hosts a password-protected 2.4 GHz access point and serves the same interface. The link is local only; it must continue working without cellular service or Internet access. Android Chrome is the first supported client. The current device address, interface version, and connection instructions must be discoverable from the Rover display and a QR/on-device connection aid.

During development, local-router credentials are held only in the Git-ignored
`firmware/um980-display-demo/src/wifi_credentials.h`, created from its tracked
example. The production workflow must replace this with local device
provisioning; neither a password nor a reusable secret belongs in a commit,
log, export, or displayed QR code.

The development console offers a persisted `wifi local` / `wifi direct` toggle. `LOCAL ROUTER` means both units are stations on the configured router; `DIRECT LINK` restores the Base AP at `192.168.4.1` and Rover station workflow. Changing it is a transport action only: it must clear stale peer/correction state but never reconfigure the UM980 role.

### Completed checkpoint: read-only Rover status

The Rover now serves a read-only status view at `/` and `/ui/v1/`, reachable from a PC on the same local router (Unit A bench address: `http://192.168.100.20`; DHCP may change it). The four equal status cards show correction-link state, GNSS fix state, Wi-Fi quality, and horizontal uncertainty. Readiness and immediate warnings use the same firmware rules as the onboard dashboard. Connection/GNSS details are available in an expandable section. UI version 0.1.0 has no external assets, browser storage, configuration, point collection, SD downloads, authentication, or write endpoints.

`GET /api/v1/status` returns a complete snapshot. The page polls once per second with no overlapping requests and a 1.8-second timeout. A failed request or stale/frozen snapshot clears the green readiness state, old values, and clock, then retries automatically. Manual Refresh and browser reload also fetch a complete snapshot. The server rejects a main-loop snapshot older than 1.5 seconds with HTTP 503. Stale GGA data is labeled `GNSS STALE` on both main dashboards and cannot supply a usable uncertainty or ready state. Missing numeric values are JSON `null`, never an invented zero.

The PC/local-router checkpoint and controlled browser failure tests are recorded in the [2026-09-10 validation record](../tests/2026-09-10-rover-web-status/README.md). Phone-width desktop testing does not validate physical Android Wi-Fi behavior or field accuracy.

### Rover phone/tablet Wi-Fi

The Rover adds a WPA2 access point while retaining the station connection used for corrections, in both Local Router and Direct Link modes. **Link → Phone / Tablet** shows the per-instrument SSID, AP state, connected-device count, browser address and UI version. **Show key** reveals the saved password locally for 30 seconds; leaving the page hides it. **New key → Confirm** replaces it and disconnects phone clients without changing the receiver profile. Confirmation expires after 10 seconds or when leaving the page.

At the user's request, keys contain eight random digits separated by a period: `dddd.dddd`. `1234.5678` is a format example, not a shared default. The initial 16-character prototype key migrates once after the update. The key remains saved across transport/role changes and startup, and is never placed in the API, USB logs, QR data or SD exports.

The phone network normally uses `http://192.168.8.1`; a conflicting upstream subnet selects `http://172.22.42.1`. Use the address on the display. Android may report no Internet: choose **Stay connected** and open the explicit HTTP address in Chrome. There is no captive-portal dependency or Internet forwarding. The browser repeats these instructions and shows public phone-network metadata.

The user reported successful initial connection. Model/version and extended recovery coverage are tracked separately in [phone Wi-Fi validation](../tests/2026-09-10-rover-phone-wifi/README.md).

### Survey workflow checkpoint (UI 0.3)

The user requested the next four ranked features together. `/survey` now provides Jobs → Setup → Collect with PIN-based single-controller access, instrument-side storage, explicit WGS84 UTM/height/antenna/base configuration and quality-gated occupations. The Base serves its own paired receiver setup page. See [survey workflow, limits and validation](survey-workflow.md). The next ranked development item is full point review and an offline plot; field acceptance of this new workflow remains separate.

The tablet is an interaction surface, not the authoritative datastore. The Rover SD card retains jobs, point records, configuration changes, errors, and exports. A browser reconnect or close must not discard an already accepted rover-side record.

The SiK pair is the intended correction transport after its independent validation. Wi-Fi RTCM is a validated prototype/fallback path and must not be required for the tablet interface or final correction link.

### Interface contract

- Serve versioned static UI assets and a versioned local API from the Rover.
- Provide a bounded request/response command API for state-changing operations and a live status channel for display updates.
- Each command must return an explicit accepted, rejected, or completed state; destructive actions require a confirmation step and a logged audit record.
- The UI must clearly state when the tablet is connected to a local network without Internet. Do not rely on Android captive-portal behavior; show the local address and QR aid explicitly.
- Require an access-point password. Rotate or replace the initial credential before field deployment and do not expose secrets in exported logs.
- Support downloaded interoperable exports first. Browser-side persistence may cache drafts or presentation preferences, but it is never the only copy of survey data.

### Delivery stages

1. **Responsive browser UI:** status, jobs, point collection, stakeout, export, diagnostics, and recovery through Rover Wi-Fi.
2. **Browser shortcut:** provide a manifest/icon only if it improves launch ergonomics; do not promise full PWA offline behavior from a local HTTP device address.
3. **Android APK wrapper, only if justified:** package the same proven UI with a native WebView bridge when native Bluetooth LE, USB, files/share integration, background work, or controlled app deployment solves an observed field problem.
4. **Native Android features, only when necessary:** add Kotlin/Compose screens or native services for requirements that cannot be safely met by the shared web UI and bridge.

Bluetooth LE is reserved for later provisioning, recovery, or compact diagnostics. It is not the primary survey workflow or bulk log path because it adds Android permission, discovery, reconnection, and protocol complexity.

## Required Screens

- Connection and instrument status
- Base setup
- Job and coordinate-system setup
- Point collection and review
- Stakeout
- Export and logs
- Diagnostics and recovery

## Required Live Status

Define presentation and warning rules for RTK state, precision, correction age, satellites, baseline, radio, battery, and logging.

The prototype uses role-aware base labels: `BASE WAIT` without a fix, `BASE SURVEY` while obtaining the temporary coordinate, and `BASE LOCKED` when GGA quality 7 indicates that the coordinate is being held. Raw GGA quality remains available in USB diagnostics.

`H-ACC` is the receiver-estimated horizontal position uncertainty in 1DRMS. It is calculated as `sqrt(latitude_sigma^2 + longitude_sigma^2)` from CRC-validated UM980 `BESTNAVA` records and automatically formatted in millimetres, centimetres, or metres. It shows `---` without a usable fix and `N/A (BASE)` after the autonomous base coordinate is locked. It is an estimator, not a guarantee that the true position lies inside that radius.

## Field Dashboard Rules

These rules apply to the onboard display and should carry forward to the phone/tablet UI unless a documented field test justifies a change.

### Screen hierarchy

- The main screen is a glanceable status overview: correction link, required GNSS fix, link quality, horizontal uncertainty, and one immediately actionable warning.
- Detailed coordinates, satellites, GNSS time, quality metrics, and correction diagnostics belong on the GPS details screen.
- SSID, IP address, RSSI, packet counters, and network diagnostics belong on the Wi-Fi details screen.
- A permanent 48-pixel bottom navigation bar exposes `HOME`, `GPS`, `LINK`, and `SETUP`. Each tab has an 80-pixel-wide touch target. Settings expose runtime role and brightness controls.
- From the main screen, a downward swipe from the top opens GPS details and an upward swipe from the bottom opens Wi-Fi details. The opposite swipe returns to the main screen.
- Gesture instructions are not permanently displayed. Navigation must remain discoverable through documentation and initial onboarding rather than consuming field-status space.

### Central status definitions

The firmware must implement these definitions once and reuse them for banner color, text, warnings, logging, and later mobile clients:

| State | Prototype definition |
|---|---|
| `GPS FIXED` | Rover: GGA quality 4 (`RTK FIXED`). Base: GGA quality 7 (`BASE LOCKED`). |
| `CONNECTED` | A valid peer packet has been received within 3 seconds and the relevant Wi-Fi link is still active. |
| `READY` | UM980 UART and identity are current, the automatic profile is applied, `CONNECTED` is true, `GPS FIXED` is true, and a rover has corrections no older than 3 seconds. |

Profiles are applied only after each receiver command is acknowledged and a checksum-valid `MODE` response matches the selected role. Required fix checks also require GGA no older than three seconds. A base with RTCM output disabled is not ready.

The main banner is green only for `READY`; otherwise it is grey. The GPS-details banner is green only for `GPS FIXED`. The Wi-Fi-details banner is green only for `CONNECTED`. Every state is also written in text, so color is never the sole indication.

### Outdoor readability

- Use a near-black background, high-contrast white or bright status text, solid grey/green banners, and the built-in bitmap font at readable integer sizes.
- Equivalent labels and status values use consistent size and weight. Larger text is reserved for a genuinely more urgent function, not decoration.
- Equivalent information cards on the same screen use the same height and spacing. A larger section requires a documented functional need.
- The header identifies the runtime role as `Rover` or `Base`; do not use a generic `STATUS` title.
- Preserve explicit terms such as `NO LINK`, `NO FIX`, `RTK FLOAT`, and `RTK FIXED` even when banner color conveys the same state.
- Prefer fewer large, stable values over dense diagnostics on the main screen.
- Redraw only regions whose text, color, or state changed. Do not continuously repaint the complete header, cards, or screen; full-screen refreshes are reserved for initialization and page changes.

## GNSS Time and Display Brightness

The UM980 is configured to emit `GPRMC` once per second on COM2. The ESP32 validates the NMEA checksum and extracts UTC date/time. The RMC navigation-valid flag is recorded separately: it may be `V` indoors while the receiver still supplies a plausible GNSS-derived clock. Time is unavailable when its fields are missing, malformed, checksum-invalid, or stale.

- The main display shows local civil time using the project-requested fixed `UTC-6` offset.
- GPS details retain UTC time and UTC date for diagnostics.
- Fixed `UTC-6` is a prototype rule, not a complete time-zone engine; revisit it before supporting regions with another offset or daylight-saving rules.
- If GNSS time is unavailable, show `TIME WAIT` and keep the backlight at its daylight maximum.

The archived Waveshare schematic and the manufacturer example use active-high GPIO6 backlight PWM at 5 kHz. There is no ambient-light sensor. The prototype therefore uses PWM on GPIO6 with a time-based profile: full daylight brightness, about 10% night brightness, and gradual 05:00-07:00 and 18:00-20:00 local transitions. The transition curve is intentionally biased brighter during twilight. Uncertain or stale GNSS time always selects full brightness and the Setup screen reports `AUTO: NO GNSS TIME`.

The Setup screen and USB console expose the same saved `Auto`, `Day`, and `Night` choices. Automatic mode retains the full-brightness fallback when time is uncertain.

## Touch Setup and Saved Configuration

Open `SETUP`, tap `BASE` or `ROVER`, then tap `USE BASE` or `USE ROVER`. The first tap only selects a candidate; leaving Setup discards an unapplied selection. Applying saves the selection, changes the Wi-Fi role, clears old peer/correction/fix state, and configures the UM980. The A/B badge remains a physical instrument identifier. The other instrument must have the opposite role; the firmware does not change the peer remotely.

| Selected role | Automatic UM980 profile | Wi-Fi role |
|---|---|---|
| Base on either unit | `MODE BASE`; RTCM 1006/1033/1074/1084/1094/1124 on COM2 unless disabled; `GPGGA`, `GPRMC`, and `BESTNAVA`; `MODE` verification | Access point / correction sender |
| Rover on either unit | `MODE ROVER SURVEY`; `GPGGA`, `GPRMC`, and `BESTNAVA`; `MODE` verification | Station / correction receiver |

The profile begins with `UNLOG COM2` so rover mode cannot inherit base output streams. Commands are sent one at a time with acknowledgements, bounded retries, and a final role readback. The UI and UART remain serviced between commands. `APPLYING...` disables settings changes until completion; failures expose `RETRY SETUP`. Correction forwarding remains disabled until profile verification succeeds.

`AUTO`, `DAY`, and `NIGHT` apply and save brightness immediately. Role, brightness mode, and the base RTCM enable flag are stored together in one versioned ESP32 NVS value (`toportk/config`). Writes occur only when needed, and failed writes keep the previous active configuration with a visible error. The SD card is not required. Missing settings use A=Base, B=Rover, automatic brightness, and enabled base RTCM. Invalid settings fall back to these defaults and report an error.

At power-on, saved settings load before Wi-Fi and display setup. After the receiver handshake, the selected volatile profile is reapplied automatically, including after receiver-link loss. No startup touch is needed. The firmware never sends `SAVECONFIG` to the UM980. It remembers the operating role and display preferences, **not a surveyed base coordinate**: autonomous Base starts a new temporary survey-in after restart.

The USB commands `role rover`, `role base-test`, `brightness auto|day|night`, and `rtcm base-test|off` share the same saved configuration path. RTCM commands are restricted by the active Base role rather than unit identity and reapply the complete receiver profile. `config?` reports storage and receiver-profile status. Arbitrary receiver-command passthrough remains prohibited.

Touch uses single-finger release events, a movement threshold, and a maximum tap duration. Swipes, drags out and back, multi-touch, and I2C errors cannot apply a role. Buttons and hit-testing share the same rectangles. Portrait rotations `0` and `2` are supported; `2` transforms both touch axes.

The redesigned dashboard has four equal-height cards, larger values, an actionable two-line warning, and bounded text fitting. The detail pages reserve space for the navigation bar. Cached regions repaint only when their content changes.

See the [2026-09-08 validation record](../tests/2026-09-08-touch-role-settings/README.md) for the distinction between host checks, Unit B hardware checks, and outstanding physical touch/power-cycle tests.

## Data Model

Document jobs, points, codes, antenna height, quality metadata, base identity, coordinate configuration, and units.

## Offline and Failure Behavior

Define reconnection, unsaved-data protection, controller restart, radio loss, GNSS loss, low battery, and storage failure behavior.

## Validated Devices

| Device/browser | Version | Workflow tested | Result | Test record |
|---|---|---|---|---|
| Windows PC / isolated headless Microsoft Edge | 152.0.4191.66 | Live Rover status/reloads; responsive widths and controlled reconnect failures | PASS; physical Android pending | [2026-09-10](../tests/2026-09-10-rover-web-status/README.md) |
