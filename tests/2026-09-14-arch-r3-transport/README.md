# R3 transport ownership checkpoint — 0.11.14-arch-r3 deployed to both units (2026-09-14)

Implementation of the [architecture review](../../docs/architecture-review.md) R3 checkpoint: one physical radio/UDP owner, network lifecycle isolated from corrections/GNSS. Both instruments now run **`0.11.14-arch-r3`** via guarded OTA with verified boots and unchanged saved survey state.

## Contract implemented

- **`radio_transport.h/.cpp`** — sole owner of UART2 (57600 8N1, RX18/TX17, 4096/1024 buffers, receive-error counters, paired-run observation counters). One fixed 256-byte `Decoder` covers RTM1, RTC1 and RTDG envelopes: CRC-valid envelopes are consumed whole (a nested RTC1/RTDG inside a valid RTM1 payload cannot split a frame), bad-CRC candidates resync by one byte and count their family, unknown prefix bytes count as discarded. `send` reports −1 on TX backpressure. Header-only `Decoder` is Arduino-free and natively tested.
- **`wifi_transport.h/.cpp`** — single owner of the three UDP channels (corrections 22345, diagnostics 22346, peer 22347). One datagram per `receive` (0 = none, −1 = discarded oversized/short with the remainder drained, positive = complete size, sender captured before read); whole-datagram `send`; `stop_all` on network restart with lazy per-consumer restart independent of radio state.
- **`network_service.h/.cpp`** — sole owner of the AP/STA lifecycle: mode switching, direct Base AP (identity/geometry unchanged), local-router station, 10 s reconnect timer, subnet/broadcast helpers, rover-AP lifecycle calls. `restart` snapshots only role and Wi-Fi mode and performs **no** correction/receiver/survey/UI mutations; `main.cpp` composes those explicit hooks.
- **Consumer migration**: `main.cpp` (`start_wifi` → `network_service::restart` plus explicit link-state resets; packet RX/TX and helpers via the owners; direct `WiFi.*`/`WiFiUDP`/rover-AP calls removed), `link_diagnostic.cpp` (private `HardwareSerial(2)`/`WiFiUDP` removed; live bridge, probe dialog, paired and diagnostic traffic via `radio_transport`; Wi-Fi diagnostic leg via `wifi_transport`), `peer_update.cpp` (socket via the Peer channel), `rover_ap.cpp` (`stop` no longer touches the Wi-Fi mode — it uses `softAPdisconnect`, keeping mode ownership with the network service). Wi-Fi v2 packet formats, all ports, and correction safety gates are unchanged.

## Defects caught and fixed during integration

1. **Host-double sharing**: `static` globals in `test/host_hardware.h` gave every translation unit its own `WiFi`/`Serial`/`host_now`, so service mutations were invisible to the firmware tests (the failing Base-mode assertion was this, not a production bug). Fixed with `extern` declarations plus `test/host_hardware.cpp` single definitions; the proper `WIFI_AP`/`WIFI_AP_STA` assertions were restored, not weakened.
2. **Live control routing**: peer control rides RTM1 type-3 envelopes, so the decoder's `Rtcm` family plus `peer_wire::control` decides — not the frame kind. The paired engines now receive each validated envelope's full 256 bytes in wire order (a first draft fed only the first byte).
3. **Diagnostics error accounting**: raw-byte framing surfaced corruption as engine errors; the framer consumes bad bytes internally, so `engine.errors` now adds the per-service-turn delta of the decoder's diagnostic-error + discarded-byte counters while busy (report semantics preserved).
4. **Loop reconstruction losses**: the reworked `loop()` initially dropped `service_survey()` and the `network_service` service call; Unit B briefly ran a build whose survey snapshot reported the `Fix::unit` default `'?'`. The OTA runner's identity assert caught it (`'?' !== 'B'`); `service_survey()` was restored and the fixed image versioned `0.11.14` to distinguish it from the briefly-flashed `0.11.13`.
5. **Restored lost definitions**: `correction_radio_session()`, `peer_update_quality_ready()`, `ota_reset_corrections()`, and the `locked/persisted/persisted_peer`/probe declarations.

## Software verification

- `test/radio_framing_cases.cpp` (now wired into `run_host_tests.py`): family isolation, nested-envelope containment, garbage resync, per-family bad-CRC counters, fragmentation — PASS.
- Full host suite: all 7 blocks PASS, including the frame-buffer "navigated page equals clean render" and unchanged-render checks.
- `pio run -e unit_a -e unit_b`: both SUCCESS. Packages: 1,242,448 bytes, identity-checked, version `0.11.14-arch-r3`.

## OTA deployment (guarded runner, sequential)

| Unit / role | Final firmware | Accepted boot | Result |
|---|---|---|---|
| A / Base | `0.11.14-arch-r3` | verified, Debug On | PASS: acknowledged peer notices, unchanged saved survey state |
| B / Rover | `0.11.14-arch-r3` | verified, Debug On | PASS on re-run; first fixed-image run completed but its `savedStateUnchanged` assert flagged the `'?'`→`'B'` baseline transition caused by defect 4 |

Two earlier Unit A upload attempts (both still on `0.11.12`, receiving side old firmware) aborted mid-transfer — `ERR_CONNECTION_RESET` at 238 KB and 109 KB, then a clean run after the operator's power cycle. Retained-firmware safeguards worked exactly as documented; environmental, not a firmware defect. Unit B's re-flash after defect 4 used the documented manual API workflow (takeover → prepare with peer ack confirmed → start → upload → boot verification); the runner's identity assert had correctly refused an instrument whose snapshot reported `'?'`.

## Radio-path hardware validation (bench, indoor)

Through the new `radio_transport` on both units:

- **ATI probes**: `RFD SiK 2.0 on HM-TRP` identified on both (`run_sik_bench.py` probe dialog).
- **Paired synthetic test** (30 s, clean profile, RTM1-shaped frames over the live SiK air interface): Base 15/15 sent, Rover 15/15 received, zero errors, zero integrity violations, `pair_pass` **TRUE on both**.
- **Live session select/join**: Base created session 922259236, Rover joined; control envelopes flowed through the new owner. One transient route rejection ("Finish the current test…") occurred when the selection request raced the pairtest-completion release — a pre-existing ordering property, resolved by retry; not an R3 regression.
- **Live RTCM forwarding not exercised**: the Base UM980 produced zero RTCM (no fix, no base reference — indoor bench without a usable observation source), so `submitted`/`envelopes` stayed at 0. This matches the standing **outdoor SiK acceptance gate**, which remains the independent receiver proof and is unaffected by this checkpoint.

Both instruments were restored to Wi-Fi corrections (`transport: wifi`, session 0, no errors) after the bench run.

## Outstanding

- Direct-sunlight readability acceptance on both physical panels (R2b human gate, unchanged).
- Outdoor SiK RTK acceptance and the R3-specific live-forwarding-under-loss/STA-bounce checks (independent gates per the review; the indoor bench could not produce an RTCM source).
- Firmware-folder restructure: planned and deferred per [the restructure plan](../../docs/firmware-folder-restructure.md).
