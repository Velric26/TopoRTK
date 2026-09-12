# Standalone instrument link test

Updated 2026-09-12. Field hardware: two ESP32 instruments, their SiK radios and antennas, normal power supplies, and a tablet browser. **No PC is needed in the field.** The UM980 receivers are not needed for synthetic link testing. See [electronics architecture](electronics-architecture.md) for wiring and power.

## Tablet workflow

1. Keep both radio antennas connected before applying power. Confirm crossed UART wiring and common ground. Current SiK UART is 57600 baud; this page does not change radio settings.
2. Open **Link test** from each instrument's status or Survey page (`/diagnostics`). Access each instrument's own page while both are reachable. Press **Take control** on each. Both Base and Rover use latest-request takeover with no web PIN. Taking control again replaces the previous controller. Joining a password-protected Wi-Fi network still requires its Wi-Fi key.
3. Optionally press **Check radio wiring** on each page after confirming preparation. This briefly sends local SiK `+++`, `ATI`, and `ATO`, without saving or changing settings. A SiK identity reply confirms bidirectional local UART communication. No reply does not by itself prove swapped wires: check power, ground, baud, connector orientation and crossed TX/RX.
4. Enter the same fresh six-digit **test code** on both instruments. This is a session identifier, not a password. Match link, direction, duration and rate. Start with 30 seconds, 1000 framed bytes/second and both directions on the bench. One instrument must be Base and the other Rover.
5. Confirm preparation and arm each instrument. Both wait up to two minutes for matching peer settings, then start after a short countdown. Tests last 30, 60, 120 or 300 seconds, followed by five seconds to drain packets. Choose 300 seconds when moving apart during a range test. For a test wholly at a fixed location, arm both instruments there within the two-minute window.
6. The tablet may disconnect: generation, checking and completion run on the ESP32s. Survey writes and role/configuration changes are blocked while a diagnostic or local UART probe is active. An active occupation, queued survey operation or receiver profile operation prevents arming. A queued response is not proof of arming; check the displayed state.
7. Reconnect and download the JSON report. Each instrument retains its latest completed/interrupted result in NVS across restart. Only one result per instrument is retained, so download before another run. A restart during a test is recorded as interrupted, never passed.

The page shows the connected role and locks controls when the connection is lost or stops updating. Authenticated polling renews the current controller lease without reclaiming ownership. Leases expire after 120 seconds without authenticated activity; a takeover by another browser immediately invalidates the previous bearer. Take control again if cancellation is rejected. The bounded test still ends without tablet intervention. Local USB console commands exist only for development/bench verification; they are not required for the field workflow.

## Results and acceptance

A pair passes only after both instruments complete all expected sends/receives, with zero corrupt data, duplicates or reordered packets, and exchange matching peer summaries. Missing peer results mean **not passed**, even if the local direction delivered every packet. Result exchange repeats for 60 seconds after completion; if the link is still unavailable, download each instrument's report separately after returning. There is no later automatic merging or indefinite radio retry.

The page distinguishes a restored saved report from a current run. Download chooses the current run whenever one exists, including a failed or in-progress run; it never substitutes a previous passing report. Idle instruments offer their saved report. The report includes run code, role, transport, duration, framed data rate, expected and actual counts, errors, duplicates, reordering, longest gap between valid received packets, local/pair pass and peer counters. The gap is an interarrival measure, not synchronized one-way latency, RTT, correction age, or initial/tail outage duration. Parser errors include discarded serial bytes and invalid frames; they are not an RF bit-error rate. Record distance, antenna orientation/height, supply, terrain, obstacles and saved radio configuration alongside the downloaded report; the test does not measure those automatically.

Repeat Base-to-Rover, Rover-to-Base and both directions at each useful distance. Start with 1000 framed bytes/second, then qualify the actual expected RTCM load and bursts; available presets are 200, 1000 and 3000 per transmitting instrument. Control frames add overhead. Failed synthetic delivery blocks link qualification but does not establish that a radio is damaged. After synthetic acceptance, real RTCM continuity, correction freshness, RTK recovery and independent check points must still be tested.

