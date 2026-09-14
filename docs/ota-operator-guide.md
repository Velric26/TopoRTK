# Debug OTA operator guide — 0.11.1 preview

**Both units installed with 0.11.0 by USB; first Wi-Fi OTA transfer failed safely.** Unit B's browser upload timed out after 37,336 of 1,238,768 bytes. Its boot ID remained unchanged, its update lock/pause cleared, and saved survey state was preserved. Unit A acknowledged preparation and displayed the Rover-updating notice. No successful OTA reboot or hardware rollback has yet been demonstrated. See the [USB evidence](../tests/2026-09-14-ota-usb/README.md) and [first OTA result](../tests/2026-09-14-ota-live/README.md).

**Recovery increment:** Unit B now runs 0.11.1, installed by USB with write/startup verification and saved state preserved. Unit A remains 0.11.0. The new upload handler retries transient receives with a 12-second no-progress limit, retains the 120-second total limit, and closes failed requests without draining their remaining body. Debug browser requests are cancelled/paused during upload. Software checks pass; a new physical Debug enable on Unit B and a real Wi-Fi retry are still required. See [recovery evidence](../tests/2026-09-14-ota-recovery/README.md).

## First installation and later updates

1. Install the appropriate firmware/bootloader on each ESP32 by USB and verify the flashed image and startup. Both units are complete, with USB write hashes and startup verified. Keep USB access available for the first OTA and rollback checks.
2. On each instrument touchscreen, open **Setup → Debug → Enable Debug**. Debug is Off after restart and after 15 minutes without user activity. There is no HTTP enable command and no PIN. The current takeover owner may use private Debug/OTA actions.
3. Connect through the existing local Wi-Fi or instrument hotspot and open **Debug** in the Survey interface. Use the displayed instrument address. The Base in Local Router mode does not gain a new independent phone hotspot from this feature; Direct Link's Base AP remains available. Network provisioning/topology is unchanged.
4. Choose the `.tpk` package for **hardware Unit A or B**, independently of its current Base/Rover role. Select **Review package and notify peer**. This reserves the instrument and rejects active collection, queued survey mutations, receiver setup and diagnostics. Correction forwarding continues during review.
5. Read the role-specific interruptions. Accept the interruption/power checkbox. If peer acknowledgement cannot be confirmed, explicitly choose whether to permit an unconfirmed notification. **Confirm interruption and install** pauses local processing and starts a separate Updating notice. Without the override, a missing final acknowledgement aborts before writing flash.
6. Keep power connected. The browser shows transfer progress, then waits for a new boot and its startup verdict. HTTP/Debug polling pauses during the synchronous upload. A successful transfer alone is not reported as a successful update. If the connection is lost, reconnect and check the outcome before retrying.
7. Debug returns Off after reboot. Enable it locally again if diagnostics are needed. For **SiK**, start a **fresh Base correction session**, then copy it to Rover. The saved old route is restored for peer-status communication only; correction traffic remains blocked until the new session is selected. This avoids resetting sequence/replay protection in the old session. Wi-Fi does not need this manual SiK rejoin.

## Development backups and recovery

A full 16 MB flash backup is **optional**, not a prerequisite for routine development updates. Prefer a small settings snapshot when USB is already available, and preserve the source/build configuration needed to rebuild. Do not perform a lengthy full read before every flash. Normal firmware uploads preserve settings and SD files.

USB reflashing can recover firmware, but cannot reconstruct erased settings or survey data. The current USB snapshot reads 0x7000 bytes starting at 0x9000 (28,672 bytes covering NVS and OTA metadata); verify the partition layout before reusing those offsets with another build. This raw snapshot is not a portable settings export, and its OTA metadata must not be blindly restored over a different firmware layout. SD jobs/files require their own export or backup when needed. Full flash snapshots remain useful before partition changes or investigations that specifically need the old flash contents.

Unit A and B full snapshots were already completed during this installation session. Unit B's small snapshot took 0.7 seconds; its full read finished just before cancellation. Snapshots and compiled packages stay private under ignored `.pio`, since they may contain credentials.

