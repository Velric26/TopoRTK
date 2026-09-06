# Test: UM980 TTL Channel 2 Receive-Only

- **Date:** 2026-09-04
- **Purpose:** Validate one UM980-to-ESP32 UART direction using the independently verified TTL Channel 2 output.
- **Firmware:** `firmware/um980-display-demo/`

## Isolated Configuration

| Item | Configuration |
|---|---|
| ESP32 USB | PC, COM4 |
| BDRTK USB/CH340 | PC, COM9 during initial configuration and controlled run |
| 5 V interconnection | Disconnected |
| Common ground | BDRTK GND to Waveshare GND |
| Data | BDRTK `TTL_TXD2` to Waveshare GPIO44 / RX |
| ESP32 TX | GPIO43 disconnected |

## Prechecks

- BDRTK `TTL_TXD1`, isolated: approximately 0.022 V; rejected for this test.
- BDRTK `TTL_TXD2`, isolated: 3.26 V; accepted for receive-only testing.
- BDRTK USB current port was identified as UM980 COM3.
- Target output was explicitly configured with `GPGGA COM2 1`; no `SAVECONFIG` was sent.

## Results

| Check | Result | Evidence |
|---|---|---|
| UM980 accepted explicit COM2 output | PASS | `$command,GPGGA COM2 1,response: OK` |
| Electrical idle level | PASS | `TTL_TXD2` measured 3.26 V |
| ESP32 received COM2 data | PASS | COM4 logged continuous `$GNGGA` lines |
| NMEA checksum | PASS | Repeated no-fix message checksum `*78` |
| Approximate output rate | PASS | One GGA message per second |
| Visible display state | PASS | Project owner confirmed `UART RECEIVING` |
| Position fix | NOT TESTED | GGA quality 0; empty position and satellite fields |

## Later Confirmation With Only ESP32 Enumerated

- The project owner reported `TTL_TXD2` connected to ESP32 GPIO44, `GND` connected to `GND`, and BDRTK `5V_IN` connected to the Waveshare 5 V/VBUS header.
- Only the ESP32 was connected by USB to the PC; the BDRTK USB port was unplugged.
- Windows enumerated only the ESP32 as COM4; no BDRTK CH340 port was present.
- COM4 again logged continuous `$GNGGA,,,,,,0,,,,,,,,*78` at approximately 1 Hz.
- This validates startup and short receive-only operation with both boards powered from the ESP32 USB VBUS source. Supply voltage under load, current, temperature, antenna load, and long-duration stability were not measured.

## Conclusion

BDRTK TTL Channel 2 transmit, the `TTL_TXD2` to GPIO44 jumper, ESP32 GPIO44 UART receive, parser, and display status are validated. The later rerun also validates startup and short receive-only operation with the ESP32 USB VBUS powering both boards. It does not yet validate electrical margin or long-duration power stability. Bidirectional COM2 communication is the next isolated UART checkpoint. Channel 1 remains out of service pending investigation.
