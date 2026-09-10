# Firmware

> Status: Display/GNSS, SD logging, and the Wi-Fi RTCM bridge have been validated on project hardware. Runtime role selection and NVS restoration passed a Unit B bench checkpoint on 2026-09-08; physical touch and complete power-cycle validation remain pending.

## Supported Hardware

List exact ESP32-S3 board revisions and connected peripherals.

## Development Environment

Record toolchain, framework, dependencies, board settings, and required host tools.

## Build and Flash

Document commands, cable/port requirements, recovery procedure, and how to confirm the flashed version.

## Architecture

Describe tasks/modules for GNSS control, radio monitoring, logging, local UI, networking, and mobile UI.

Current implementation: [UM980 display demo](../firmware/um980-display-demo/README.md).

### Read-only Rover browser status service

The firmware serves a **read-only** Rover status page at `/` and `/ui/v1/`, plus `GET /api/v1/status`. In development mode, a PC on the same router opens the Rover's DHCP address (Unit A bench address `http://192.168.100.20`). UI 0.3 also serves `/survey` on both roles, with separately paired typed commands; the Base root opens its receiver setup page. The original status endpoint remains read-only and returns 409 in Base role.

`main.cpp` constructs a bounded 2 KiB dashboard snapshot every 250 ms. `web_http.cpp` runs the ESP-IDF HTTP server in a separate task with three client sockets, LRU eviction and two-second receive/send timeouts. GET handlers copy snapshots under short critical sections; command handlers authenticate and enqueue typed requests. They never access receiver hardware, SD or NVS directly. Dashboard snapshots older than 1.5 seconds and survey snapshots older than two seconds return 503. Unknown GET routes return 404; unsupported methods return 405. Responses disable caching. `web_ui.h` and `survey_ui.h` contain the flash-resident pages; no separate asset upload is needed.

API version 1 includes `device`, `state`, `gnss`, `link`, `time`, and `warning`. UI 0.2.0 adds `phone_wifi` with availability, public AP SSID/address and client count. `boot_id` and sampled `uptime_ms` let the client detect an unchanged snapshot across polls and recover across reboots. Missing or unusable measurements are `null`; no coordinates, upstream router SSID, passwords or raw receiver identifiers are published.

The local-router checkpoint is recorded in [2026-09-10 validation](../tests/2026-09-10-rover-web-status/README.md), followed by the [phone AP checkpoint](../tests/2026-09-10-rover-phone-wifi/README.md). UI 0.3 adds roadmap ranks 2–5: see [survey architecture, endpoints, storage and limits](survey-workflow.md) and its [test record](../tests/2026-09-10-survey-workflow/README.md). Next development: full point review/offline plot, after outstanding hardware/field acceptance.

### Phone access point

`rover_ap.cpp` adds a WPA2 SoftAP to the Rover's existing Wi-Fi station. The Base correction network remains unchanged. Phone clients use a separate /24 subnet (192.168.8.0, alternate 172.22.42.0 on conflict). UDP discovery uses the upstream station's directed broadcast address; Rover correction packets from outside that station subnet are discarded. The AP does not route Internet traffic. Its default channel is 6, but the connected station channel takes priority; changing upstream networks can briefly disconnect phone clients. See the [ESP32-S3 Wi-Fi guide](https://docs.espressif.com/projects/esp-idf/en/v4.4.5/esp32s3/api-guides/wifi.html).

NVS namespace `topoap`, key `password`, stores eight random decimal digits with a middle period. Rejection sampling uses the hardware RNG after Wi-Fi starts; writes are committed and read back before use. Existing 16-character prototype keys migrate once; other corrupt records leave the AP unavailable until local replacement. Missing/invalid credentials leave the phone AP disabled. The [ESP32-S3 RNG documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/api-reference/system/random.html) describes the Wi-Fi entropy prerequisite.

The touchscreen is the only password-reveal path. `phone?` prints public AP diagnostics without the key. Replacement restarts only the AP side, without calling receiver or survey configuration functions. Runtime Base stops the phone AP. The `toportk/config` encoding is unchanged.

The first mobile client is a browser connected through the local router during development or the Rover's password-protected access point in the field. The ESP32 serves the static interface and a versioned local API; the Rover SD card remains authoritative for jobs, points, configuration audit data, and exports. Design the service so a reconnecting browser can fetch a complete current state without relying on stale tablet storage.

Use a live status channel for non-critical display updates and bounded request/response endpoints for configuration and point-collection actions. State-changing requests require an explicit result and durable rover-side audit record. Keep the radio RTCM transport independent of the UI service. Bluetooth LE and an Android WebView bridge are deferred integration layers, not prerequisites for the browser workflow.

## Configuration and Storage

### Development-only Wi-Fi credentials

The tracked template
[`wifi_credentials.example.h`](../firmware/um980-display-demo/src/wifi_credentials.example.h)
is copied locally to `wifi_credentials.h` and filled with the local router's
2.4 GHz SSID and password. The real file is Git-ignored. The saved console
toggle `wifi local` makes both Base and Rover Wi-Fi clients on that network;
`wifi direct` restores the autonomous Base access point/Rover station bridge.
Local mode is a development and browser-interface transport, not the final
field correction dependency.

The ESP32 Preferences namespace `toportk` contains a single `uint32_t` key, `config`. Current schema tag `0x54520200` encodes role in bit 0, brightness in bits 1-2, base RTCM enabled in bit 3, and Wi-Fi transport in bit 4 (`0` Direct, `1` Local Router). Older `0x54520100` records remain readable and default to Direct Link. Reserved bits, unknown schema tags, and brightness value 3 are rejected. Related settings share one NVS value. Writes are checked and read back before changing runtime configuration; unchanged values do not write again.

Defaults are Unit A Base / other units Rover, automatic brightness, and enabled base RTCM. Defaults apply when storage is missing; invalid or inaccessible storage also reports an error. NVS is independent of the SD logger. UM980 settings remain volatile, with no `SAVECONFIG`; the saved role is reapplied at boot and after detected GNSS-link loss. The temporary autonomous base coordinate itself is not persisted.

The touchscreen and console share the same configuration functions. Profile setup is serviced between main-loop iterations, checks each command acknowledgement, retries missing replies up to three attempts, and waits for the final role readback. Receiver short replies include their leading `$`/`#` in the XOR checksum; this is separate from NMEA and BESTNAV CRC-32 validation.

See [interface behavior](interface.md) and [bench/software validation](../tests/2026-09-08-touch-role-settings/README.md).

## Verified Releases

| Version/commit | Device | Test record | Result |
|---|---|---|---|
| Uncommitted prototype, 2026-09-06 | Units A and B | [ESP32 Wi-Fi link](../tests/2026-09-06-esp32-wifi-link/README.md) | PASS: handshake and checksummed/sequenced packets; RTCM not carried |
