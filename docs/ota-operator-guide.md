# ESP32 firmware updates — OTA preferred

**OTA over Wi-Fi is the preferred method for routine firmware flashing and updates.** Use the existing web Debug workflow or the supported browser automation below. USB/serial flashing is the fallback when OTA is unavailable, the instrument cannot be reached over the network, or bootloader/partition recovery or first-time provisioning is required. A normal OTA update does not require connecting USB or removing the batteries/holder; keep stable power throughout.

Both units have validated local-router OTA. Debug defaults **On** at startup from 0.11.5 and has no idle timeout. If manually disabled, enable it on the touchscreen; there is no remote enable command. The transfer budget is 300 seconds with a 12-second no-progress limit. Interrupted-upload retention and bootloader/watchdog rollback have been tested on hardware; hotspot OTA remains unvalidated. See [default-on deployment](../tests/2026-09-14-debug-default-on/README.md) and [rollback evidence](../tests/2026-09-14-rollback-acceptance/README.md).

## Known local-router addresses

| Hardware | Known IP | Debug page |
|---|---|---|
| Unit A | `192.168.100.20` | [Unit A Debug](http://192.168.100.20/debug) |
| Unit B | `192.168.100.19` | [Unit B Debug](http://192.168.100.19/debug) |

These are last-known DHCP addresses, not guaranteed static assignments. Hardware A/B identity determines the package; Base/Rover roles can change. Check `/api/v1/survey` (`unit`) and `/api/v1/update` (numeric `unit`, firmware and boot status) before updating. If an address changes, use the instrument's Link display. Hotspot addresses depend on the active network configuration and need not match this table.

## Recommended OTA procedure

1. Build and package the intended hardware variant using the commands below. Verify the package target and version against the intended source/build; do not reuse a stale `.tpk`. Both existing instruments are already provisioned for OTA. Update one instrument at a time, with collection and active diagnostics stopped.
2. Check Debug availability on the target. It is On by default at startup (0.11.5) and stays On until disabled on the instrument or web (no idle timer). Only if it is Off, open **Setup → Debug → Enable Debug** on that instrument. There is no HTTP enable command and no PIN. The current takeover owner may use private Debug/OTA actions.
3. Connect through the existing local Wi-Fi or instrument hotspot and open **Debug** in the Survey interface. Select **Take control** (the latest accepted request owns control), then verify the unit identity. Use the known address above or the displayed instrument address. The Base in Local Router mode does not gain a new independent phone hotspot from this feature; Direct Link's Base AP remains available. Network provisioning/topology is unchanged.
4. Choose the `.tpk` package for **hardware Unit A or B**, independently of its current Base/Rover role. Select **Review package and notify peer**. This reserves the instrument and rejects active collection, queued survey mutations, receiver setup and diagnostics. Correction forwarding continues during review.
5. Read the role-specific interruptions. Accept the interruption/power checkbox. If peer acknowledgement cannot be confirmed, explicitly choose whether to permit an unconfirmed notification. **Confirm interruption and install** pauses local processing and starts a separate Updating notice. Without the override, a missing final acknowledgement aborts before writing flash.
6. Keep power connected. The browser shows transfer progress, then waits for a new boot and its startup verdict. HTTP/Debug polling pauses during the synchronous upload. A successful transfer alone is not reported as a successful update. If the connection is lost, reconnect and check the outcome before retrying.
7. Debug returns On after reboot (default). Disable it on the touchscreen if not needed. For **SiK**, start a **fresh Base correction session**, then copy it to Rover. The saved old route is restored for peer-status communication only; correction traffic remains blocked until the new session is selected. This avoids resetting sequence/replay protection in the old session. Wi-Fi does not need this manual SiK rejoin.

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

Only the inactive OTA slot is written, in bounded chunks. Transfer timeout is 300 seconds (raised from 120 in 0.11.3 after the measured 118.6-second transfer left almost no margin). Individual two-second receive timeouts are retried, with a 12-second limit without progress. Disconnects and hard receive errors abort immediately; rejected uploads close their HTTP connection. Missing/truncated/mismatched data, control loss, write failure or failed image verification abort without selecting the new boot image. The standard ESP-IDF image validation runs before boot selection; an NVS update receipt must also be read-back verified.

`verifyRollbackLater()` defers the framework's early image acceptance. A pending image starts a 30-second task watchdog and is accepted only after local display/configuration, survey service, HTTP service, NVS and image/receipt identity checks pass after at least ten seconds. Failed startup attempts rollback. No GNSS antenna fix, connected UM980, peer, Internet or SD card presence is required to accept otherwise healthy firmware. **Proven on hardware 2026-09-14**: deliberate fail-health and hang test images both rolled back to the previous firmware via the bootloader, and a raw-socket mid-upload disconnect retained the running firmware — see [rollback acceptance evidence](../tests/2026-09-14-rollback-acceptance/README.md). Hotspot OTA remains untested.

## Build and package

From the repository root:

```powershell
platformio run -d firmware -e unit_a -e unit_b -j 2
python tools/package_firmware.py firmware/.pio/build/unit_a/firmware.bin firmware/.pio/build/unit_a/firmware.tpk
python tools/package_firmware.py firmware/.pio/build/unit_b/firmware.bin firmware/.pio/build/unit_b/firmware.tpk
```

Keep packages under ignored `.pio` and distribute locally to the intended instruments. Compiled firmware can contain the ignored build-time Wi-Fi configuration; binaries/packages must not be committed to the repository.

The [design](debug-and-ota.md) records decisions and the [software evidence](../tests/2026-09-14-ota/README.md) separates completed checks from outstanding hardware acceptance.

## Supported scripted OTA (optional)

Run from the repository root after the build/package commands above. Requires Node.js, Playwright available to Node, and Microsoft Edge installed (the runner launches Edge). Use the project's existing Node environment; if needed, set `NODE_PATH` to the environment containing Playwright. The manual browser procedure requires none of these automation dependencies.

```powershell
# Update hardware Unit A; monitor Unit B as its peer.
node firmware/test/run_ota_live.cjs --install A http://192.168.100.20 http://192.168.100.19 firmware/.pio/build/unit_a/firmware.tpk

# Or update hardware Unit B; monitor Unit A as its peer.
node firmware/test/run_ota_live.cjs --install B http://192.168.100.19 http://192.168.100.20 firmware/.pio/build/unit_b/firmware.tpk
```

**These commands really install firmware.** Run one at a time. The runner validates package target/size/digest, checks live identity, Debug and idle state, takes control, reviews the package, requires peer preparation acknowledgement, accepts the interruption warning, and uploads without enabling the unconfirmed-peer override. It verifies a changed boot ID, startup acceptance, current default Debug On, and unchanged saved survey state. It never retries automatically. Evidence is private under `firmware/.pio/ota-live-<timestamp>/`. If peer acknowledgement is unavailable, the runner stops; use the manual workflow only after reviewing its explicit unconfirmed-peer warning.

**Verified 2026-09-14 (`0.11.6-arch-r1`, both units):** the scripted runner completed guided updates on Unit A and Unit B with acknowledged peer notices, verified new boots, Debug On after restart and unchanged saved survey state; see the [deployment record](../tests/2026-09-14-arch-r1-deployment/README.md). Two prerequisites matter in practice. First, update notices ride the currently selected correction transport: after a SiK bench session, switch corrections back to Wi-Fi on both units before updating, or preparation cannot be acknowledged. Second, the target must be fully idle: a rejected preparation reports `Finish collection, receiver setup and diagnostics first` while a survey or diagnostic reservation is held, and a restart clears a held reservation. The runner is plain CommonJS and also executes under Bun with the project's `test/node_modules` Playwright when Node is not on PATH; evidence remains private under `.pio/ota-live-<timestamp>/`.

### Address detection: recommended follow-up, not implemented

The current runner requires both URLs; it does **not** scan these addresses automatically. A future wrapper should probe both known addresses with bounded read-only HTTP requests, read their hardware identities, and map A/B to reachable URLs. Select the unique identity matching the package, never simply the first reachable IP. Reject duplicate/ambiguous identities, a missing target or busy target; report an unavailable peer without silently bypassing its acknowledgement. Permit explicit URL overrides for changed DHCP or hotspot addresses. This removes routine manual IP selection without risking an update to the wrong unit.

Automatic discovery was considered but deferred because the five-hour usage pause threshold had already been reached during this documentation update. No detection capability or device changes are claimed here.

## USB/serial fallback

Use USB when OTA cannot be used or for recovery/initial provisioning. Confirm the USB identity/port first: current project defaults are Unit A `COM4`, Unit B `COM10`, but enumeration can change. From the repository root, select only the required target:

```powershell
platformio run -d firmware -e unit_a -t upload
# Or:
platformio run -d firmware -e unit_b -t upload

# Optional USB console (close before uploading):
platformio device monitor -d firmware -e unit_b
```

Verify the write hash, boot/version, and preserved settings/jobs afterward. Do not erase flash as a routine update step. Full-flash backups remain optional as described above; USB recovery restores firmware, not erased user data.
