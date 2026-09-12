# SiK radio configuration and USB bench test

Date: 2026-09-11 America/Mexico_City (capture timestamps use UTC, 2026-09-12).

## Scope

The owner authorized configuration, documentation and backup of the two USB-connected Holybro 1 W radios. No ESP32 application or UM980 firmware/configuration was changed. This is a radio-only bench checkpoint, not an RTCM, RTK accuracy, outdoor range or electrical UART acceptance test.

## Device identity

| USB port at test | FTDI USB serial | VID:PID | Radio firmware | ATI2 / ATI3 / ATI4 |
|---|---|---|---|---|
| COM14 | DU0EUNGIA | 0403:6015 | RFD SiK 2.0 on HM-TRP | 78 / 145 / 1 |
| COM15 | DU0EULJ8A | 0403:6015 | RFD SiK 2.0 on HM-TRP | 78 / 145 / 1 |

These USB identities identify the current USB interface. Label the physical radios before moving cables; COM numbers alone are not permanent identities. Neither has been assigned to an instrument letter or Base/Rover role. No camera is installed or planned on either ESP32, per owner confirmation.

## Backups and changes

- `sik-radio-backup.zip`: portable copy of this record, before/after snapshots, test evidence, radio documentation and scripts. `SHA256SUMS.json` inside the ZIP records hashes of its payload files. `sik-radio-backup.sha256` verifies the ZIP itself.
- `before/COM14.json` and `before/COM15.json`: original full parameter reports, identity and RSSI diagnostics, captured before any setting write.
- `applied/COM14.json` and `applied/COM15.json`: pre-change snapshot, setter/save/restart responses, and full post-restart parameter comparison.
- `after-first-test/`: unchanged settings and radio diagnostics after the initial failed data test.
- `binary-link-test.json`: original 3000 bytes/second test result; failure retained.
- `binary-link-low-rate.json`: lower-rate diagnostic, including both single directions and simultaneous bidirectional traffic.

Both radios already matched at 115200 serial, 64 kbps air, NETID 25, ECC 0, RTSCTS 0, MAX_WINDOW 131, and 915000-928000 kHz with 50 channels. See the [complete configuration table](../../docs/radio.md).

The initial changes, independently on each radio, were:

| Parameter | Original | New | Command |
|---|---:|---:|---|
| TXPOWER | 11 | 1 | ATS4=1 |
| MAVLINK | 1 | 0 | ATS6=0 |

Each change was acknowledged and the full parameter table checked before the next change. `AT&W` was acknowledged, `ATZ` restarted the radio, and a fresh readback matched every intended and preserved parameter. Save/software-restart verification: **PASS on both radios**. Physical power-cycle verification is still pending. No firmware upgrade, factory reset, FORMAT change or regional RF parameter change was performed.

