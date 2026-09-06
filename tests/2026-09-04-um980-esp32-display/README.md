# Test: Initial UM980 ESP32 Display Integration

> **Status:** Historical result not reproduced under isolated testing. Do not use this record as validation of Channel 1 or the one-USB power path. See the later validated TTL Channel 2 test.

- **Date:** 2026-09-04
- **Purpose:** Validate single-USB power, bidirectional TTL UART, receiver identification, and live GGA display firmware.
- **Firmware:** `firmware/um980-display-demo/`

## Hardware and Wiring

| Item | Test configuration |
|---|---|
| Host USB device | Waveshare ESP32-S3 on `COM4` |
| BDRTK USB | Disconnected |
| BDRTK power | `5V_IN` from Waveshare J8 pin 2 USB `VBUS` |
| Ground | BDRTK `GND` to Waveshare J8 ground |
| UM980 to ESP32 | `TTL_TXD1` to GPIO44 / `ESP_RXD` |
| ESP32 to UM980 | GPIO43 / `ESP_TXD` to `TTL_RXD1` |
| PPS / EVENT | Disconnected |

The physical wiring was completed by the project owner before this test.

## Procedure

1. Confirm that only the Waveshare USB device enumerates on the PC.
2. Build and flash the dedicated display demo to the Waveshare.
3. Initialize the dedicated GNSS UART at 115200 baud, 8-N-1.
4. Send read-only `VERSION`.
5. Send volatile `GPGGA 1`; never send `SAVECONFIG`.
6. Parse and display GGA fields while mirroring receiver traffic to USB.
7. Observe the serial stream for receiver responses, continuity, and resets.

## Results

| Check | Result | Evidence |
|---|---|---|
| Firmware build | PASS | 19,996 bytes RAM; 347,273 bytes flash reported before upload |
| Firmware flash | PASS | COM4 upload completed and image hashes verified |
| Display initialization | PASS by firmware | `TCA9554: PASS` and `ST7796: PASS` |
| ESP32 to UM980 UART | PASS | `VERSION` and `GPGGA 1` acknowledged |
| UM980 identification | PASS | Version response identified UM980 and expected firmware |
| UM980 to ESP32 UART | PASS | Continuous checksummed `$GNGGA` messages at approximately 1 Hz |
| Position fix | NOT TESTED | GGA quality 0, with empty position and satellite fields |
| Power continuity | PASS for monitor interval | No restart observed while both boards ran from the Waveshare USB VBUS path |
| On-screen contents | PENDING | Requires project-owner visual confirmation |
| Current, voltage drop, temperature | NOT MEASURED | Required before treating this as a final power design |

## Sanitized USB Evidence

```text
TopoRTK UM980 display demo
TCA9554: PASS
ST7796: PASS
ESP32> VERSION
ESP32> GPGGA 1
BOOT COMPLETE
UM980> $command,VERSION,response: OK
UM980> #VERSION,...;UM980,R4.10Build13504,...,<unique-identifiers-redacted>,2024/04/03*<checksum-redacted>
UM980> $command,GPGGA 1,response: OK
UM980> $GNGGA,,,,,,0,,,,,,,,*78
UM980> $GNGGA,,,,,,0,,,,,,,,*78
```

## Conclusion

This run produced valid bidirectional logs, but its presumed Channel 1 wiring and one-USB power result were not reproduced under the later controlled isolation procedure. It remains useful as historical evidence only. The subsequent Channel 2 receive-only test is the accepted checkpoint.
