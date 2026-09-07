# User Interface

> Status: Living UI rules and prototype behavior. The long-term direction remains an offline rover-hosted web interface over local Wi-Fi.

## Users and Field Workflow

Describe job setup, base setup, rover connection, point collection, stakeout, checks, and export.

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
- From the main screen, a downward swipe from the top opens GPS details and an upward swipe from the bottom opens Wi-Fi details. The opposite swipe returns to the main screen.
- Gesture instructions are not permanently displayed. Navigation must remain discoverable through documentation and initial onboarding rather than consuming field-status space.

### Central status definitions

The firmware must implement these definitions once and reuse them for banner color, text, warnings, logging, and later mobile clients:

| State | Prototype definition |
|---|---|
| `GPS FIXED` | Rover: GGA quality 4 (`RTK FIXED`). Base: GGA quality 7 (`BASE LOCKED`). |
| `CONNECTED` | A valid peer packet has been received within 3 seconds and the relevant Wi-Fi link is still active. |
| `READY` | UM980 UART and identity are current, the automatic profile is applied, `CONNECTED` is true, `GPS FIXED` is true, and a rover has corrections no older than 3 seconds. |

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

The archived Waveshare schematic shows an active-high GPIO6 backlight control and no ambient-light sensor. The prototype therefore uses PWM on GPIO6 with a time-based profile: full daylight brightness, reduced night brightness, and gradual 05:00-07:00 and 18:00-20:00 local transitions. The transition curve is intentionally biased brighter during twilight. Uncertain or stale time always selects full brightness.

Manual USB-console overrides remain available as `brightness day`, `brightness night`, and `brightness auto`. A later field settings screen may expose the same choices, but it must retain the safe full-brightness fallback.

## Automatic Instrument Setup

The prototype requires no touch action to configure the UM980. After the ESP32 receives both a valid UM980 identity response and fresh GGA data, it automatically applies the assigned volatile profile and reapplies it after a detected receiver-link loss.

| Instrument | Automatic UM980 profile | Persistence |
|---|---|---|
| Unit A | `MODE BASE`; RTCM 1006/1033/1074/1084/1094/1124 on COM2; `GPGGA`, `GPRMC`, and `BESTNAVA`; `MODE` verification | Volatile; reapplied by ESP32; no `SAVECONFIG` |
| Unit B | `MODE ROVER SURVEY`; `GPGGA`, `GPRMC`, and `BESTNAVA`; `MODE` verification | Volatile; reapplied by ESP32; no `SAVECONFIG` |

The allowlisted USB console remains as a recovery and diagnostic interface. Arbitrary receiver-command passthrough and `SAVECONFIG` remain prohibited. Instrument identity and Wi-Fi role are still fixed in this prototype: A is the access point/RTCM sender and B is the station/RTCM receiver.

## Data Model

Document jobs, points, codes, antenna height, quality metadata, base identity, coordinate configuration, and units.

## Offline and Failure Behavior

Define reconnection, unsaved-data protection, controller restart, radio loss, GNSS loss, low battery, and storage failure behavior.

## Validated Devices

| Device/browser | Version | Workflow tested | Result | Test record |
|---|---|---|---|---|
