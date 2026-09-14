# First live Wi-Fi OTA attempt — Unit B, 2026-09-14

**Result: transfer failed; running firmware retained. No successful OTA reboot or rollback claim.**

Both ESP32s were USB-installed at 0.11.0 before this test. The user enabled Debug physically on Unit B and authorized the Wi-Fi test. Unit A remained Debug Off; it did not need Debug enabled to receive peer status. Both used the local router, with UM980s connected but no sky view. No survey collection was active.

## Observed sequence

1. The actual Unit B `/debug` page was operated through Edge/Playwright. The existing Unit B package matched the previously validated SHA-256 `1e63022af6cf7a982315d456dcd481bc2b99d12da7504641809b525d7a3e410a` (1,238,768 bytes). No firmware was rebuilt or modified.
2. The page displayed the Rover interruption warning. The package targeted Unit B, and installation remained disabled until the interruption checkbox was selected. The unconfirmed-peer override remained unchecked.
3. Unit A's live status reported `Rover preparing update`, then `Rover updating - reception paused`. Unit B reported peer acknowledgement.
4. The browser initiated the binary upload. Unit B's HTTP service became temporarily unreachable. Passive USB observation showed the main loop later processing GNSS and Wi-Fi traffic; read-only console queries confirmed the router connection and verified receiver profile.
5. When HTTP became reachable, Unit B reported `failed`, `Upload connection lost or timed out`, received 37,336 of 1,238,768 bytes, attempt 1. Boot ID remained 4042490384 with `USB / normal boot`: no OTA reboot occurred. Update lock and pause were both false. Unit A's update notice cleared.
6. Saved survey state matched the pretest snapshot, collection remained inactive, and the GNSS profile remained verified. Debug remained On because no reboot occurred; its normal idle timeout still applies. See `unit-b-result.json`.

The temporary browser harness waited for fixed terminal text and missed the firmware's more specific failure message, so it subsequently hit its 170-second observation timeout and closed its browser. This harness timeout is separate from the firmware upload receive timeout. No second upload, USB recovery flash, forced reset, power interruption or deliberate rollback test was performed.

## Next development work

- Investigate the real upload receive timeout and temporary HTTP unavailability. Capture response/network timing and serial logs together. Inspect request-body cleanup after an early upload rejection and the interaction between browser connections and the three-socket HTTP limit. These are hypotheses, not established causes.
- Improve the test harness to recognize API `failed` state and preserve terminal browser/network evidence immediately, including failed uploads.
- After a targeted fix and regression checks, repeat one controlled OTA with USB recovery available. Then validate restart acceptance, settings retention and peer recovery before testing interrupted uploads and actual rollback.

Keep packages, full flash/NVS snapshots, bearer tokens and raw device state private under ignored `.pio`. Only redacted evidence is committed. This failure does not establish a SiK radio problem: the firmware upload used Wi-Fi.
