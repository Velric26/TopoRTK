# Unicore UM980 on BDRTK-980 Carrier

> The receiver is a Unicore UM980 mounted on a BDRTK-980 carrier from Shenzhen Beidou Lianxing Technology. The carrier model is supported by the seller manual and matching connector labels; the actual PCB revision is still to be read from the physical board.

## Quick Links

- [Local UM980 user manual R1.10](references/um980-user-manual-r1.10.pdf)
- [Local UM980 product brief](references/um980-product-brief.pdf)
- [Local BDRTK-980 carrier manual](references/bdrtk-980-board-manual.pdf)
- [Local N4 commands reference R1.15](references/n4-commands-reference-r1.15.pdf)
- [Local UPrecise user manual R2.2](references/uprecise-user-manual-r2.2.pdf)
- [Source URLs and checksums](SOURCES.md)
- [Waveshare ESP32 UART and power integration plan](esp32-uart-integration.md)
- [Safe serial demo](../../../tools/um980-demo/README.md)
- [First USB test record](../../../tests/2026-09-04-um980-usb-serial/README.md)
- [ESP32 display demo](../../../firmware/um980-display-demo/README.md)
- [First integrated display test](../../../tests/2026-09-04-um980-esp32-display/README.md)
- [Validated TTL Channel 2 receive test](../../../tests/2026-09-04-um980-esp32-ttl2-receive/README.md)
- [Validated TTL Channel 2 bidirectional test](../../../tests/2026-09-04-um980-esp32-ttl2-bidirectional/README.md)
- [Validated HA-609 standalone GPS fix](../../../tests/2026-09-05-ha609-standalone-fix/README.md)
- [Validated Unit B bring-up and rover role](../../../tests/2026-09-06-unit-b-bring-up/README.md)

## Module Summary

| Item | Manufacturer specification |
|---|---|
| GNSS engine | NebulasIV, 1408 channels |
| Constellations | GPS, BDS, GLONASS, Galileo, QZSS, NavIC, and SBAS |
| RTK accuracy, RMS | Horizontal 0.8 cm + 1 ppm; vertical 1.5 cm + 1 ppm |
| Typical RTK initialization | Less than 5 seconds under specified conditions |
| Maximum data update rate | 50 Hz, dependent on configuration/firmware |
| Correction format | RTCM 3.x |
| Output formats | NMEA 0183 and Unicore logs |
| Module interfaces | Three LVTTL UARTs; other interfaces depend on firmware |
| Module size | 17.0 x 22.0 x 2.6 mm, 54-pin LGA |

These are manufacturer specifications, not yet demonstrated by this project. Supported accuracy requires correct antenna, corrections, base coordinates, environment, configuration, and survey procedure.

The horizontal figure `0.8 cm + 1 ppm` is in the normal survey-RTK receiver class. It means an RMS horizontal uncertainty of approximately 8 mm plus 1 mm per kilometre of base-to-rover baseline under the manufacturer's test conditions: about 9 mm at 1 km and 18 mm at 10 km. RMS is a statistical specification, not a maximum-error guarantee. A displayed `H-ACC` near `0.8 cm` is therefore plausible and good, but it does not include every source of absolute field error. Incorrect base control, antenna height or centering, pole movement, multipath, antenna phase-center effects, datum/epoch/projection mistakes, and poor field procedure can each dominate the receiver estimate. TopoRTK must establish its own repeatability and known-control error before making an accuracy claim.

## Connected Receiver Identification

Observed on 2026-09-04:

| Item | Observed value |
|---|---|
| Windows port | `COM7` |
| USB bridge | WCH CH340 |
| USB VID:PID | `1A86:7523` |
| Carrier | BDRTK-980 |
| Carrier input | DC 4.0-5.5 V; 5.0 V typical |
| Carrier manual current | 160 mA at 5.0 V |
| Receiver | `UM980` |
| Firmware | `R4.10Build13504` |
| Build date | `2024/04/03` |
| Receiver identifier | `HRPT00-S10C-P` |
| COM1/COM2/COM3 baud | 115200 |
| NMEA version | V410 |
| Antenna power | On |

The Windows port can change after reconnection. The seller manual does not state which TTL channel is shared with the CH340, so treat `TTL_RXD1` as potentially driven by the USB bridge until this is verified.

## BDRTK-980 Carrier Interfaces

The carrier manual identifies:

- One USB Type-C port.
- One SMA antenna connector.
- A 5-pin, 1.25 mm connector carrying `PPS_OUT`, `5V_IN`, `GND`, `TTL_TXD1`, and `TTL_RXD1`.
- An 8-pin, 1.25 mm connector carrying `EVENT`, two RS-232 channels, `GND`, `TTL_TXD2`, and `TTL_RXD2`.
- A stated 5 V current of 160 mA. Treat this as a nominal board figure, not a complete power budget for the display, active antenna, radio, battery charging, and transients.

