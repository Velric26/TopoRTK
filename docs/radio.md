# Correction Radio

> Status (2026-09-11): both USB radios backed up, configured for transparent serial, and read back after saving/restarting. ESP32 UART/RTCM integration and field acceptance remain pending. See the [configuration and test record](../tests/2026-09-11-sik-radio-configuration/README.md). Current transport status is tracked in [live correction bridge](live-correction-bridge.md) and [packet loss investigation](packet-loss-investigation.md).

## Hardware

The consolidated [electronics architecture](electronics-architecture.md) records signal/power paths, all selected UART pins, current versus proposed connections, antenna handling and ESP32 power-control scope. Use the [reusable transport/field test](transport-field-test.md) for future range and integrity work.

Record exact Holybro SiK radio revisions, firmware, included antennas, connectors, and supply requirements.

The selected Holybro Long Range 1 W 915 MHz radio uses a 6-position JST-GH connector with 3.3 V TTL serial and an XT30 power input. Holybro specifies 7-28 V DC input; the 1 W variant should be powered from the unit's 3S battery branch, not from the ESP32 5 V rail. Keep the supplied radio antenna attached before transmitting.

The standard host wiring is crossed: radio `RX` receives from the ESP32 UART `TX`, and radio `TX` sends to the ESP32 UART `RX`. Confirm the exact pin-1 orientation and cable wiring against the Holybro drawing before crimping or cutting a cable. Leave RTS/CTS disconnected for the first test with hardware flow control disabled; add them only after the selected ESP32 pins are verified.

### Selected ESP32 serial wiring

The owner confirmed no camera is installed or planned. The original Waveshare ESP32-S3-Touch-LCD-3.5 schematic exposes GPIO17/18 on J8 and shares them with the unused camera socket. These are the selected radio UART pins on both instruments:

| ESP32 signal | J8 physical pin | Radio signal |
|---|---:|---|
| GPIO17, UART TX | 16 | RX |
| GPIO18, UART RX | 18 | TX |
| GND | 29 or 30 | GND |

Keep UM980 COM2 on GPIO43/TX and GPIO44/RX. Use a separate hardware UART for the radio; the bridge implementation and its current validation status are recorded in [live correction bridge](live-correction-bridge.md). The board's display, touch/I2C, native USB and SD pins remain allocated to their existing functions. Keep the camera socket empty. Confirm J8 pin 1 and the radio connector orientation physically; connector pin numbers are not GPIO numbers. Do not connect radio power to an ESP32 GPIO or 3V3 pin.

The selected topology is **UM980 COM2 <-> ESP32 <-> SiK** at each end. It avoids COM1 and permits future application telemetry/control, but correction delivery then depends on ESP32 uptime. Direct UM980-to-radio wiring remains a possible alternative, not the current integration target.

## Radio power control (requested 2026-09-15, needs hardware)

Operator request: at power-on the instrument stays on Wi-Fi and the SiK radios are **off** — or in a low-power mode — until radio communication is requested. This keeps a Wi-Fi-selected instrument from paying an idle SiK module's current for a link it is not using, which matters on the 3S pack (one pack ran flat unnoticed on 2026-09-15; see [power](power.md)).

**Electrical work this needs, none of which exists yet:**

- A switched supply for the radio module (high-side load switch or P-FET on the 3S feed) driven by a free GPIO, with the GPIO recorded in the hardware documents and in `board_hardware`.
- Inrush handling for the 1 W module, and a defined power-up settling time before any UART command is sent.
- A rule for the UART lines while the module is unpowered: the ESP32 has to neither back-power the module through its input protection nor see a floating RX as data. Series resistance or a level-safe arrangement on GPIO17/GPIO18 is part of this task.
- If the modules' own firmware turns out to support a dependable low-power command, that may replace the switched rail — but it must be verified on the actual module (ATI/version and a measured idle current), not assumed. Stock SiK behaviour on this point is not documented well enough to rely on.

**Firmware behaviour to implement once the switch exists:** energize before a probe, a radio test or a cutover whose target is Radio (before staging, not at commit); keep the module's boot/settling inside the existing negotiation and probe windows; never toggle the rail while a frame is transmitting; and report a powered-off radio as *off*, never as a fault. Nothing about sessions, replay or admission gates changes.

## Configuration

