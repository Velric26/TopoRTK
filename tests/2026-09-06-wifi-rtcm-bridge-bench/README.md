# Test: Wi-Fi RTCM Bridge Bench Checkpoint

- **Date:** 2026-09-06
- **Purpose:** Install and exercise a CRC-validated RTCM-over-Wi-Fi path, configure volatile UM980 RTCM output, and identify the prerequisites for live corrections.
- **Firmware:** `firmware/um980-display-demo/`

## Configuration

Both ESP32s and both UM980 carrier boards were powered by separate PC USB connections. Both 5 V interconnection wires were removed. The validated TTL2 TX/RX/GND wiring remained installed in each instrument.

| Instrument | ESP32 USB during this run | Receiver role at final query | Wi-Fi role |
|---|---|---|---|
| Unit A | COM4 | `MODE BASE TIME 60 2.5 3.5` | Access point and RTCM sender |
| Unit B | COM10 | `MODE ROVER SURVEY` | Station and RTCM receiver |

USB port numbers can change. Unique hardware identifiers are intentionally omitted.

## RTCM Profile Sent to Unit A

```text
RTCM1006 COM2 10
RTCM1033 COM2 10
RTCM1074 COM2 1
RTCM1124 COM2 1
RTCM1084 COM2 1
RTCM1094 COM2 1
```

Every command was sent through Unit A's allowlisted ESP32 console and acknowledged by the UM980. No `SAVECONFIG` command was sent.

## Results

| Check | Result | Evidence |
|---|---|---|
| Unit A build and flash | PASS | RAM 47,320 bytes; flash 776,461 bytes; upload hashes verified |
| Unit B build and flash | PASS | RAM 47,336 bytes; flash 775,349 bytes; upload hashes verified |
| Existing Wi-Fi diagnostics after bridge change | PASS | Link remained up with `gap=0` and `bad=0` |
| RTCM counters before activation | PASS | Both units reported zero RTCM frames and zero RTCM errors |
| Unit A role persistence | OBSERVED | After the power/reconnection cycle it had returned to default `MODE ROVER SURVEY` |
| Restore temporary Unit A base role | PASS | `MODE BASE` acknowledged and read back as `MODE BASE TIME 60 2.5 3.5` |
| Unit B rover role | PASS | Read back as `MODE ROVER SURVEY` |
| RTCM output commands | PASS | All six commands returned OK responses from Unit A |
| Unit A RTCM UART parsing | BLOCKED | `RTCM_UART=0`; no antenna, satellite observations, or completed base position |
| Unit A-to-B RTCM forwarding | BLOCKED | No source RTCM frames existed to forward |
| Physical bridge counters | PASS | Project owner confirmed Unit A showed `BASE`, `RTCM:0`, `R-BAD:0`; Unit B showed `ROVER`, Wi-Fi up, `RTCM:0`, `BAD:0` |
| Receiver configuration persistence | AVOIDED | No `SAVECONFIG` sent |
| Survey base coordinate | NOT TESTED | Autonomous temporary base has no accepted survey-control value |
| Rover RTK state | NOT TESTED | No live corrections or open-sky rover observations |

The sanitized serial evidence is preserved in [`logs/bench-proof.txt`](logs/bench-proof.txt).

## Conclusion

The firmware build, flash, existing network regression, receiver role control, and volatile RTCM-output configuration pass. Live RTCM production and forwarding are blocked by the current antenna-less indoor setup, not by a detected Wi-Fi or CRC error.

The next live checkpoint requires Unit A and Unit B antennas with sky view. Its first pass criterion is increasing RTCM counters on both displays with zero RTCM errors; RTK `FLOAT` or `FIXED` is a later criterion and must not be inferred from packet delivery alone.
