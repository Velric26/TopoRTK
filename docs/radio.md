# Correction Radio

> Status: Hardware selected; connector and power plan documented. Start at low RF power and validate every setting incrementally.

## Hardware

Record exact Holybro SiK radio revisions, firmware, included antennas, connectors, and supply requirements.

The selected Holybro Long Range 1 W 915 MHz radio uses a 6-position JST-GH connector with 3.3 V TTL serial and an XT30 power input. Holybro specifies 7-28 V DC input; the 1 W variant should be powered from the unit's 3S battery branch, not from the ESP32 5 V rail. Keep the supplied radio antenna attached before transmitting.

The standard host wiring is crossed: radio `RX` receives from the ESP32 UART `TX`, and radio `TX` sends to the ESP32 UART `RX`. Confirm the exact pin-1 orientation and cable wiring against the Holybro drawing before crimping or cutting a cable. Leave RTS/CTS disconnected for the first test with hardware flow control disabled; add them only after the selected ESP32 pins are verified.

## Configuration

| Parameter | Base | Rover | Reason | Validated |
|---|---|---|---|---|

Initial bench settings should match on both radios: 57.6 kbps serial, 8-N-1, same Network ID, same regional band, and transparent serial mode. The first firmware test should send a counter through the radios before inserting RTCM into the path.

## RTCM Capacity

Record UART rate, air rate, measured RTCM throughput, correction age, packet loss, and recovery behavior.

## Installation

Document antenna placement, separation from GNSS, mast use, cable loss, grounding, and weather protection.

## Range and Interference Tests

Link dated tests covering distance, terrain, RF power, Wi-Fi/display/storage activity, C/N0, and RTK state.

## Mexico Regulatory Check

Record the applicable IFT requirements, equipment homologation, lawful frequencies, power, antenna gain, and operating constraints before field transmission.
