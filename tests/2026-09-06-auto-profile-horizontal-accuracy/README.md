# Test: Automatic Profiles and Horizontal Accuracy

- **Date:** 2026-09-06
- **Purpose:** Remove touch-dependent receiver setup, add role-aware base labels, and validate UM980 `BESTNAVA` horizontal-accuracy parsing on both physical units.
- **Firmware:** `firmware/um980-display-demo/`

## Changes Under Test

- Unit A automatically applies temporary base mode, the six-message RTCM profile, GGA, BESTNAV, and a role query after the UART handshake.
- Unit B automatically applies survey-rover mode, GGA, BESTNAV, and a role query after the UART handshake.
- GGA quality 7 in verified base mode is displayed as `BASE LOCKED`.
- `H-ACC` is horizontal 1DRMS from the root-sum-square of BESTNAV latitude and longitude standard deviations.
- `H-ACC` is hidden without a valid fix and shown as `N/A (BASE)` for the locked autonomous base.
- The former touch-to-configure action is removed; the safe USB console remains available for recovery.

## Bench Results

| Check | Unit A | Unit B |
|---|---|---|
| Build | PASS - RAM 47,660 bytes; flash 779,601 bytes | PASS - RAM 47,668 bytes; flash 778,365 bytes |
| Flash integrity | PASS - upload hash verified | PASS - upload hash verified |
| Automatic role | PASS - receiver read back `MODE BASE TIME 60 2.5 3.5` | PASS - receiver read back `MODE ROVER SURVEY` |
| `BESTNAVA COM2 1` output | PASS - live records at approximately 1 Hz | PASS - live records at approximately 1 Hz |
| Unicore ASCII CRC-32 parser | PASS - `accuracy?` reported 24 parsed records | PASS - `accuracy?` reported 24 parsed records |
| No-fix handling | PASS - `usable=NO`; display value suppressed | PASS - `usable=NO`; display value suppressed |
| Wi-Fi peer link | PASS | PASS |
| Real mm/cm/m value under GNSS fix | NOT TESTED - no sky view | NOT TESTED - no sky view |
| `BASE LOCKED` visual state | NOT TESTED - no sky view | Not applicable |

No `SAVECONFIG` command was sent. Unique hardware identifiers are intentionally omitted.

## Conclusion

The automatic A/B role behavior and CRC-validated BESTNAV parser pass on both physical units. The next open-sky run should confirm the displayed `H-ACC` scale changes appropriately from metres or centimetres to millimetres as the rover progresses toward RTK fixed, and that Unit A displays `BASE LOCKED` after its temporary base coordinate is established.
