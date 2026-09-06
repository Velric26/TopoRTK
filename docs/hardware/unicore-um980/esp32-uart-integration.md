# BDRTK-980 to Waveshare ESP32 UART and Power Plan

> **Status:** Bidirectional TTL Channel 2 passed a controlled stop/reset/restart test on 2026-09-04. A separate rerun passed startup and short receive-only operation with Waveshare USB VBUS powering BDRTK `5V_IN`; power margin and long-duration stability remain unvalidated. Channel 1 remains out of service. Perform and record further checkpoints one at a time.

## Source Facts

- BDRTK-980 supply: DC 4.0-5.5 V, 5.0 V typical.
- BDRTK-980 stated current: 160 mA at 5.0 V.
- BDRTK 5-pin connector: `PPS_OUT`, `5V_IN`, `GND`, `TTL_TXD1`, `TTL_RXD1`.
- Waveshare J8 pin 2 is `VBUS`, directly connected to USB Type-C VBUS.
- Waveshare J8 pin 25 is `ESP_TXD` / GPIO43.
- Waveshare J8 pin 27 is `ESP_RXD` / GPIO44.
- Waveshare J8 pins 29 and 30 are ground.
- The project owner measured BDRTK `TTL_RXD1` and `TTL_RXD2` at approximately 3.3 V idle. Verify `TTL_TXD1` before connection as well.

The seller manual does not specify whether USB/CH340 drives Channel 1 or Channel 2. Assume Channel 1 may be shared until proven otherwise.

Subsequent bench testing established that the carrier USB/CH340 is UM980 COM3, and the 8-pin TTL Channel 2 is COM2. The isolated `TTL_TXD1` output measured approximately 0.022 V despite explicit COM1 output configuration, so Channel 1 must not be used until investigated.

## Power Rules

1. `5V_IN` on the BDRTK is an input.
2. The Waveshare 5 V header is USB `VBUS`; the ESP32 board does not generate that 5 V rail.
3. Never connect Waveshare `VBUS` to BDRTK `5V_IN` while both USB ports are connected.
4. Never connect an external 5 V supply to Waveshare `VBUS` while its USB VBUS is also present unless a designed power-path or VBUS-blocking cable isolates the sources.
5. Do not power either board from the other's 3.3 V rail.
6. Budget separately for the active GNSS antenna, display/backlight, radio transmit current, battery charging, and startup transients.

## Checkpoint 1 - Receive-Only UART

Purpose: validate signal direction and parsing without allowing the ESP32 to transmit to the receiver.

Power both boards from separate USB cables. Do **not** connect their 5 V pins.

| BDRTK-980 | Waveshare J8 | Connection |
|---|---|---|
| `GND` | Pin 29 or 30 `GND` | Required common signal ground |
| `TTL_TXD2` | Pin 27 `ESP_RXD` / GPIO44 | Receiver data to ESP32 |
| `TTL_RXD2` | - | Leave disconnected |
| `5V_IN` | - | Leave disconnected |
| `PPS_OUT` | - | Leave disconnected |

Procedure:

1. With power off, verify `TTL_TXD2` is not shorted to 5 V or ground.
2. Power the BDRTK by USB and confirm `TTL_TXD2` idles near 3.3 V relative to its ground.
3. Power the Waveshare by its own USB.
4. Connect ground first, then `TTL_TXD2` to GPIO44. A 100-330 ohm series resistor is optional for the bench lead.
5. Use the BDRTK USB/COM3 connection to enable temporary `GPGGA COM2 1`; do not send `SAVECONFIG`.
6. Confirm that the ESP32 receives complete checksummed GGA lines and remains responsive.
7. Send `UNLOG COM2` from the PC and record the result.

Pass criteria:

- No board resets, excess heating, or supply instability.
- ESP32 receives complete GGA lines at approximately 1 Hz.
- The displayed fix state agrees with the raw GGA quality field.
- Temporary output is removed successfully.

### 2026-09-04 Controlled Result

The procedure was performed using `TTL_TXD2` / COM2 instead of the unverified Channel 1 output. `TTL_TXD2` measured 3.26 V idle, explicit `GPGGA COM2 1` was acknowledged, COM4 logged continuous checksummed GGA at approximately 1 Hz, and the project owner confirmed `UART RECEIVING` on the display. Result: PASS for Channel 2 receive-only operation.

## Checkpoint 2 - One USB, Integrated Power and UART

Purpose: validate the intended self-contained connection without the BDRTK USB bridge active.

Disconnect both USB cables before changing wiring.