## What pauses

- **Base update:** Base correction forwarding, local GNSS processing, SD logging and USB console processing pause. Base web/Debug access disconnects during reboot. Rover may lose RTK fixed; stale corrections still inhibit collection.
- **Rover update:** collection must be idle. Rover correction admission/forwarding, local GNSS processing, SD logging and USB console processing pause. Web/Debug access disconnects during reboot. Base may continue generating corrections.
- The ESP32 update does not update or power off the UM980 or SiK firmware. Jobs, NVS configuration and SD content are not erased. Partial forwarding buffers are cleared at the pause/resume boundary; failed preparation before the pause does not clear normal correction data.
- Passive Debug alone does not pause these functions. Disabling Debug after an admitted update has paused the instrument does not tear down a flash write. The update has its own bounded transaction/transfer lifetime.

## Peer messages and limits

The selected transport carries bounded `RTM1` type-3 control envelopes. SiK shares its existing UART owner and packet scheduler; Wi-Fi uses the known peer address on UDP 22347. A boot challenge/echo establishes reachability, hardware identity and opposite role. Update attempts are saved monotonically in NVS. CRC detects corruption; these radio messages are not cryptographically authenticated and cannot start an update or issue receiver commands.

Preparation, Updating, Cancelled and Reconnected notices are distinct. Only an observed Updating notice supplies the paused-link explanation. Unreceived notices remain ordinary link loss. Repeated notices cannot indefinitely extend the deadline. Overdue updates show **Update overdue — link unavailable**. Return notices and matching boot/attempt evidence show recovery; Rover quality still requires fresh corrections and the required GNSS fix. Informational labels never bypass survey quality gates.

The old SiK session cannot safely carry restarted correction sequence counters. The preview explicitly blocks correction input/output after receipt-based radio restoration and shows **REJOIN SiK LINK**. Transparent session negotiation and cryptographic pairing remain future work.

## Package and boot safeguards

The 128-byte TPK1 header specifies format, Unit A/B, hardware type, image size, embedded identity offset, SHA-256 digest, version and header CRC32. The complete image must match the reviewed header and its embedded 64-byte TopoRTK identity. Filename changes cannot change the target. This is integrity validation, not signed-firmware authentication.

Only the inactive OTA slot is written, in bounded chunks. Transfer timeout is 120 seconds. Individual two-second receive timeouts are retried, with a 12-second limit without progress. Disconnects and hard receive errors abort immediately; rejected uploads close their HTTP connection. Missing/truncated/mismatched data, control loss, write failure or failed image verification abort without selecting the new boot image. The standard ESP-IDF image validation runs before boot selection; an NVS update receipt must also be read-back verified.

`verifyRollbackLater()` defers the framework's early image acceptance. A pending image starts a 30-second task watchdog and is accepted only after local display/configuration, survey service, HTTP service, NVS and image/receipt identity checks pass after at least ten seconds. Failed startup attempts rollback. No GNSS antenna fix, connected UM980, peer, Internet or SD card presence is required to accept otherwise healthy firmware. Actual bootloader rollback and watchdog behavior must still be proven on hardware; host doubles do not prove them.

## Build and package

From the repository root:

```powershell
platformio run -d firmware/um980-display-demo -e unit_a -e unit_b -j 2
python tools/package_firmware.py firmware/um980-display-demo/.pio/build/unit_a/firmware.bin firmware/um980-display-demo/.pio/build/unit_a/firmware.tpk
python tools/package_firmware.py firmware/um980-display-demo/.pio/build/unit_b/firmware.bin firmware/um980-display-demo/.pio/build/unit_b/firmware.tpk
```

Keep packages under ignored `.pio` and distribute locally to the intended instruments. Compiled firmware can contain the ignored build-time Wi-Fi configuration; binaries/packages must not be committed to the repository.

The [design](debug-and-ota.md) records decisions and the [software evidence](../tests/2026-09-14-ota/README.md) separates completed checks from outstanding hardware acceptance.