Both report `RFD SiK 2.0 on HM-TRP`. USB identities: COM14 `DU0EUNGIA`, COM15 `DU0EULJ8A` (FTDI 0403:6015). COM assignments can change. Neither radio has been physically assigned to Base/Rover or Unit A/B yet.

| Parameter | Before, both | Saved/read back, both | Decision |
|---|---:|---:|---|
| SERIAL_SPEED | 115 | 57 | Now 57600 baud, 8-N-1, for the loss diagnostic; GNSS COM2 baud is unchanged |
| AIR_SPEED | 64 | 64 | Initial 64 kbps air rate |
| NETID | 25 | 25 | Keep the matching pair ID; no claim of local uniqueness or security |
| TXPOWER | 11 | 1 | Lower bench power |
| ECC | 0 | 0 | Disabled |
| MAVLINK | 1 | 0 | Transparent binary serial |
| OPPRESEND | 0 | 0 | Preserve |
| MIN_FREQ / MAX_FREQ | 915000 / 928000 | 915000 / 928000 | Preserve existing kHz limits |
| NUM_CHANNELS | 50 | 50 | Preserve hopping configuration |
| DUTY_CYCLE | 100 | 100 | Preserve |
| LBT_RSSI | 0 | 0 | Preserve |
| MANCHESTER | 0 | 0 | Preserve |
| RTSCTS | 0 | 0 | No hardware flow control |
| MAX_WINDOW | 131 | 131 | Preserve initial transmit window |

Initially only TXPOWER and MAVLINK were changed. After repeat failures at approximately 60 cm separation, SERIAL_SPEED was changed from 115 to 57 on both radios and save/restart readback passed; the radio USB/UART speed is now **57600 baud**. All RF parameters were preserved. Both acknowledged `AT&W`; complete parameter readback after `ATZ` matched the intended values, including all unchanged settings. This tests a software restart, not physical power removal. Firmware and FORMAT were not changed. Saved settings, raw command responses and rollback instructions are in the dated record.

Holybro specifies a +10 dB amplifier offset for the selected 1 W hardware: TXPOWER 1 corresponds to about 12.5 mW output, versus about 125 mW at the previous value 11. These are manufacturer figures, not measured RF power. [Holybro power table](https://docs.holybro.com/radio/sik-telemetry-radio-v3/rf-transmission-power-setting-for-1w-variants).

115200 UART speed is not the sustainable over-air payload rate. Validate the actual RTCM load, both directions, buffering and recovery before increasing AIR_SPEED or field power. [SiK configuration reference](https://ardupilot.org/copter/docs/common-3dr-radio-advanced-configuration-and-technical-information.html).

Once the SiK pair passes its correction-stream validation, it becomes the preferred base-to-rover RTCM transport. The Rover Wi-Fi network remains available for the tablet browser interface and local log transfer; it must not be required for correction delivery. Retain the validated Wi-Fi RTCM implementation only as an explicitly labelled diagnostic/fallback path.

## RTCM Capacity

USB binary acceptance is **not yet passed**: the first 3000 bytes/second transfer lost data. At 1000 bytes/second, the two separate single-direction tests passed, but simultaneous 500 bytes/second in each direction lost isolated bytes. Keep the saved low-power profile for diagnosis; do not treat these results as field readiness or as an established reliable throughput ceiling. The owner reported antenna spacing below one metre; the owner could obtain only approximately 60 cm separation, where the 115200-baud repeat also failed; the 57600-baud comparison also failed. Both radios remain at 57600 baud for the next diagnostic session. See the [exact results and counters](../tests/2026-09-11-sik-radio-configuration/README.md).

Record UART rate, air rate, measured RTCM throughput, correction age, packet loss, and recovery behavior.

## Installation

Document antenna placement, separation from GNSS, mast use, cable loss, grounding, and weather protection.

## Range and Interference Tests

Link dated tests covering distance, terrain, RF power, Wi-Fi/display/storage activity, C/N0, and RTK state.

## Mexico Regulatory Check

Record the applicable IFT requirements, equipment homologation, lawful frequencies, power, antenna gain, and operating constraints before field transmission.

## Loss-tolerant transport

The approved loss-tolerant transport plan, its stages and its acceptance criteria live in [loss-tolerant transport](loss-tolerant-transport.md); results are in the dated `tests/2026-09-12-*` records, [packet loss investigation](packet-loss-investigation.md) and [live correction bridge](live-correction-bridge.md).