## Implementation and scope

- SiK uses hardware UART2: ESP32 GPIO17 TX to radio RX; GPIO18 RX from radio TX, 57600 8N1. RX/TX buffers are bounded. Synthetic frames are never forwarded to UM980.
- Wi-Fi uses UDP 22346 and the peer discovered by the existing instrument Wi-Fi exchange. It tests the instrument-to-instrument network, not tablet range. Production RTCM remains on UDP 22345. For router mode, both stations and the router form the tested path; Direct Link tests the direct instrument network.
- Each fixed 256-byte packet carries version, session, role, settings, sequence/counters, deterministic payload and CRC32. A 4096-bit bitmap tracks unique data packets; maximum configured send count is 3515. Hello/result frames repeat, while test data are not retransmitted, so losses remain visible.
- The standalone SiK diagnostic is implemented; a production SiK RTCM/status bridge and radio power-setting UI are still pending. Current UART probing only identifies the local radio.
- BLE remains future work. ESP32-S3 supports BLE, not Classic SPP. A BLE GATT adapter needs explicit fragmentation, backpressure and session handling before it can join this test engine. [Espressif support matrix](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/bt-architecture/overview.html).

## Development verification

The pure C++ engine test is [diagnostic_core_test.cpp](../tests/2026-09-12-transport-test-kit/diagnostic_core_test.cpp). It covers a clean pair, packet loss/corruption, CRC reference vector, timer wrap, cancellation, peer timeout and invalid/reused configuration. Build with a C++17 compiler and include `firmware/um980-display-demo/src`.

Build/upload records and hardware snapshots are in [the dated test record](../tests/2026-09-12-transport-test-kit/README.md). PC USB was used to flash and trigger bench checks; the instruments themselves generate/check packets and save their reports. Field acceptance requires the tablet workflow and the intended field supplies and spacing.

## Connecting without the local router

Set both instruments to Direct Link on their touchscreens before leaving the router. Base then hosts the existing `TopoRTK-Link-Test` network; its test-network key is `TopoRTK-test-2026`, and its browser is at `http://192.168.4.1/diagnostics`. This is separate from the Rover's phone hotspot and its eight-digit key with a period. Use the Rover's displayed phone address for its page (normally `http://192.168.8.1/diagnostics`). Connect the tablet to each network in turn to arm its instrument, within the two-minute peer wait. With SiK selected, generation/checking continues over SiK even when the tablet or the inter-instrument Wi-Fi link is absent. Base and Rover roles must differ. Do not change role or network during a running diagnostic.

The automated browser bench check uses the actual instrument pages at phone/tablet viewport sizes. It is not evidence of a physical Android tablet test or Direct Link field acceptance; those remain operator checks.

## Loss-tolerant transport plan

The approved [transport development plan](loss-tolerant-transport.md) separates fresh RTCM delivery, acknowledged duplicate-safe commands and periodic status. Start with a fixed-memory core and on-instrument fault self-tests, then paired synthetic SiK transport, then real UM980 COM2/RTK validation. Keep both UM980s disconnected through the synthetic stages.

Stage 1 is implemented: all 22 local transport fault checks pass, saved reports survive restart, and RF results remain separate. Use `/diagnostics` → **Run local fault checks**. Its ESP32 test workspace is 4744 bytes. Stage 2 now adds **Arm RTCM fault test** using the same code/duration on both units and fixed SiK Base → Rover traffic. Firmware 0.10.1 is flashed to both. Run 913225 recovered 19/20 eligible messages with zero invalid sink output: delivery did not pass. Reports survived restart; no real correction forwarding is connected and UM980s remain disconnected. See `tests/2026-09-12-correction-pair/README.md` for evidence and next diagnostic work.