The project owner measured `TTL_RXD1` and `TTL_RXD2` idling at approximately 3.3 V. Before connection, also verify each carrier TX output idles near 3.3 V relative to carrier ground.

### Validated Port Mapping

Bench tests on 2026-09-04 established:

| Carrier interface | UM980 port | Evidence/status |
|---|---|---|
| USB CH340 | COM3 | Only `GPGGA COM3 1` appeared on the USB port |
| 8-pin `TTL_TXD2` / `TTL_RXD2` | COM2 | `TTL_TXD2` measured 3.26 V idle; explicit COM2 GGA reached ESP32 GPIO44 at 1 Hz |
| 5-pin `TTL_TXD1` / `TTL_RXD1` | COM1 per seller manual | `TTL_TXD1` measured approximately 0.022 V while isolated; do not use pending investigation |

The BDRTK USB port can remain connected during COM2 testing because it uses COM3. ESP32 GPIO43/TX to `TTL_RXD2` was added only after the receive-only checkpoint passed; bidirectional COM2 then passed the controlled stop/reset/restart test.

### Seller Manual Discrepancy

The BDRTK manual describes `EVENT` as though it outputs a pulse. The Unicore UM980 manual defines module pin 51 `EVENT` as an **input** for event marking. Treat `EVENT` as an input unless the carrier manufacturer provides a schematic proving that it is buffered or repurposed. When carrier documentation conflicts with the module manufacturer, the Unicore electrical definition controls.

## Initial Configuration Snapshot

The read-only `CONFIG` response reported:

- RTK timeout: 120 seconds.
- RTK reliability: `3 1`.
- PPP timeout: 120 seconds.
- DGPS timeout: 300 seconds.
- B1C/B2a RTCM support enabled.
- Antenna delta H/E/N: 0.0000/0.0000/0.0000.
- PPS enabled, GPS time, positive polarity.
- Anti-jamming: automatic.
- AGNSS disabled.
- Base observation filter disabled.
- COM1, COM2, and COM3: 115200 baud.

The complete response is stored in the first test record. No configuration was saved or reset.

## First Demo Result

The receiver:

1. Responded to read-only `VERSION` and `CONFIG` queries.
2. Accepted temporary `GPGGA 1` output.
3. Produced twelve GGA messages in approximately twelve seconds.
4. Accepted `UNLOG`, restoring the port's prior no-output state.

GGA fix quality was `0` with no coordinates or satellites, so serial communication and NMEA generation passed but GNSS positioning did not. Connect a compatible active GNSS antenna outdoors for the next positioning test.

### 2026-09-05 HA-609 Standalone Fix

With the HA-609 helix antenna under open sky and the assembly powered from a USB power bank through the Waveshare USB-C/VBUS path, the display reported `GPS FIX`, 13 satellites, HDOP 0.90, populated coordinates, and 1627.096 m GGA altitude. UART remained active, the receiver identified as `UM980 OK`, and the checksum-error counter remained zero.

This passes standalone GNSS acquisition and the complete antenna-to-display data path. It does not establish survey accuracy, RTK correction handling, repeatability, or the suitability of GGA altitude for survey elevations.

## Electrical and Integration Cautions

- The UM980 module UARTs are LVTTL; the BDRTK manual identifies its exposed TTL channels but does not publish numeric TTL thresholds.
- The 5-pin connector's `5V_IN` is a carrier power input. It is not a 5 V output.
- The Waveshare header's 5 V pin is direct USB `VBUS`, not an independently regulated accessory output.
- Never join the two boards' 5 V rails while both USB ports are connected; that can connect two USB VBUS sources together.
- Do not let the ESP32 TX and CH340 TX drive the same UM980 RX line simultaneously.
- Do not assume a pin marked `TX` is viewed from the cable or host perspective; verify it from carrier documentation or measurement.
- Antenna bias is currently enabled. Confirm the carrier's SMA path, bias voltage/current, and the antenna's requirements before connection.
- Do not send `SAVECONFIG`, `FRESET`, `RESET`, base-mode, baud-rate, or RTCM-output commands during bring-up unless the intended change and recovery path are documented.

## Next Incremental Test

Both UM980/ESP32 pairs passed USB identification and bidirectional TTL Channel 2 by 2026-09-06. Unit B is explicitly confirmed in `MODE ROVER SURVEY`. Unit A accepted an allowlisted `MODE BASE` command relayed by its ESP32 and read back `MODE BASE TIME 60 2.5 3.5`; this validates runtime command relay and a temporary base role, not a usable base position or RTCM output. No `SAVECONFIG` was sent. K700 validation is on hold because the purchased cable has the wrong antenna-side center-contact gender. Channel 1 remains out of service; RTCM, RTK accuracy, antenna comparison, and extended power stability remain separate investigations.
