# Two-Sided Open-Sky SD Test

> Status: PASS for the complete base-to-rover correction path using the HA-609 antennas. The result validates functional RTK operation and reported receiver uncertainty; independent survey accuracy and repeatability remain future work.

## Setup

| Item | Value |
|---|---|
| Date | 2026-09-07 |
| Base | Unit A, 16 GB `UNIT-A` card |
| Rover | Unit B, 8 GB `UNIT-B` card |
| Antennas | HA-609 permanently installed in both printed bodies |
| Logging | Independent SD sessions on both units |

## Unit A base log

Session `BOOT-2702` contains 2,013 one-hertz solution rows from 05:30:59 to 06:04:33 UTC. The event sequence was:

```text
LINK CONNECTED   05:30:59
RTCM ACTIVE      05:31:00
BASE SURVEY      05:31:01
BASE LOCKED      05:32:05
```

The base ended with 22 satellites, HDOP 0.70, and reported position `20.76660681, -103.40839401`, altitude `1625.519 m`. RTCM counters reached 8,456 UART frames and 8,456 Wi-Fi transmissions. Wi-Fi sequence gaps and invalid packets were zero; fixed-state RSSI averaged approximately -59.6 dBm.

## Unit B rover log

Session `BOOT-2384` contains 2,013 one-hertz solution rows from 05:30:38 to 06:04:11 UTC. The rover reached `RTK FIXED` at 05:31:11 UTC and remained fixed for 1,980 samples.

During the fixed interval:

- H-ACC ranged from 1.5 to 2.4 cm (1.5 cm average).
- Satellites ranged from 15 to 27 (25 average).
- RTCM reception reached 8,362 frames and 1,301,176 forwarded bytes.
- Wi-Fi sequence gaps and invalid packets were zero.
- RSSI averaged approximately -59.8 dBm.

The final base/rover coordinates imply an antenna separation of approximately 3.3 m, consistent with the bench placement. This is an inference from logged coordinates, not a surveyed baseline.

## Interpretation and limits

This proves the complete functional chain:

```text
UM980 base → ESP32 UART → Wi-Fi → ESP32 rover → UM980 rover → RTK FIXED
```

H-ACC is the receiver's estimated horizontal uncertainty, not an independent accuracy measurement. A control-point comparison and repeated occupations are still required before calling the equipment survey-validated.

The logs also show duplicate CSV headers in reused `BOOT-*` folders because the current folder name is based only on startup milliseconds. Session naming should be made collision-proof before longer field campaigns.
