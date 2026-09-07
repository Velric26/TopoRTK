# UM980 Display Demo

> Status: Both units have validated bidirectional TTL Channel 2, rotation `0`, automatic startup profiles, and CRC-validated BESTNAV accuracy parsing. On 2026-09-06 Unit A generated live base RTCM over the ESP32 Wi-Fi bridge and Unit B reached `RTK FIXED` using HA-609 antennas. Absolute accuracy and repeatability remain untested.

This firmware keeps the validated display initialization and adds a dedicated UART connection to the UM980. It displays the most recent NMEA GGA position and mirrors receiver lines to native USB serial.

## SD Logging (Unit A Checkpoint)

Each unit mounts the Waveshare microSD/TF slot in 1-bit SD_MMC mode using GPIO9 (`D0`), GPIO10 (`CMD`), and GPIO11 (`CLK`). The first boot performs a non-destructive read/write/read-back check at `/TOPO-RTK/SD-READBACK-TEST.TXT` and creates a session directory under `/TOPO-RTK/UNIT-A/SESSIONS/` or `/TOPO-RTK/UNIT-B/SESSIONS/`, depending on the build.

Each session contains:

- `session.json`: unit, boot uptime, and card capacity.
- `events.csv`: boot, UART, link, GNSS-fix, role, and configuration state changes.
- `config.csv`: automatic profiles and allowlisted manual configuration requests.
- `solution.csv`: approximately 1 Hz GGA solution values, H-ACC, correction age, link RSSI, RTCM frame/byte counters, and network error counters.

The logger is append-oriented and flushes after each record in this initial reliability checkpoint. It does not yet record raw observations or a binary RTCM archive. Both Unit A and Unit B have now been flashed with SD support; each unit must use its own card during a two-sided field session.

Display rotation is selected per instrument so the two printed bodies can use different physical orientations without editing source code:

| PlatformIO environment | Instrument badge | Rotation |
|---|---:|---:|
| `unit_a` | `A` | `0` (original portrait orientation) |
| `unit_b` | `B` | `0` (original portrait orientation) |

Both units' rotation `0` settings were visually confirmed on real hardware.

The yellow `A`/`B` badge identifies the physical instrument only. It does not indicate GNSS role; either instrument may later operate as base or rover.

Unit B's yellow `B` badge was visually confirmed on real hardware on 2026-09-06. Its connected UM980 was separately commanded to and read back as `MODE ROVER SURVEY`.

## Bench Wiring

The initial controlled receive-only test powered each board from its own USB connection and did not join their 5 V rails. In a later confirmation, only the ESP32 was connected to the PC by USB: Waveshare `VBUS`/5 V powered BDRTK `5V_IN`, the boards shared ground, and GGA reception continued. This later setup validates startup and short receive-only operation from one USB source, but not current draw, voltage margin, temperature, or long-duration stability.

| BDRTK-980 8-pin header | Waveshare J8 header |
|---|---|
| `GND` | Pin 29 or 30, `GND` |
| `TTL_TXD2` | Pin 27, `ESP_RXD` / GPIO44 |
| `TTL_RXD2` | Pin 25, `ESP_TXD` / GPIO43; connected only after receive-only validation passed |

The ESP32 GPIO43/TX wire remained disconnected for the receive-only checkpoint, then was connected to `TTL_RXD2` for the bidirectional checkpoint. Never leave the 5 V interconnection wire installed when connecting the BDRTK USB port; doing so could tie two USB VBUS sources together.

## Receiver Behavior

When a bidirectional UART is connected, the ESP32 retries this handshake until it receives a valid identity response and fresh GGA data:

```text
VERSION
GPGGA COM2 1
GPRMC COM2 1
MODE
```

It then enables `BESTNAVA COM2 1` and automatically applies the unit-specific profile described below. `GPRMC` supplies checksum-validated UTC date/time. These settings are volatile. The demo never sends `SAVECONFIG`, so the settings are not intentionally written to receiver flash.

Bidirectional operation was proven by stopping COM2 output from the separate BDRTK USB/COM3 interface, observing silence on the ESP32, and resetting only the ESP32. GGA resumed after the ESP32 sent its startup command over GPIO43 to `TTL_RXD2`.

