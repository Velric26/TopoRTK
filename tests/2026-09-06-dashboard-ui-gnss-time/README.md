# Dashboard UI and GNSS-Time Checkpoint

> Status: Firmware and transport validated on both units; Unit A layout and flicker improvement owner-confirmed; Unit B field display confirmation remains.

## Scope

- Replace the dense single dashboard with main, GPS-details, and Wi-Fi-details screens.
- Centralize `READY`, `CONNECTED`, and role-aware `GPS FIXED` logic.
- Remove permanent swipe instructions and use consistent main-card typography.
- Add checksum-validated `GPRMC` UTC date/time and fixed `UTC-6` local time.
- Add gradual GNSS-time-based PWM brightness with safe full-brightness fallback and USB-console overrides.

## Bench configuration

| Item | Value |
|---|---|
| Date | 2026-09-06 |
| Validation target | Unit B for the first increment; Unit A for the changed-region rendering increment |
| ESP32 USB port | COM10 (B), COM4 (A) |
| Build environment | `unit_b`, then `unit_a` |
| Display rotation | `0` |
| UM980 interface | TTL Channel 2, 115200 bit/s |
| Unit A | Flashed after the requested flicker reduction and equal-height card change |

## Evidence observed

- The first Unit B build passed at 47,796 bytes RAM and 790,393 bytes flash. Both final changed-region builds later passed.
- Upload to the ESP32-S3 on COM10 completed and flash hash verification passed.
- The connected UM980 continued producing CRC-valid `BESTNAVA`, `GNRMC`, and `GNGGA` records once per second.
- RMC supplied the plausible UTC date/time `2026-09-07 02:55:21`; the `time?` console read back local `2026-09-06 20:55:21 UTC-6`. RMC navigation state was separately reported as invalid because the antenna had no position fix indoors.
- `brightness night` and `brightness auto` were accepted on Unit B; the unit was returned to automatic mode.
- The visible flicker cause was repeated filling and repainting of every UI region at 4 Hz. The revised renderer caches each region and skips drawing while its content and color are unchanged.
- Both revised builds passed. Unit A used 51,004 bytes RAM and 792,065 bytes flash; its COM4 upload and hash verification passed.
- After Unit A restarted, it identified as `MODE BASE`, received Unit B as one Wi-Fi client with a valid peer, and forwarded increasing CRC-valid RTCM counts with zero RTCM errors in the observed interval.
- The owner confirmed that Unit A's layout and flicker reduction looked correct.
- The final changed-region build was then flashed to Unit B on COM10; upload and flash hash verification passed.
- Unit B rejoined Unit A at approximately -45 to -49 dBm. Receive counts increased through at least 74 with zero sequence gaps and zero invalid packets while monitored indoors.

## Remaining field validation

- Repeat the visual checks on Unit B and under daylight/open-sky conditions.
- Confirm the banner is grey while no full RTK fix or application-level peer link exists.
- Confirm permanent swipe instructions are absent.
- Confirm downward-from-top opens GPS details and upward returns.
- Confirm upward-from-bottom opens Wi-Fi details and downward returns.
- Confirm local time is UTC minus six hours and UTC/date appear on GPS details.
- Confirm the four main information cards have equal height and spacing.
- Confirm the top banner does not show the literal `UTC-6` text.
- Confirm steady fields no longer flicker; individual changing values may update without repainting the rest of the screen.
- Confirm no text clips or overlaps at rotation `0`.
