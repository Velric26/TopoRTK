# Test: HA-609 Wi-Fi RTK Open-Sky Checkpoint

- **Date:** 2026-09-06
- **Purpose:** Verify that live open-sky observations and RTCM corrections carried over the ESP32 Wi-Fi bridge produce an RTK solution at the rover.
- **Antennas:** HA-609 compact GNSS antennas
- **Firmware:** `firmware/um980-display-demo/`

## Configuration

| Instrument | UM980 role | ESP32 network role |
|---|---|---|
| Unit A | Temporary self-optimizing base | Wi-Fi access point and RTCM sender |
| Unit B | `MODE ROVER SURVEY` | Wi-Fi station and RTCM receiver |

Unit A used the temporary `MODE BASE` workflow and the volatile COM2 RTCM profile documented in `docs/gnss-rtcm.md`. This was a functional test, not a controlled survey occupation.

## Observed Results

| Check | Result | Evidence |
|---|---|---|
| Both UM980 UART links | PASS | Both displays showed `UART RECEIVING` and `UM980 OK` |
| Unit A base role | PASS | Display showed `BASE` |
| Unit A self-optimization | PASS | GGA changed from standalone GPS fix to quality 7 after the base coordinate was established |
| Base RTCM generation | PASS | Unit A RTCM counter increased; zero bad RTCM frames and zero NMEA checksum errors were shown |
| Wi-Fi correction link | PASS | Unit A showed one client and a peer; Unit B showed Wi-Fi up with approximately -59 to -63 dBm RSSI |
| Rover correction reception | PASS | Unit B RTCM counter increased from 691 to 981; zero bad RTCM frames and zero NMEA checksum errors were shown |
| Rover RTK solution | PASS | Unit B showed `RTK FIXED` with 27-28 satellites and HDOP 0.5 |
| Visible `RTK FLOAT` transition | NOT OBSERVED | Not a failure: the receiver may resolve integer ambiguities between the display's one-second GGA updates |
| Absolute coordinate accuracy | NOT TESTED | The temporary autonomous base coordinate was not tied to known survey control |
| Repeatability and fix recovery | NOT TESTED | Only one successful outdoor session is documented |

Exact coordinates and the location-revealing field photographs are intentionally not copied into the repository.

## Interpretation

The rover's `RTK FIXED` state proves that the end-to-end path was functional in this session: Unit A generated usable observations, the ESP32 bridge transported valid RTCM frames over Wi-Fi, Unit B delivered them to its UM980, and the rover resolved carrier-phase integer ambiguities.

The base display's then-current `MANUAL` text represented NMEA GGA quality code 7, which the UM980 manual calls `Manual input mode`. In this test it appeared after the receiver's self-optimizing base process selected and held an averaged coordinate; it did not mean that the operator manually entered the coordinate. Subsequent firmware changed this role-aware state to `BASE LOCKED` while retaining raw GGA quality code 7 in diagnostics.

## Conclusion

**Functional result: PASS.** This is the project's first documented RTK fixed solution over the ESP32 Wi-Fi RTCM bridge.

It does not establish survey accuracy. The next controlled test should measure time to first RTK fixed, remain fixed for a defined interval, interrupt and restore corrections to measure recovery, and compare repeated rover observations against a known point or established survey receiver.
