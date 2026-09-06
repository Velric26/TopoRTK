# Test: UM980 and ESP32 Bidirectional TTL Channel 2

- **Date:** 2026-09-04
- **Purpose:** Prove both UART directions independently of previously enabled UM980 output.
- **Firmware:** `firmware/um980-display-demo/`

## Configuration

| Item | Configuration |
|---|---|
| ESP32 USB | PC, COM4 |
| BDRTK USB/CH340 | PC, COM9; mapped to UM980 COM3 |
| 5 V interconnection | Removed; each board powered by its own USB |
| Ground | BDRTK GND to Waveshare GND |
| UM980 to ESP32 | BDRTK `TTL_TXD2` to GPIO44/RX |
| ESP32 to UM980 | GPIO43/TX to BDRTK `TTL_RXD2` |
| Baud | 115200 on both interfaces |

## Controlled Procedure

1. Confirm both USB devices enumerate and the ESP32 receives GGA.
2. Send volatile `UNLOG COM2` through BDRTK USB/COM3.
3. Confirm the UM980 acknowledges the command.
4. Listen on ESP32 COM4 and confirm no GGA or other UM980 lines arrive.
5. Reset only the ESP32; do not reset or unplug the UM980.
6. Listen on COM4 for resumed GGA.

The firmware sends `VERSION` and `GPGGA 1` on its GNSS UART at startup. Because COM2 output had been stopped and the UM980 was not reset, GGA could resume only if the ESP32 startup command crossed GPIO43 to `TTL_RXD2` successfully.

## Results

| Check | Result | Evidence |
|---|---|---|
| Both USB devices enumerated | PASS | ESP32 COM4; BDRTK CH340 COM9 |
| Stop COM2 output | PASS | `$command,UNLOG COM2,response: OK*52` |
| ESP32 became silent | PASS | No lines captured during a six-second COM4 observation |
| Reset only ESP32 | PASS | Performed by project owner |
| GGA resumed after ESP32 reset | PASS | Continuous `$GNGGA,,,,,,0,,,,,,,,*78` at approximately 1 Hz |
| Persistent receiver write avoided | PASS | No `SAVECONFIG` sent |
| Valid GNSS position | NOT TESTED | GGA quality remained 0 with empty position fields |

## Conclusion

Bidirectional UART on the BDRTK 8-pin TTL Channel 2 is validated at 115200 baud:

- UM980 `TTL_TXD2` to ESP32 GPIO44/RX: PASS.
- ESP32 GPIO43/TX to UM980 `TTL_RXD2`: PASS.

The next functional checkpoint is a valid standalone outdoor GNSS fix using one documented antenna. RTK, RTCM, radio, survey accuracy, and long-duration power behavior remain separate tests.
