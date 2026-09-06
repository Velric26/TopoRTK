# Test: HA-609 Battery-Powered Standalone GPS Fix

- **Date:** 2026-09-05
- **Purpose:** Validate the complete HA-609 antenna, UM980, TTL2 UART, ESP32 parser, display, and portable-power path without RTK corrections.
- **Firmware:** `firmware/um980-display-demo/`

## Configuration

| Item | Configuration |
|---|---|
| Antenna | GNSS HA-609 helix antenna |
| Receiver | UM980 on BDRTK-980 carrier |
| Controller/display | Waveshare ESP32-S3 Touch LCD 3.5 |
| Power source | USB power bank connected to Waveshare USB-C |
| UM980 power | Waveshare 5 V/VBUS to BDRTK `5V_IN` |
| Ground | BDRTK GND to Waveshare GND |
| UM980 to ESP32 | BDRTK `TTL_TXD2` to GPIO44/RX |
| ESP32 to UM980 | GPIO43/TX to BDRTK `TTL_RXD2` |
| BDRTK USB | Disconnected |
| Environment | Reported clear-sky field view |
| RTK corrections | None |

## Observed Result

| Check | Result | Evidence |
|---|---|---|
| Receiver identification | PASS | Display showed `UM980 OK` |
| UART data path | PASS | Display showed `UART RECEIVING` |
| Standalone position | PASS | Display showed `GPS FIX` with populated coordinates |
| Satellites | PASS | 13 shown |
| HDOP | PASS | 0.90 shown |
| GGA altitude | OBSERVED | 1627.096 m shown; not compared with a surveyed or geoid-corrected reference |
| Data continuity | PASS | GGA counter 72; line counter 75; 2732 bytes received |
| NMEA checksum handling | PASS | Checksum-error counter 0 |
| Time to first fix | NOT RECORDED | Not available from the supplied evidence |
| RTK status | NOT TESTED | No base corrections; `GPS FIX` is not an RTK fixed solution |

## Evidence and Privacy

The project owner supplied a display photograph showing the result. The original image was intentionally not copied into the repository because it contains precise latitude and longitude. Exact coordinates are unnecessary for this connectivity checkpoint and are omitted from this record.

## Conclusion

PASS for battery-powered standalone GNSS acquisition and the complete HA-609-to-display path. This result proves basic satellite reception and position reporting, not centimeter accuracy or survey suitability.

The planned K700 comparison is on hold because the purchased cable has the wrong antenna-side center-contact gender. The next independent checkpoint is bring-up of the second Waveshare/UM980 instrument. When the correct K700 cable is available, repeat this standalone open-sky procedure while keeping the receiver, firmware, location, orientation, power arrangement, and observation period as consistent as practical.
