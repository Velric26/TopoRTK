# UM980 Display Demo

> Status: Both units have validated bidirectional TTL Channel 2, rotation `0`, automatic startup profiles, and CRC-validated BESTNAV accuracy parsing. On 2026-09-06 Unit A generated live base RTCM over the ESP32 Wi-Fi bridge and Unit B reached `RTK FIXED` using HA-609 antennas. Absolute accuracy and repeatability remain untested.

This firmware keeps the validated display initialization and adds a dedicated UART connection to the UM980. It displays the most recent NMEA GGA position and mirrors receiver lines to native USB serial.

## Phone / Tablet Wi-Fi (2026-09-10)

On the Rover touchscreen, open **Link → Phone / Tablet → Show key**. Join the displayed SSID (current Unit A: `TopoRTK-Rover-A-16C8`) and open **http://192.168.8.1** in Chrome. Choose **Stay connected** if Android reports no Internet. No router or mobile data is needed. Use the screen's address if subnet conflict handling selects `172.22.42.1` instead.

The saved key has eight random digits and a middle period (`dddd.dddd`). The update replaces the original letter-based key once, so paired devices need the new key. **Show key** hides automatically after 30 seconds; **New key → Confirm** replaces it and disconnects phone clients. Neither changes the receiver configuration.

The phone AP runs alongside the correction station in both **Local Router** and **Direct Link**, with up to three clients. The PC router address also works in Local Router mode. `phone?` provides non-secret USB diagnostics. See [phone Wi-Fi validation](../../tests/2026-09-10-rover-phone-wifi/README.md) and the [ranked next features](../../docs/web-app-roadmap.md).

## Rover Browser Status (2026-09-10)

From a PC on the same router, open the Rover's IP address from its **Link** screen. Unit A is currently the Rover at **http://192.168.100.20/**; Unit B remains Base. This is a DHCP address and may change. `/ui/v1/` serves the same UI and `/api/v1/status` returns the complete version 1 JSON snapshot.

The read-only overview shows readiness, correction link, GNSS fix, horizontal uncertainty, Wi-Fi signal, GNSS time and actionable warnings. Expand **Connection & GNSS details** for ages and counters. It refreshes once per second, clears outdated values on connection loss and recovers automatically. Assets are compiled into firmware, so no Internet or separate filesystem upload is needed.

The original status endpoint remains read-only. UI 0.3 keeps an HTTP listener on both roles and adds paired survey commands; the HTTP task queues these for the survey/receiver workers. The Rover field access point is implemented; extended Android and field validation remain tracked in the phone Wi-Fi checkpoint.

Build both hardware variants with `pio run -e unit_a -e unit_b`. Unit A uses COM4 and Unit B uses COM10; normal uploads use `pio run -e unit_a -t upload` / `pio run -e unit_b -t upload`. Both now have UI 0.3. The survey checkpoint records USB recovery and verified flashing alongside the [earlier status validation](../../tests/2026-09-10-rover-web-status/README.md).

## Survey jobs and collection (UI 0.3)

Open **Open survey jobs** from the Rover status page, or `/survey` directly. Pair using the **six-digit WEB CONTROL PIN** shown locally by **Link → Phone / Tablet → Show key**. Create/open a job, review the Zapopan starter profile (WGS84 / UTM 13N, metres, ellipsoidal height and HA-609 models), complete the required antenna/base measurements, then collect quality-controlled stationary points. Jobs and points persist on SD; no point is acknowledged until write/readback succeeds. The same job reopens on startup. A new job still requires explicit review, measurements and confirmation before it is configured.

On the Base, `/survey` (also `/`) provides separately paired fixed-coordinate or temporary survey-in setup with receiver verification and NVS startup persistence. Enter receiver-reference ellipsoidal height, including the base antenna height/offset. This page changes the local Base only.

See the [workflow and limits](../../docs/survey-workflow.md), [test record](../../tests/2026-09-10-survey-workflow/README.md), and [ranked roadmap](../../docs/web-app-roadmap.md). This first implementation has 16 jobs, 512 durable command IDs and 1,024 journal records; WGS84 standard-zone UTM; ellipsoidal or locally validated constant-geoid heights; and a last-point preview. Full point management/plot, export, datum transforms, installed geoid grids and validated field accuracy remain later checkpoints.

