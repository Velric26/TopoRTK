# Test: ESP32-to-ESP32 Wi-Fi Link

- **Date:** 2026-09-06
- **Purpose:** Validate a directly observable wireless data path between the two ESP32 controllers before carrying RTCM or other GNSS data.
- **Firmware:** `firmware/um980-display-demo/`

## Configuration

| Item | Unit A | Unit B |
|---|---|---|
| PlatformIO environment | `unit_a` | `unit_b` |
| USB port during this run | COM4 | COM10 |
| Wi-Fi role | Access point | Station |
| Network address | `192.168.4.1` | DHCP client |
| Application transport | UDP port `22345` | UDP port `22345` |
| UM980 configuration change | None | None |

Both ESP32s and both BDRTK/UM980 boards were powered from separate PC USB connections. The 5 V interconnection wire was removed in both instruments. Each instrument retained TTL2 TX, RX, and common ground between its ESP32 and BDRTK board.

The compiled SSID and password are test credentials, not production security. Unique device identifiers are intentionally omitted from this record.

## Protocol

1. Unit A starts the test access point and UDP listener.
2. Unit B joins the access point and sends periodic checksummed handshake packets.
3. Unit A learns Unit B's address from a valid handshake.
4. Unit A sends a checksummed test packet every 250 ms with an increasing sequence number.
5. Unit B validates packet size, marker, protocol version, type, checksum, and sequence continuity.
6. Both units report counters through their displays and native USB logs.

No NMEA, RTCM, receiver command, coordinate, or survey data was transported during this checkpoint.

## Results

| Check | Result | Evidence |
|---|---|---|
| Unit A build | PASS | PlatformIO build completed; RAM 46,248 bytes, flash 774,417 bytes |
| Unit B build | PASS | PlatformIO build completed; RAM 46,264 bytes, flash 773,529 bytes |
| Unit A flash | PASS | Upload completed and flash hashes verified on COM4 |
| Unit B flash | PASS | Upload completed and flash hashes verified on COM10 |
| Station association | PASS | Unit A reported one client; Unit B reported link `UP` |
| Bidirectional discovery | PASS | Unit A received repeated Unit B handshakes and reported peer `YES` |
| A-to-B packet delivery | PASS | Unit B's received counter increased through packet 84 |
| Sequence continuity | PASS | Unit B reported `gap=0` throughout the observation |
| Application integrity | PASS | Both units reported `bad=0` throughout the observation |
| Bench signal | OBSERVED | Unit B reported approximately -47 to -53 dBm; not a range test |
| Physical display counters | PASS | Project owner confirmed both displays showed the expected increasing counters with zero gaps/errors |
| RTCM forwarding | NOT TESTED | Deliberately excluded from this incremental checkpoint |

The sanitized USB evidence is preserved in [`logs/wifi-proof.txt`](logs/wifi-proof.txt).

## Conclusion

The test-only Wi-Fi transport passes its initial bench checkpoint. It proves association, peer discovery, bidirectional handshake traffic, and checksummed/sequenced Unit A-to-Unit B application packets.

The physical display checkpoint is complete. The next change may add a CRC-validated RTCM frame path while retaining the synthetic link diagnostics. RTCM configuration, throughput, packetization, UART backpressure, reconnect behavior, base coordinates, and RTK state remain separate validation checkpoints.