## Display and Navigation

The main screen shows only correction-link state, role-aware GNSS state, horizontal 1DRMS uncertainty, link RSSI/quality, and the highest-priority warning. A top-origin downward swipe opens GPS details; a bottom-origin upward swipe opens Wi-Fi details. The opposite gesture returns to the main screen. There are no permanent gesture instructions.

The four main information cards use equal height and spacing. The renderer caches each header, card, warning, and detail row and repaints only a region whose content or color changed. This avoids the previous four-times-per-second full-region repaint that caused visible flicker.

All screens use the same centralized definitions:

- Rover `GPS FIXED` requires GGA quality 4; base `GPS FIXED` requires quality 7.
- `CONNECTED` requires a current peer packet and active network link.
- `READY` requires current UM980 communication, applied profile, connected peer, required GNSS state, and fresh rover corrections.

The main banner is green only when `READY`; GPS details only when `GPS FIXED`; Wi-Fi details only when `CONNECTED`. Otherwise each banner is grey, and explicit text states the condition. The role appears in each header as `Rover` or `Base`.

GPS details contain UART, fix, local `UTC-6`, UTC date/time, coordinates, satellites/HDOP, altitude, `H-ACC`, component sigmas, and RTCM activity. Wi-Fi details contain mode, SSID, IP, link, RSSI, peer, transport counters, correction data, last-peer age, and brightness mode.

Base mode uses `BASE WAIT`, `BASE SURVEY`, and `BASE LOCKED` instead of the misleading generic `MANUAL` label. `H-ACC` shows `---` without a usable fix and `N/A (BASE)` after the autonomous base coordinate is locked.

## Build, Flash, and Monitor

From this directory, select the instrument explicitly:

```powershell
pio run --environment unit_a
pio run --environment unit_a --target upload
pio device monitor --environment unit_a

pio run --environment unit_b
pio run --environment unit_b --target upload
pio device monitor --environment unit_b
```

## Pass Criteria

- Display initializes and remains stable.
- USB log contains a successful response to `VERSION`.
- GGA messages arrive at approximately 1 Hz over GPIO44.
- The on-screen values match the raw GGA fields.
- Neither board resets, overheats, or shows unstable power behavior.

A `NO FIX` result still passes UART integration when the GGA messages are complete and checksummed. GNSS positioning is a separate outdoor antenna test.

See the [validated Channel 2 receive test](../../tests/2026-09-04-um980-esp32-ttl2-receive/README.md). The earlier [integrated test record](../../tests/2026-09-04-um980-esp32-display/README.md) is retained as historical evidence but its assumed Channel 1 wiring was not reproduced.

See also the [validated bidirectional Channel 2 test](../../tests/2026-09-04-um980-esp32-ttl2-bidirectional/README.md).

The [HA-609 standalone-fix test](../../tests/2026-09-05-ha609-standalone-fix/README.md) validates the complete battery-powered GNSS-to-display path. It does not validate RTK accuracy.

## Safe USB Role Console

The ESP32 accepts a small allowlisted command set through its native USB serial port and relays the corresponding UM980 commands over TTL Channel 2:

| Console command | UM980 action |
|---|---|
| `help` | Print the safe command list |
| `role?` | Send read-only `MODE` query |
| `role rover` | Send `MODE ROVER SURVEY`, then query `MODE` |
| `role base-test` | Send bare `MODE BASE`, then query `MODE` |
| `accuracy?` | Report BESTNAV parser count, component sigmas, horizontal 1DRMS, age, and usability |
| `time?` | Report GNSS UTC date/time, fixed UTC-6 local time, and RMC navigation-valid status |
| `brightness auto` | Use GNSS local time, gradual dawn/dusk transitions, and full brightness if time is uncertain |
| `brightness day` | Force full daylight brightness |
| `brightness night` | Force reduced night brightness |

`role base-test` starts the UM980's default averaged-base behavior and is for functional testing only. It does not establish a survey-quality base. The console intentionally provides no arbitrary passthrough, `SAVECONFIG`, factory reset, baud-rate, firmware-update, or RTCM configuration commands.

