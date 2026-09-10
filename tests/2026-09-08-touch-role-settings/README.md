# Touch role selection, saved configuration, and GUI redesign

- Date: 2026-09-08, project local time.
- Firmware: `firmware/um980-display-demo`, Unit A and Unit B builds.
- Connected instrument: Unit B ESP32 on COM10, existing UM980 TTL2 wiring.
- Scope: runtime Base/Rover selection, Wi-Fi direction, ESP32 NVS persistence, acknowledged receiver profiles, and redesigned touchscreen navigation.
- No `SAVECONFIG` command was sent. No antenna/fix or survey accuracy claim is made.

## Procedure and evidence

The configuration path was built, flashed, and exercised before adding the touchscreen redesign. Console selections use the same configuration functions as touchscreen Apply/brightness controls. Unit B was changed from Rover to Base and back; both modes returned checksum-valid receiver readback and the corresponding AP/station state. The six base RTCM output commands were acknowledged.

Saved Base/Night/RTCM-on, Base/Night/RTCM-off, and Rover/Auto/RTCM-on were each restored across an ESP32 hardware reset while the UM980 stayed powered. Logs are in [config-bench-pass.txt](logs/config-bench-pass.txt). An initial reset harness attempt did not capture a reboot because it omitted the Windows serial DTR update; its partial observations are retained in [config-bench.txt](logs/config-bench.txt). This harness issue was corrected using the Espressif reset sequence.

The first profile trial identified that short UM980 command replies include the leading `$`/`#` in their XOR checksum. The firmware now validates these separately from NMEA and BESTNAV. A further trace exposed a redundant reconfiguration immediately after a short profile completed before the next GGA. A two-second post-profile grace period addresses that case and is covered by a production-code host test.

The GUI was then built and flashed to Unit B. The final version was built for both units. The host checks execute the production profile and touch handlers plus actual screen drawing functions. Previews use synthetic values and are not physical LCD photographs.

The [final firmware console](logs/final-firmware-console.txt) reports saved Rover/Auto, a verified profile, and increasing peer packets with zero displayed gaps or invalid packets. Unit B was left in this state. Layout previews: [Home](previews/home.png), [Setup](previews/setup.png), and [unapplied Base selection](previews/base-selection.png).

## Results

| Check | Result |
|---|---|
| Unit A / Unit B final builds | PASS; each uses 51,812 bytes RAM and 891,193 bytes flash |
| Unit B flash | PASS; upload hash verified |
| Unit A flash | NOT RUN; Unit A was not connected by USB |
| Unit B runtime Base/Rover and Wi-Fi AP/station reversal | PASS via shared console/configuration path |
| UM980 command acknowledgements and role readback | PASS on hardware |
| Saved role, brightness, and RTCM flag across ESP32 hardware resets | PASS on hardware |
| GPIO6 PWM brightness, 5 kHz hardware readback, and clear Day/Night change | PASS on Unit B and Unit A; Night duty 26/255 (~10%) |
| No repeated NVS write for unchanged brightness | PASS, host assertion and hardware console |
| NVS write failure preserves previous active configuration | PASS, simulated storage failure |
| Invalid settings/schema fallback | PASS, host checks |
| Corrupt receiver reply, bounded timeout, retry | PASS, host checks |
| Touch navigation, staged role choice, Apply, brightness | PASS with simulated FT6336 samples; physical taps pending |
| Drag, long press, multi-touch, and I2C error cancellation | PASS, host checks |
| Text/screen bounds and unchanged-region repaint caching | PASS, production renderer with hardware doubles; previews visually inspected |
| Display, touch-controller discovery, SD mount/readback | PASS on Unit B initialization |
| Full instrument power-off/on | PENDING; resets are not full power removal |
| Physical touch alignment/readability and rotation 2 | PENDING |
| Reversed A=Rover/B=Base pair under RTK | PENDING |

## Remaining physical checks

1. On Unit B, open Setup, select Base, and tap Use Base. Confirm the screen reports Base Active and Link details show Access Point. Repeat for Rover and confirm Station.
2. Confirm selecting a role without tapping Use does not change the instrument; leaving Setup discards the selection.
3. Select each brightness mode and verify gradual brightness changes. With Auto and no GNSS time, full brightness should be retained.
4. Fully power down and restart the instrument. Confirm the selected role and brightness restore without touching the screen. Autonomous Base should start a new temporary survey-in, not reuse a saved control coordinate.
5. Flash Unit A and repeat with opposite roles on both units. Validate fresh RTCM, fixed recovery, stale-link warnings, and SD records in open sky.

The two instruments must be set to opposite roles manually. Both units set to Base or both set to Rover will not establish a correction link.

At the final 2026-09-08 check, both units reported `GNSS TIME: INVALID OR STALE`. Auto therefore correctly selected its documented full-brightness safety fallback even though local wall-clock time was 23:18. Use `Night` when GNSS time is unavailable; once a valid fresh GNSS clock is available, Auto will apply the night schedule.
