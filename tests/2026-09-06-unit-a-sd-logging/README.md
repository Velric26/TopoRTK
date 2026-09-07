# Unit A SD Logging Checkpoint

> Status: PASS for Unit A SD_MMC mount and read-back; session logging is enabled for continued observation. Raw GNSS/RTCM archival is intentionally deferred.

## Configuration

| Item | Value |
|---|---|
| Date | 2026-09-06 |
| Unit | A / temporary base |
| ESP32 USB port | COM4 |
| SD card label | `UNIT-A` |
| Filesystem | FAT32 |
| SD_MMC mode | 1-bit |
| Pins | GPIO9 D0, GPIO10 CMD, GPIO11 CLK |

## Evidence

- PlatformIO `unit_a` build passed with SD_MMC and FS libraries.
- Upload to the ESP32-S3 on COM4 completed with flash hash verification.
- Controlled reset reported `SD: MOUNT PASS card=14832 MB free=14822 MB`.
- The firmware reported `SD: READBACK PASS` for its project-owned test file.
- Unit A continued the UM980 startup handshake and applied its volatile base profile after the SD test.
- The session logger created its project directory and began recording boot/state/profile/solution files; the automatic profile commands were acknowledged by the UM980.

## Files created by the firmware

```text
/TOPO-RTK/SD-READBACK-TEST.TXT
/TOPO-RTK/UNIT-A/SESSIONS/BOOT-<uptime>/session.json
/TOPO-RTK/UNIT-A/SESSIONS/BOOT-<uptime>/events.csv
/TOPO-RTK/UNIT-A/SESSIONS/BOOT-<uptime>/config.csv
/TOPO-RTK/UNIT-A/SESSIONS/BOOT-<uptime>/solution.csv
```

The test file is deliberately limited to a known project path. No reformat, recursive delete, or unrelated card content change was performed.

## Remaining validation

- The card was safely removed from Unit A and inspected on Windows as drive `F:`. The newest session contained all four expected files and readable CSV/JSON records.
- The inspected event log contained `SD_READY|READBACK_PASS`, `CONFIG_PROFILE,BASE_TEST`, `ROLE,BASE`, and a later `LINK,CONNECTED` event.
- The inspected solution log contained valid UTC timestamps but no position fix while indoors; this is expected and is not an SD failure.
- The follow-up build adding RTCM UART/Wi-Fi/forwarded-byte counters and an `RTCM ACTIVE/INACTIVE` event was flashed to Unit A after the card was reinserted. The controlled boot again reported `SD: MOUNT PASS card=14832 MB free=14822 MB` and `SD: READBACK PASS`.
- During the current live check, Unit A first reported `clients=0`, `NO FIX`, and `RTCM_UART=0`; after Unit B rejoined, it reported `clients=1`, `peer=YES`, with zero Wi-Fi packet errors. `RTCM_UART=0` remains expected until the base obtains a valid GNSS solution and begins its configured correction stream.
- Unit B's USB port is not currently enumerated on the PC, but the Unit-A AP confirms one connected peer. This is sufficient for the wireless-link portion; it is not evidence of a rover fix.
- PC read-back later succeeded from the `UNIT-A` FAT32 card on drive `F:`. Session `BOOT-2696` contains a 725-row GNSS/RTCM segment lasting about 12.2 minutes: `NO FIX` → `BASE WAIT` → `BASE SURVEY` → `BASE LOCKED`. The base reached `BASE LOCKED` with 28 satellites and HDOP 0.50 at `20.76661852, -103.40838294`, approximately 1,629.434 m altitude.
- In that segment, `RTCM_UART` increased from 0 to 3,009 frames and `RTCM_WIFI_TX` from 0 to 2,977 frames. Wi-Fi sequence gaps and invalid packets remained zero; nonzero RSSI samples ranged from approximately -84 to -45 dBm. This confirms base-side correction generation and transmission, but not rover-side receipt or RTK fix because Unit B was not logging to this card.
- The card also shows an earlier short pre-fix segment in the same `BOOT-2696` directory. Duplicate CSV headers indicate the current session naming (`BOOT-<millis>`) can reuse an existing directory after a reboot; this should be corrected before treating session folders as independent field records.
- FAT directory timestamps currently appear as 1980 because the firmware has not yet set FAT file timestamps; GNSS UTC is recorded inside the log rows and is the authoritative time.
- Reinsert the card and power-cycle Unit A before the open-sky session so it mounts cleanly.
- Verify records survive a normal restart and document behavior after sudden power removal.
- Add raw RTCM and UM980 observation files only after measuring data rate and defining retention rules.
