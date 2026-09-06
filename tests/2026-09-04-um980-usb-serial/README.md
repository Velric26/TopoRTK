# Test: UM980 USB serial bring-up

- **Date:** 2026-09-04
- **Location:** Development PC bench
- **Purpose:** Identify the connected receiver and validate commands plus temporary NMEA GGA output without saving configuration.
- **Tool:** `tools/um980-demo/`

## Hardware

| Item | Observed value |
|---|---|
| USB port | `COM7` |
| USB bridge | CH340, VID:PID `1A86:7523` |
| Receiver | UM980 |
| Firmware | `R4.10Build13504`, dated `2024/04/03` |
| Antenna/sky view | Not established; no valid position was available |

## Procedure

1. Listen at 115200 baud for six seconds without transmitting.
2. Send read-only `VERSION` and `CONFIG` commands.
3. Confirm that no periodic data was initially present.
4. Send `GPGGA 1` without `SAVECONFIG`.
5. Capture output for approximately twelve seconds.
6. Send `UNLOG` to restore the port's no-output state.

## Results

| Check | Result | Evidence |
|---|---|---|
| USB serial bridge detected | PASS | CH340 on `COM7` |
| Passive port state recorded | PASS | 0 bytes in 6 seconds |
| `VERSION` acknowledged | PASS | Receiver identified as UM980 |
| `CONFIG` acknowledged | PASS | Configuration snapshot captured |
| Temporary GGA output | PASS | 12 messages in approximately 12 seconds |
| Output restored | PASS | `UNLOG` acknowledged with `OK` |
| Valid GNSS position | NOT TESTED | GGA quality `0`; antenna/sky view not established |

## Conclusion

- **Result:** PASS for USB serial communication, receiver identification, command handling, and temporary NMEA output.
- **Limitation:** This does not validate satellite tracking, standalone accuracy, RTK, antenna compatibility, or base/rover operation.
- **Next action:** Identify the carrier and repeat GGA outdoors with a confirmed compatible antenna.

The archived `VERSION` response has its unique receiver serial, firmware fingerprint, and resulting checksum redacted so they are not published with the repository.
