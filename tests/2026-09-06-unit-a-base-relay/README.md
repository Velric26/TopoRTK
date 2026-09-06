# Test: Unit A ESP32 Command Relay and Temporary Base Role

- **Date:** 2026-09-06
- **Purpose:** Verify that Unit A's ESP32 can relay an allowlisted configuration command to its UM980 over TTL Channel 2 and confirm the resulting role without reflashing.
- **Firmware environment:** `firmware/um980-display-demo/`, PlatformIO `unit_a`

## Configuration

| Item | Configuration |
|---|---|
| ESP32 USB | COM4 during this run |
| BDRTK USB/CH340 | COM8 during this run |
| 5 V interconnection | Removed; each board powered by its own USB |
| Ground | BDRTK GND to Waveshare GND |
| UM980 to ESP32 | BDRTK `TTL_TXD2` to GPIO44/RX |
| ESP32 to UM980 | GPIO43/TX to BDRTK `TTL_RXD2` |
| Display rotation | `0`, visually confirmed by the project owner |
| Instrument badge | `A`, visually confirmed by the project owner |

COM port numbers can change after reconnection. Unique device identifiers are intentionally omitted from the repository.

## Procedure

1. Build and flash PlatformIO environment `unit_a` through the ESP32 USB connection.
2. Observe successful display initialization and UM980 responses to the ESP32 startup commands.
3. Confirm the initial `MODE` query reports `MODE ROVER SURVEY`.
4. Enter `role base-test` on the ESP32 native USB console.
5. Observe the ESP32 send `MODE BASE`, followed by the read-only `MODE` query.
6. Inspect the UM980 acknowledgements and role read-back on the same ESP32 USB console.

## Results

| Check | Result | Evidence |
|---|---|---|
| Unit A build and flash | PASS | PlatformIO upload completed and image hash verified |
| Display initialization | PASS | `TCA9554: PASS` and `ST7796: PASS` |
| Rotation and badge | PASS | Project owner confirmed rotation `0` and visible `A` badge |
| Initial role query | PASS | Receiver returned `MODE ROVER SURVEY` |
| ESP32 command relay | PASS | ESP32 accepted `role base-test` and emitted `MODE BASE` over TTL2 |
| UM980 command acknowledgement | PASS | Receiver returned an OK response to `MODE BASE` |
| Role verification | PASS | Receiver read-back was `MODE BASE TIME 60 2.5 3.5` |
| On-screen role indication | PASS | Project owner confirmed Unit A displays `BASE` beneath `UM980 OK` |
| Valid averaged base position | NOT TESTED | GGA quality remained 0 with no antenna/fix during this bench test |
| RTCM generation/output | NOT TESTED | RTCM was not configured or inspected |
| Persistent configuration write | AVOIDED | No `SAVECONFIG` command was sent |

## Conclusion

Unit A passes the ESP32-to-UM980 configuration-relay checkpoint. Its role can be changed at runtime through the restricted ESP32 USB console without reflashing the ESP32 or UM980, and the verified receiver role is shown correctly on the display.

Unit A is only in a temporary base-test state. This result does not establish a survey-quality base coordinate, RTCM output, persistence across power loss, or RTK accuracy. Those require separate validate-first checkpoints with a suitable antenna, controlled base-position workflow, and explicit recovery procedure.

## Later Persistence Observation

During the subsequent Wi-Fi RTCM bench checkpoint, Unit A read back `MODE ROVER SURVEY` after the equipment had been reconnected and powered again. This confirms the temporary base-test role was not retained across that cycle. The role was then restored through `role base-test` and read back as `MODE BASE TIME 60 2.5 3.5`.
