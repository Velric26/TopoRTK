# Test: Unit B ESP32 and UM980 Bring-Up

- **Date:** 2026-09-06
- **Purpose:** Validate the second ESP32/UM980 assembly, its physical-unit badge and orientation, bidirectional TTL2 UART, and initial rover role.
- **Firmware environment:** `firmware/um980-display-demo/`, PlatformIO `unit_b`

## Configuration

| Item | Configuration |
|---|---|
| ESP32 USB | COM10 during this run |
| BDRTK USB/CH340 | COM8 during this run |
| 5 V interconnection | Removed; each board powered by its own USB |
| Ground | BDRTK GND to Waveshare GND |
| UM980 to ESP32 | BDRTK `TTL_TXD2` to GPIO44/RX |
| ESP32 to UM980 | GPIO43/TX to BDRTK `TTL_RXD2` |
| Display rotation | `0` |
| Instrument badge | `B` |

COM port numbers can change after reconnection. Unique ESP32 and UM980 identifiers are intentionally omitted from the repository.

## Results

| Check | Result | Evidence |
|---|---|---|
| Distinct second ESP32 detected | PASS | Different hardware identity from Unit A; unique value redacted |
| Unit B build and flash | PASS | PlatformIO upload completed and image hash verified |
| Unit A compatibility build | PASS | `unit_a` environment compiled with rotation 2 and badge A |
| Display initialization | PASS | `TCA9554: PASS` and `ST7796: PASS` |
| Rotation 0/body fit | PASS | Project owner confirmed screen fit in the printed body |
| Physical-unit badge | PASS | Project owner confirmed yellow `B` is visible |
| UM980 identification | PASS | UM980 firmware `R4.10Build13504`; unique identifier redacted |
| Receive UART | PASS | Checksummed GGA received on ESP32 COM10 at approximately 1 Hz |
| Transmit UART | PASS | ESP32 `VERSION` and `GPGGA 1` commands acknowledged over TTL2 |
| Controlled bidirectional state change | PASS | `UNLOG COM2` stopped ESP32 data; ESP32 reflash/reset restored GGA through its startup command |
| Rover role command | PASS | `MODE ROVER SURVEY` acknowledged |
| Rover role read-back | PASS | Subsequent `MODE` query returned `MODE ROVER SURVEY` |
| Persistent configuration write | AVOIDED | No `SAVECONFIG` sent; UM980 receiver firmware was not reflashed |
| Standalone satellite fix | NOT TESTED | No antenna/open-sky checkpoint performed on Unit B yet |

## Conclusion

Unit B passes ESP32 display initialization, rotation 0, visible hardware identification, UM980 identification, bidirectional TTL Channel 2, and explicit survey-rover role selection. It is the current rover candidate.

The next checkpoint is Unit A rotation 0 and base-role planning. Base mode must not be treated as survey-ready until its coordinate, datum/epoch, altitude/height meaning, antenna reference, RTCM output, and recovery procedure are defined and validated.