On the selected Holybro 1 W variant, manufacturer mapping gives approximately 12.5 mW for value 1 and 125 mW for value 11; RF power was not independently measured. [Holybro reference](https://docs.holybro.com/radio/sik-telemetry-radio-v3/rf-transmission-power-setting-for-1w-variants).

The first command-mode probe did not obtain an acknowledgement from a radio that appeared to be in command mode already. A CR-terminated identity query succeeded. The tool now uses CR-only commands and clears a partial line before retrying AT. No parameter write occurred during that probe.

## Validation

The synthetic test uses numbered 272-byte frames containing every byte value, paced over 115200-baud USB serial with software/hardware flow control disabled. Exact byte comparisons and SHA-256 hashes detect loss, insertion, corruption and reordering; these are not actual RTCM frames. USB success would not qualify the six-pin TTL interface.

The first COM14 -> COM15 run offered 3000 bytes/second and sent 200 frames (54,400 bytes). Only 53,479 bytes arrived: **FAIL**, 921 bytes missing by length, hashes unequal. It stopped before testing the reverse direction. The first harness version did not retain raw failed bytes; its JSON is the retained evidence.

Diagnostics afterward showed COM15 `rxe=6`, COM14 `rxe=0`, and both `stx=0 srx=0`. Strong reported RSSI does not establish a clean link. This supports investigating radio reception/bench placement; it does not conclusively identify the cause or prove the absence of all host-side loss. The subsequent lower-rate test provides additional evidence. Raw failed captures, when present, are named in the JSON results.

The lower-rate run sent 50 frames per active direction. Both single-direction phases passed at 1000 bytes/second (13,600 bytes each, exact hashes). Simultaneous traffic at 500 bytes/second per direction failed: COM14 received 13,595/13,600 bytes and COM15 received 13,599/13,600. `low-rate-byte-differences.json` shows five isolated byte deletions at COM14 and one at COM15; there were no other differences in those captures. The reported radio receive-error counters did not increase between the two diagnostics snapshots, and reported serial overflow counters stayed zero. This leaves serial/USB handling and radio firmware behavior open as possible causes; it is not evidence of a proven RF-only fault.

The owner confirmed the antennas were less than one metre apart. Cables limited the requested separation to approximately **60 cm**. The repeat at that spacing (`repeat-60cm-115200.json`) still failed with 100 frames per active direction: at 3000 bytes/second, COM15 received 27,037/27,200 bytes and COM14 received 26,676/27,200. Simultaneous 1500 bytes/second per direction delivered 26,799 bytes to COM14 and 26,794 to COM15, also short of 27,200. Failed raw captures are retained. This does not rule out close-range RF effects.

The next comparison changes only the radio serial speed to 57600 (`SERIAL_SPEED=57`), retaining all RF parameters and the same payload rates. The `applied-57600/` records capture the original state, change/save/restart commands and readback for this comparison. Firmware was not changed.

Radio acceptance remains pending until the recorded failures are resolved and the target RTCM traffic is tested. The ESP32 bridge, actual correction freshness/recovery, and field RF qualification remain separate work.

## Restoring the original settings

The backup is a parameter backup, not a firmware image. The radio firmware was not changed. For the exact devices above, restoring the three changed values (including the subsequent serial-rate diagnostic) restores the original configuration because every other parameter was preserved and checked.

1. Connect to the identified radio at the current **57600, 8-N-1**, with flow control disabled. Close any other serial application using that port.
2. Keep the serial input quiet for at least one second, send `+++` without a line ending, then wait at least one second. Confirm command mode and use `ATI` / `ATI5` to check the device against its saved snapshot.
3. Send each command separately, terminated by **CR**, and check each setter/save acknowledgement:

```text
ATS4=11
ATS6=1
ATS1=115
AT&W
ATZ
```

4. Reconnect the host at **115200 baud** after the restart, re-enter command mode and compare the complete `ATI5` report with `before/<port>.json`, then send `ATO` to return to transparent mode. Repeat locally for the second radio. Restoring MAVLINK=1 restores the old setup; it is not the new transparent-RTCM profile.

Do not send a blanket factory reset or write FORMAT. If identity, firmware or unrelated parameters have changed, review the full backup before restoring anything. Rollback instructions were reviewed against the captured register map but were not executed, since that would undo the requested configuration.

## Reproduction tools

The scripts require Python and `pyserial`. This PC used `C:/Users/el_sp/.platformio/penv/Scripts/python.exe`. Run from the repository root. Choose new output paths; snapshots refuse to overwrite existing evidence.

```text
python tools/sik-radio/bench.py snapshot --ports COM14 COM15 --output <new-snapshot-directory>
python tools/sik-radio/bench.py apply-bench --ports COM14 COM15 --backup <original-snapshot-directory> --output <new-application-directory>
python tools/sik-radio/link_test.py --ports COM14 COM15 --identity-backup <snapshot-directory> --output <new-test.json> --rate 3000 --frames 200
```

`set-baud --baud 115200 --target-baud 57600` (or the reverse) changes only serial speed against a fresh matching backup, saves/restarts and reads back at the new speed. Use `--baud 57600` for snapshots and link tests with the current configuration. `build_backup.py` regenerates and verifies the portable ZIP.

`apply-bench` is deliberately limited to the reviewed profile and two setters above. It checks USB identity, firmware and a fresh full parameter readback against both backups before applying the per-device changes. It leaves an incremental journal if verification fails; do not rerun blindly after a partial failure. `link_test.py` verifies USB identity but assumes both radios have already been placed in transparent mode and their settings verified.

Primary protocol reference: [SiK command set and serial/air-rate behavior](https://ardupilot.org/copter/docs/common-3dr-radio-advanced-configuration-and-technical-information.html).

## Current checkpoint after the 60 cm comparison

Both radios remain saved at **57600 baud**, transparent mode (MAVLINK 0), TXPOWER 1 and AIR_SPEED 64. The GNSS and ESP32 application were not changed. Current full readbacks are in `current-57600/`.

The lower baud did **not** resolve the failure. Each active direction expected 27,200 bytes:

| Phase | Receiver | Received bytes | Exact match |
|---|---|---:|---|
| first_to_second | COM15 | 26942 | False |
| second_to_first | COM14 | 27200 | False |
| simultaneous_bidirectional | COM14 | 26542 | False |
| simultaneous_bidirectional | COM15 | 27200 | True |

One phase had the correct byte count but a different hash; counting bytes alone would have falsely passed it. The cause is unresolved. Possible follow-ups are greater antenna separation using suitable USB extensions, checking power/cables, and isolating the radio TTL interface from USB. These are diagnostic options, not established causes. Do not treat either tested baud as a qualified field link. Stop further configuration experiments at this checkpoint, preserve the failure captures and resume the bounded investigation later.