The relay was validated on Unit A on 2026-09-06: the receiver acknowledged the ESP32's `MODE BASE` and verification query, then read back `MODE BASE TIME 60 2.5 3.5`. The project owner also confirmed that `BASE` appeared beneath `UM980 OK` on the physical display. This proves runtime configuration relay and role display without an ESP32 or UM980 reflash. It does not prove a valid base coordinate or RTCM output. See the [Unit A command-relay test](../../tests/2026-09-06-unit-a-base-relay/README.md).

## Wi-Fi Link Checkpoint

The current test-only network roles are selected by instrument identity:

| Instrument | Wi-Fi role | Test behavior |
|---|---|---|
| Unit A | Access point at `192.168.4.1` | Accept Unit B handshakes and send sequenced test packets |
| Unit B | Station | Join Unit A, send handshakes, validate received sequence and checksum |

The test uses UDP port `22345`. Each packet carries a protocol marker, version, type, sequence number, sender timestamp, and application checksum. The display and USB log expose transmitted/received packets, sequence gaps, invalid packets, client state, and RSSI.

The SSID and password compiled into this checkpoint are test credentials and must not be treated as production security. The bridge carries validated RTCM frames only; it does not forward NMEA, receiver commands, or survey records.

The [ESP32 Wi-Fi link test](../../tests/2026-09-06-esp32-wifi-link/README.md) passed through packet 84 with zero detected gaps and zero invalid packets.

## RTCM-over-Wi-Fi Checkpoint

The firmware now includes an RTCM v3 frame parser and bridge:

1. Unit A recognizes RTCM only after the `0xD3` preamble and complete 10-bit frame length.
2. It validates the RTCM CRC-24Q before transmission.
3. The complete frame is wrapped in a sequenced, checksummed Wi-Fi envelope.
4. Unit B validates the envelope, message metadata, and RTCM CRC-24Q again.
5. Unit B writes only a fully validated original RTCM frame to its UM980 UART.

NMEA and receiver command responses are not forwarded as correction data. Both displays and USB logs show RTCM frame/error counters.

Safe console additions:

| Console command | Behavior |
|---|---|
| `rtcm?` | Print bridge counters without changing configuration |
| `rtcm base-test` | Unit A only: enable the volatile manufacturer-example COM2 MSM4 stream |
| `rtcm off` | Unit A only: stop COM2 output, restore GGA, and query role |

The receiver commands for RTCM 1006, 1033, 1074, 1084, 1094, and 1124 were acknowledged on Unit A. The bench checkpoint captured and forwarded CRC-valid RTCM frames with zero reported bridge errors. The subsequent HA-609 open-sky checkpoint produced `RTK FIXED` at Unit B, validating useful end-to-end correction flow for one session but not absolute accuracy or repeatability.

## Automatic Unit Profiles

No touch action is required for receiver configuration. Touch is used only for page navigation. After the startup handshake confirms UM980 identity and fresh GGA traffic, Unit A automatically applies temporary `MODE BASE` plus the six volatile RTCM outputs, while Unit B applies `MODE ROVER SURVEY`. Both also enable `GPGGA`, `GPRMC`, and `BESTNAVA` at 1 Hz. The profile is reapplied after a detected GNSS-link loss.

These actions intentionally send no `SAVECONFIG`. The allowlisted USB console remains available for role queries, controlled recovery, RTCM counters, and `accuracy?` diagnostics.

The [Wi-Fi RTCM bridge bench test](../../tests/2026-09-06-wifi-rtcm-bridge-bench/README.md) records the transport checkpoint.

The [HA-609 Wi-Fi RTK open-sky test](../../tests/2026-09-06-ha609-wifi-rtk-open-sky/README.md) records the first `RTK FIXED` field checkpoint.

The [automatic-profile and horizontal-accuracy bench test](../../tests/2026-09-06-auto-profile-horizontal-accuracy/README.md) records the first real-hardware validation of the new startup behavior and BESTNAV parser.