| BDRTK-980 | Waveshare J8 | Connection |
|---|---|---|
| `5V_IN` | Pin 2 `VBUS` / 5 V | Temporary bench power from the single USB source |
| `GND` | Pin 29 or 30 `GND` | Common power/signal return |
| `TTL_TXD2` | Pin 27 `ESP_RXD` / GPIO44 | UM980 to ESP32 |
| `TTL_RXD2` | Pin 25 `ESP_TXD` / GPIO43 | ESP32 to UM980 |
| `PPS_OUT` | - | Leave disconnected |

Then connect only the Waveshare USB port to a known-good, adequately rated powered port. Leave the BDRTK USB disconnected.

The Waveshare pin does not regulate or current-limit this branch; it passes USB VBUS through. Use a USB power meter if available and verify that the 5 V rail stays stable while the display, receiver, and antenna operate. Stop if either board resets, the cable or connector heats, or the supply droops.

Pass criteria:

- Both boards start reliably from one USB source.
- The ESP32 receives a valid response to read-only `VERSION` and `CONFIG` commands.
- Temporary GGA can be enabled, received, and disabled without saving configuration.
- No brownout, restart, unexpected heating, or unstable serial data occurs.

### 2026-09-04 Initial Result - Not Reproduced

- Firmware build and flash: PASS.
- Waveshare display initialization reported by firmware: PASS.
- Read-only `VERSION` command and UM980 identification: PASS.
- Temporary `GPGGA 1` acknowledgment and continuous 1 Hz checksummed GGA reception: PASS.
- No restart observed during the captured monitor interval: PASS.
- Valid GNSS position: not tested; GGA reported quality 0.
- Physical display contents and temperature were not confirmed during this initial run.

Later isolated testing could not reproduce communication through the presumed Channel 1 pins, and `TTL_TXD1` measured approximately 0.022 V. Treat the original channel assignment and one-USB power result as unverified historical evidence, not an accepted design checkpoint.

### 2026-09-04 TTL2 Receive-Only Power Result

The one-USB arrangement was repeated with the validated Channel 2 receive path:

- Waveshare USB connected to the PC; BDRTK USB disconnected.
- Waveshare 5 V/VBUS connected to BDRTK `5V_IN`.
- `GND` connected to `GND`.
- BDRTK `TTL_TXD2` connected to ESP32 GPIO44/RX.
- ESP32 GPIO43/TX disconnected.
- Continuous checksummed GGA received at approximately 1 Hz.

Result: PASS for startup and short receive-only operation. Current draw, 5 V rail voltage under load, heating, antenna load, and long-duration stability remain to be measured before this power path is accepted for extended bench or field use. Disconnect the 5 V interconnection wire before attaching the BDRTK USB cable.

## Checkpoint 3 - Bidirectional COM2 UART

With both boards separately USB-powered and no 5 V interconnection, connect `TTL_TXD2` to GPIO44/RX, GPIO43/TX to `TTL_RXD2`, and ground to ground.

On 2026-09-04, `UNLOG COM2` sent through the BDRTK USB/COM3 interface was acknowledged and stopped all GGA received by the ESP32. Only the ESP32 was then reset. Its firmware sent `VERSION` and `GPGGA 1` over GPIO43/COM2, after which continuous checksummed GGA resumed on GPIO44 at approximately 1 Hz.

Result: PASS. The controlled state change proves both ESP32-to-UM980 and UM980-to-ESP32 directions on TTL Channel 2. No `SAVECONFIG` was sent. See the [bidirectional test record](../../../tests/2026-09-04-um980-esp32-ttl2-bidirectional/README.md).

See the [test record](../../../tests/2026-09-04-um980-esp32-display/README.md).

## Field Hardware Direction

For the finished base and rover, do not route the full system load through either development board. Use one protected, regulated 5 V supply with separate branches to the Waveshare, BDRTK-980, and SiK radio. Size it with margin for the 1 W radio's transmit peaks and all peripherals; 5 V at 2 A is a practical minimum design target, with 3 A preferred until measured consumption establishes a lower requirement.

Add branch protection, bulk capacitance near the radio and GNSS carrier, and a deliberate USB power-isolation strategy. USB should be a diagnostic/data connection, not an accidental second power source.

## Deferred Timing Pins

- Reserve `PPS_OUT` for later ESP32 time synchronization and latency measurement.
- Reserve `EVENT` as an input for an external event that the UM980 must timestamp precisely. The seller manual's output description conflicts with the Unicore module manual.
- Neither pin is required for NMEA reception, RTCM corrections, or the initial UART test.