## Touch Roles and Remembered Settings (2026-09-08)

Use **Setup → Base / Rover → Use Base / Use Rover** to change either instrument's role. Base also becomes the Wi-Fi access point and RTCM sender; Rover becomes the station and correction receiver. Hardware letters do not change. Set the other instrument to the opposite role separately.

Role, brightness (`Auto`, `Day`, `Night`), and the base RTCM enable flag survive restart in ESP32 NVS. Brightness buttons save immediately. Missing settings preserve the original A=Base/B=Rover defaults. Saved preferences load before networking, and receiver setup runs automatically after the UART handshake. No SD card or startup touch is needed.

Every profile clears old COM2 streams, waits for command acknowledgements, and verifies the final receiver role before permitting correction forwarding. Failed saves retain the previous active settings; failed receiver setup provides a retry action. No `SAVECONFIG` is sent. Base still uses temporary autonomous survey-in: base coordinates are not saved or certified by this feature.

The redesigned portrait interface has larger status values, four equal-height cards, two-line actionable warnings, and permanent **Home / GPS / Link / Setup** tabs. Existing vertical swipes remain available. Detailed text is fitted to the available width, and unchanged regions do not repaint.

Backlight brightness uses GPIO6 PWM at 5 kHz, matching the Waveshare example. `DAY` is full duty, `NIGHT` is approximately 10% duty, and transitions complete in about 1.2 seconds. `AUTO` uses checksum-validated GNSS UTC time converted to fixed UTC-6; when GNSS time is invalid or stale it deliberately remains full brightness and labels the state `AUTO: NO GNSS TIME`. The `brightness?` console command reports the requested mode, current/target duty, hardware duty, and PWM frequency.

Unit B has passed runtime role reversal, saved settings across ESP32 hardware resets, and profile verification on the bench. Physical touchscreen usability, complete power-off/on, and two-unit role reversal under RTK remain separate checks. See the [test record](../../tests/2026-09-08-touch-role-settings/README.md).

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

The yellow `A`/`B` badge identifies the physical instrument only. Either instrument can operate as base or rover through Setup. The interface supports portrait rotations `0` and `2`.

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

It then applies the saved role profile described above. `GPRMC` supplies checksum-validated UTC date/time. Receiver settings remain volatile; the demo never sends `SAVECONFIG`. The ESP32 independently stores the selected role and display preferences in its own NVS.

Bidirectional operation was proven by stopping COM2 output from the separate BDRTK USB/COM3 interface, observing silence on the ESP32, and resetting only the ESP32. GGA resumed after the ESP32 sent its startup command over GPIO43 to `TTL_RXD2`.

## Display and Navigation

The main screen shows correction-link state, role-aware GNSS state, horizontal 1DRMS uncertainty, link RSSI/quality, and the highest-priority warning. Use the bottom tabs to open Home, GPS, Link, or Setup. A top-origin downward swipe opens GPS details; a bottom-origin upward swipe opens Wi-Fi details. The opposite gesture returns to the main screen.

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
| `config?` | Report selected role, brightness, RTCM flag, storage, and profile verification |
| `role rover` | Save Rover; switch Wi-Fi to station; apply and verify the rover profile |
| `role base-test` | Save temporary Base; switch Wi-Fi to AP; apply and verify base plus RTCM |
| `accuracy?` | Report BESTNAV parser count, component sigmas, horizontal 1DRMS, age, and usability |
| `time?` | Report GNSS UTC date/time, fixed UTC-6 local time, and RMC navigation-valid status |
| `brightness auto` | Use GNSS local time, gradual dawn/dusk transitions, and full brightness if time is uncertain |
| `brightness day` | Force full daylight brightness |
| `brightness night` | Force reduced night brightness |

`role base-test` starts the UM980's default averaged-base behavior and is for functional testing only. It does not establish a survey-quality base. The console intentionally provides no arbitrary passthrough, `SAVECONFIG`, factory reset, baud-rate, or firmware-update commands. Brightness commands save the same preferences as the touchscreen.

The relay was validated on Unit A on 2026-09-06: the receiver acknowledged the ESP32's `MODE BASE` and verification query, then read back `MODE BASE TIME 60 2.5 3.5`. The project owner also confirmed that `BASE` appeared beneath `UM980 OK` on the physical display. This proves runtime configuration relay and role display without an ESP32 or UM980 reflash. It does not prove a valid base coordinate or RTCM output. See the [Unit A command-relay test](../../tests/2026-09-06-unit-a-base-relay/README.md).

## Wi-Fi Link Checkpoint

### Local Wi-Fi credentials

Do not put a network password in source, a commit, a screenshot, or a console
log. Before the router-connected web-interface checkpoint, create the local
credentials file once on this development PC:

```powershell
Copy-Item src/wifi_credentials.example.h src/wifi_credentials.h
```

Edit `src/wifi_credentials.h` and replace the two placeholder values with the
local **2.4 GHz** Wi-Fi SSID and password. The real file is ignored by Git;
only `wifi_credentials.example.h` is tracked. Use a separate IoT/guest SSID if
the router provides one.

The development command `wifi local` makes **both** instruments join this
router as stations; neither instrument creates an access point in that mode.
The Base discovers the Rover through its broadcast handshake and replies only
to that discovered peer. The Wi-Fi Details screen and `wifi?` console command
show the assigned DHCP address without exposing the password.

`wifi direct` restores the validated self-hosted link immediately: Base runs
the `TopoRTK-Link-Test` access point at `192.168.4.1` and Rover joins it. The
selected transport is stored with the other device preferences and survives a
restart. Direct is the safe default for old or missing settings.

Network behavior follows the saved operating role and transport:

| Transport | Base | Rover | Test behavior |
|---|---|---|
| `DIRECT LINK` | Access point at `192.168.4.1` | Station joins Base | Existing validated instrument-to-instrument bridge |
| `LOCAL ROUTER` | Station joins configured router | Station joins configured router | Rover broadcasts a signed hello on UDP 22345; Base replies to that discovered peer |

The test uses UDP port `22345`. Each packet carries a protocol marker, version, type, sequence number, sender timestamp, and application checksum. The display and USB log expose transmitted/received packets, sequence gaps, invalid packets, client state, and RSSI.

The direct-link password is a development-only credential and must not be
treated as production security. The local-router password is compiled from the
ignored header. The bridge carries validated RTCM frames only; it does not
forward NMEA, receiver commands, or survey records.

Use `wifi?`, `wifi direct`, and `wifi local` on native USB during development.
Changing transport clears link counters and peer state, reconnects Wi-Fi, and
does not change the UM980 role or its profile.

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
| `rtcm base-test` | Base role only: save the enabled RTCM flag and reapply the complete base profile |
| `rtcm off` | Base role only: save disabled RTCM and reapply the base profile with GGA/RMC/BESTNAV |

The receiver commands for RTCM 1006, 1033, 1074, 1084, 1094, and 1124 were acknowledged on Unit A. The bench checkpoint captured and forwarded CRC-valid RTCM frames with zero reported bridge errors. The subsequent HA-609 open-sky checkpoint produced `RTK FIXED` at Unit B, validating useful end-to-end correction flow for one session but not absolute accuracy or repeatability.

## Automatic Unit Profiles

No startup touch is required. The ESP32 restores its saved role, then applies that role's profile after the handshake confirms UM980 identity and fresh GGA traffic. Without saved settings, A defaults to Base and B to Rover. Both enable `GPGGA`, `GPRMC`, and `BESTNAVA` at 1 Hz. The profile is reapplied after a detected GNSS-link loss. Runtime touch and console selections update the same saved preferences.

These actions intentionally send no `SAVECONFIG`. The allowlisted USB console remains available for role queries, controlled recovery, RTCM counters, and `accuracy?` diagnostics.

The [Wi-Fi RTCM bridge bench test](../../tests/2026-09-06-wifi-rtcm-bridge-bench/README.md) records the transport checkpoint.

The [HA-609 Wi-Fi RTK open-sky test](../../tests/2026-09-06-ha609-wifi-rtk-open-sky/README.md) records the first `RTK FIXED` field checkpoint.

The [automatic-profile and horizontal-accuracy bench test](../../tests/2026-09-06-auto-profile-horizontal-accuracy/README.md) records the first real-hardware validation of the new startup behavior and BESTNAV parser.
