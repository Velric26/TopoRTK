# Debug, OTA and paired update status

## Latest hardware checkpoint — 0.11.2, 2026-09-14

Unit B successfully updated from 0.11.1 to 0.11.2 over local Wi-Fi with the actual browser workflow, verified its new boot, preserved saved survey state and returned Debug to Off. Unit A (still 0.11.0) displayed preparation, Updating and Reconnected statuses. The complete transfer took 118.602 seconds with zero receive-timeout retries, leaving little margin below 120 seconds. This proves one successful local-router OTA/acceptance cycle, not hotspot coverage, sustained throughput, actual rollback or field reliability. The preceding attempt stopped before upload; 0.11.2 corrects a host-reproduced race between HTTP timestamps and the main-loop clock. See [full evidence and next work](../tests/2026-09-14-ota-success/README.md). Earlier checkpoints below are historical.

## Source increments — 0.11.3/0.11.4/0.11.5, 2026-09-14

**0.11.5 (deployed):** Debug is **On by default at startup** — an operator request. A manual disable persists only until the next restart; there is still no stored preference, no idle timer and no HTTP enable path. Capture remains RAM-only and empty at boot. All other Debug/OTA behavior is unchanged.

**Rollback acceptance (2026-09-14):** deliberate fault-injection images proved on hardware that a pending boot which fails the health gate is marked invalid and rolled back by the bootloader, that a hung pending boot is recovered by the task watchdog, and that a raw-socket mid-upload disconnect retains the running firmware and records. See [rollback acceptance evidence](../tests/2026-09-14-rollback-acceptance/README.md). Peer recovery confirmation requires the Rover's GNSS quality gate, so indoors the peer shows "Update overdue" after an update — by design. Hotspot OTA remains untested.

**0.11.3 (deployed):** The OTA transfer deadline was raised from 120 to 300 seconds after the real 118.6-second transfer left almost no margin; the 12-second no-progress stall limit is unchanged. Debug lost its 15-minute idle timer: it stays On until disabled on the instrument or web. The `POST /api/v1/debug` `activity` operation and the browser keep-alive/extend controls were removed with the timer; the controller's separate two-minute lease and all OTA guard deadlines are unchanged. **0.11.4 (deployed):** the dashboard's Correction Link and Link Signal cards merged into one BASE/ROVER LINK indicator. Both versions were verified on hardware by Wi-Fi OTA with acknowledged peer notices.

## Current checkpoint — 0.11.0 software, 2026-09-14

Peer transport integration, the guarded package uploader, role-specific confirmations, browser progress/new-boot verification, deferred boot acceptance and rollback handling are now implemented in source. Software checks and both firmware builds pass. **Both units have received 0.11.0 by USB. The first Unit B Wi-Fi OTA transfer timed out after 37,336 bytes and safely retained the running firmware. Successful OTA and rollback acceptance remain pending.** Peer preparation and Updating notices were observed on Unit A. See the [live result and next work](../tests/2026-09-14-ota-live/README.md). Routine development updates use a small settings snapshot when practical; full flash backups are optional. See the [operator guide and actual behavior](ota-operator-guide.md), [software validation](../tests/2026-09-14-ota/README.md) and [USB installation evidence](../tests/2026-09-14-ota-usb/README.md).

**0.11.1 recovery follow-up:** Unit B is USB-installed and verified with bounded transient-timeout retries and rejected-upload socket closure. Debug requests are cancelled/paused during the transfer. Unit A remains 0.11.0. HTTP handler fault tests, OTA/Debug browser regressions, existing service tests and both builds pass. A real OTA retry remains pending physical Debug enable. See [evidence](../tests/2026-09-14-ota-recovery/README.md).

The stored correction medium is restored at boot and the pair negotiates a fresh session automatically: corrections stay blocked until a current-boot bidirectional exchange proves the peer, so restarted counters cannot reuse the old session's replay window. Seamless session negotiation is implemented in 0.11.16-arch-r5. The earlier increment sections retain their historical checkpoints and the original design requirements.

## Accepted operator requirements

- Name the feature **Debug**, including the touchscreen setting and dedicated web tab.
- Enable/disable from **Setup → Debug** on the instrument. On by default at startup (0.11.5 supersedes the original default-off). Stays On until disabled on the instrument or web; a disable lasts until the next restart.
- Gray out the web Debug tab when unavailable and explain how to enable it. An unavailable/stale connection must also disable access.
- Passive monitoring must not pause surveying, corrections or normal receiver communication.
- Treat firmware flashing as a separate, explicitly confirmed disruptive operation. Explain the affected functions before starting.
- Tell the paired unit that its peer is updating when a valid notice reaches it. Do not invent that explanation when no notice was received.
- Ask known questions together up front. If further user input is needed, ask and end the turn immediately; do not work or wait actively while awaiting a reply.
- Pause implementation at 10% remaining of the **five-hour** allowance, update documentation, commit and push. The weekly allowance is not the threshold.

**Restart on command in Debug mode (2026-09-15, R10c).** The operator asks for the instrument to be restarted in software — ideally usable after a flash and available while Debug is on — as the power-cycle equivalent for a state that cannot be recovered in place. Implemented as one admission path with three surfaces: the diagnostics API (`{"op":"restart","confirm":true}`), the touchscreen Debug page (two-tap confirm), and a Debug-page browser card. It rides the existing diagnostics gates (controller lease, same origin, explicit confirm), is refused while an update is transferring, while a run or probe is in flight, and while collecting or writing; it then holds the reservation through a 500 ms grace window so nothing new starts under a chip about to reset, publishes `restart_pending`, and resets on a later service turn so the 202, the snapshot and the serial line land first. Nothing is persisted, and after a flash the reboot is already part of OTA/USB flow — this is for the case where a clean boot is wanted without flashing. The restart phase of `run_pair_matrix.py` restarts each unit in turn and requires the pair to renegotiate by itself.

## Recommended separation

| Mode/action | Normal operation | Access and lifetime |
|---|---|---|
| Debug Off | Surveying and corrections run normally; ordinary status remains available | Debug tab disabled with touchscreen instructions; private capture unavailable |
| Debug On, passive view | Same receiver/radio owners and normal forwarding; no injected queries or test traffic | Hardware enable, existing latest-request takeover for private logs; no idle timer — persists until disabled or restart |
| Active tests or receiver commands | May change UART traffic or receiver state | Separate named action and existing occupation/configuration admission gates; never started merely by opening Debug |
| Firmware update | Local processing/forwarding must quiesce, and web/Debug access disconnects during reboot | Validate image, show role-specific impact, acquire exclusive update ownership, notify peer, then explicit confirmation |

Turning Debug On is **not** a general system pause. Physical disable ends capture and clears the in-memory history; it must not revoke an ongoing ordinary survey operation. During a future admitted OTA transaction, disable must not tear down a flash write halfway through: defer shutdown to a safe transaction boundary, then reboot or abort safely. No OTA transaction is exposed in the first increment.

## Increment 1 — implemented in 0.10.5, deployment pending

Both Unit A/B builds, production host regressions and controlled browser tests passed on 2026-09-13. No hardware was flashed or reconfigured. See the [validation record and limitations](../tests/2026-09-13-debug/README.md).

The touchscreen has a Debug page and On/Off control. The Survey interface gains a Debug tab on both roles (Base now has Base setup and Debug navigation). `/debug` remains a readable explanation when Off, but private log access is rejected by the server until hardware enables Debug and the browser holds the current controller token. There is no HTTP operation to enable Debug and no PIN. The private capture endpoints respect existing control expiry/takeover and same-origin mutation checks.

`debug_core.h` bounds the log to 32 entries of 120 characters, at most eight entries per second. `debug_service.cpp` receives calls from existing owners; it never reads a UART, writes GNSS commands, enters radio AT mode or runs a probe. Main-loop capture uses fixed memory. HTTP copies the bounded history under a short lock, then performs JSON allocation/serialization outside that lock. Full throughput under real instrument load still needs hardware measurement; do not call a finite host test proof of zero timing impact.

Capture includes recent GNSS text, GNSS RTCM summaries, selected live SiK complete-message RX/envelope TX summaries and Wi-Fi correction RX/TX summaries. It does not provide a complete raw serial recording, all RF bytes, driver-level receive-error capture, a universal `Serial` mirror or historical logs before enabling Debug. Capture throttling/overwrites/truncation have separate counters and must not be described as radio loss. Payload output uses text rendering, not HTML interpretation. Credentials/control tokens are never passed to the observer; sensitive position/receiver text is available only to the current controller while Debug is enabled.

The browser can pause its view and download the current bounded snapshot. Debug has no idle timer and is On by default at startup (0.11.5): it remains on until disabled or restart. The normal controller's two-minute lease remains separate. Both heartbeat requests and direct API mutations still require the current token. Mode and capture are RAM-only; capture is empty at boot.

Public availability: `GET /api/v1/debug`. Private history: `GET /api/v1/debug/log`. Authenticated same-origin `POST /api/v1/debug` accepts only `disable`. `web/navigation.js` drives the page's explicitly declared Debug tab: it stays disabled until a fresh `GET /api/v1/debug` reports the service enabled and advancing, with the visible `#debugAvailability` note as its `aria-describedby`. No arbitrary command route exists. The web page explicitly shows the remaining update plan with the upload control disabled.

## Increment 2 — peer update-notice protocol

### Portable core checkpoint — 2026-09-13

Implementation paused when the five-hour usage check reported **91% used / 9% remaining**, per the operator's limit. The passing portable core, test evidence and continuation steps are committed together. No transport integration or OTA flash was started.

`src/update_notice.h` now implements a bounded, hardware-independent notice codec and sender/peer state machines. `test/run_update_tests.py` compiles it with warnings as errors and runs wire-bit corruption, reserved-field rejection, loss/retry, wrong role/unit/session/attempt, phase reorder, cancellation overtaking prepare, timer rollover and 1,000 seeded fault schedules. The tests pass. This core is **not integrated into either transport, display or OTA service**; it does not change the running firmware and does not make the upload control available. Evidence: [protocol checkpoint](../tests/2026-09-13-update-notice/README.md).

The wire record is 40 bytes: `TUP1`, version 1, kind, sender/recipient instrument IDs, little-endian session/attempt/sequence/duration, role, acknowledged kind, ten reserved zero bytes, and CRC32 over the first 36 bytes. IDs 1/2 identify hardware, independently of Base/Rover role. Prepare/Updating deadlines are bounded to 3–180 seconds from reception; no phase/retry may extend an existing deadline. A sender offers at most six writes over three seconds, spaced 500 ms after successful transmission. The adapter must call `committed` only after accepting the entire record. Acknowledgements correlate session, attempt, sequence, phase and peer; they cannot authorize a flash operation.

Attempts must increase within a newly provisioned session. Selecting the same session preserves replay history. A cancellation received before its prepare/update records a closed attempt, so delayed messages cannot reopen it. Reconnection remains a separate status until the adapter supplies fresh identity and receiver-quality evidence. CRC provides corruption detection, not authentication. Transport adapters must enforce trusted pairing/session selection and bounded acknowledgement scheduling; these are not supplied by this portable core.

Next implementation steps, in dependency order:

1. Add a shared UART envelope demultiplexer and scheduler for RTCM plus notices. Do not feed an independent notice parser arbitrary RTCM payload bytes: an embedded marker could otherwise be mistaken for control traffic. Add equivalent selected-peer Wi-Fi routing, without silently substituting Wi-Fi when SiK is selected.
2. Provide fresh session/attempt correlation across reboot and role changes, bounded acknowledgement scheduling, and truthful touchscreen/web peer labels. Persist or explicitly restore the current SiK selection before promising automatic post-update recovery.
3. Implement the image package/target validation and OTA admission/ownership service, then the upload UI, deferred boot acceptance and rollback checks described below.
4. Run transport, HTTP, browser and firmware builds before the combined USB preparation and real OTA/failure tests. The user accepted completing OTA before this USB installation on 2026-09-13.

Implement a small bounded control channel independently of RTCM freshness and command execution. It must work on the **selected instrument link**, including SiK; local-router reachability cannot be a prerequisite in the field. Share the existing UART2 owner and scheduler. Use explicit peer identity, current session, update-attempt ID, sequence, finite deadline and CRC; validate source/session and reject malformed, stale or replayed notices. CRC/session matching is protocol isolation, not cryptographic authentication. Define/provision authentication separately before claiming authenticated RF control.

Use `prepare-update`, `acknowledged`, `updating`, `cancelled` and `reconnected` states with bounded retries and repeat suppression. Sending an acknowledgement must not itself change receiver configuration or cancel an occupation. An update notice records intent; `updating` follows only when interruption actually starts. A repeated notice must not indefinitely extend the peer's deadline. A peer that receives no notice shows the ordinary lost-link state.

| Peer observation | Message |
|---|---|
| Valid Base updating notice | **Base updating — corrections paused** |
| Valid Rover updating notice | **Rover updating — reception paused** |
| Fresh notice not acknowledged | Updating unit shows **Peer notification unconfirmed**; it cannot claim the peer knows |
| Notice deadline expires without recovery | **Update overdue — link unavailable** |
| Peer identity returns but corrections/receiver quality are not fresh | **Peer reconnected — checking corrections** |
| Fresh link and receiver quality recover | Ordinary link/fix/readiness states resume |

Keep informational update reason separate from safety gates: stale correction age still inhibits collection. A claimed Updating reason cannot make old observations usable. Store enough identity/attempt correlation to handle reboot without accepting stale buffered notices. The current 0.10.4 SiK preview returns to Wi-Fi at restart and uses manual sessions; automatic post-update radio recovery therefore needs persistent paired identity and fresh session negotiation, or an explicit documented manual rejoin step. Do not promise seamless SiK restart recovery until that dependency passes.

Before an update, wait a bounded time for the peer acknowledgement. If it cannot be confirmed, show a distinct warning and require an explicit choice to continue without peer notification. Do not permanently block an otherwise recoverable single-unit update merely because its peer is offline. This choice belongs in the final review, not an automatic retry loop.

## Increment 3 — guarded OTA

Support browser uploads through both the instrument hotspot and local Wi-Fi. No cloud/server/Internet dependency. Verify Base hotspot access in local-router deployments; the current Base Phone page points to its configured network and does not create a second independent phone AP in that mode. Preserve the working Wi-Fi correction topology rather than silently changing network mode just to enable Debug.

1. Validate target hardware and instrument ID, image version/format/size and integrity before accepting the update. Define a versioned package/manifest with the correct Unit A/B identity. Do not trust the filename or a caller-supplied target label alone. Distinguish digest integrity from signed-image authentication.
2. Display role-specific impact and preserve the selected image review until confirmation. Refuse to interrupt active collection, pending survey writes, a receiver configuration or a diagnostic that owns the link. Acquire the same admission authority atomically; frontend disabled controls alone are insufficient.
3. Notify the peer, show acknowledgement/unconfirmed status, and obtain the final explicit update confirmation. Takeover changes must not grant two simultaneous upload owners. Cancellation/control loss before commit must have a safe bounded abort path.
4. Quiesce local correction queues/UART production work and mark readiness unavailable. Retain enough web service to show transfer progress while feasible. Write only the inactive OTA slot; cap size, chunk memory, transfer timeout and retries. Do not erase jobs, NVS settings or SD content.
5. Validate the complete received image before selecting it for boot. A disconnected client or truncated upload must leave the old boot image usable. Do not acknowledge completion just because HTTP accepted bytes.
6. Restart into pending verification. Check local firmware identity, required services and storage/communication initialization before confirming the image. Lack of sky view, an unplugged UM980, an absent peer or missing Internet must not alone force rollback of otherwise healthy firmware.
7. Confirm success after the new boot identifies itself; otherwise report disconnected/unknown and allow recovery. Explicitly test bad-image rejection, interrupted upload, new-image boot failure and rollback before enabling OTA for normal use.

The pinned 16 MB layout already has `otadata`, `ota_0` and `ota_1` slots (0x640000 bytes per app). The installed Arduino ESP32-S3 qio_opi SDK enables `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. However, `cores/esp32/esp32-hal-misc.c` defaults `verifyRollbackLater()` to false and accepts the image through `verifyOta()` before application setup. Override/defer acceptance and implement bounded application checks; a configured flag alone is not validation of our complete rollback workflow. Verify the actual installed bootloader and failure behavior during the one-time USB preparation. Reference: [Espressif OTA guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html).

### Warnings shown before starting

**Updating Base:** correction transmission pauses. Rover may lose RTK fixed; stale corrections must block collection. Base web access, passive logging and local processing disconnect during reboot. Existing jobs/settings are retained; the UM980 and SiK firmware are not updated.

**Updating Rover:** finish collection first. Rover correction reception, local processing, web access and passive logging pause during update/reboot. Base may continue generating corrections. Existing jobs/settings are retained; the UM980 and SiK firmware are not updated.

Neither warning should imply that a radio is powered off, a receiver is updated, or the peer acknowledged unless that was actually observed. Recovery/normal operation must be determined by fresh runtime evidence.

## Acceptance and deployment order

1. Host tests for actual touchscreen routing, Off-at-boot semantics, idle expiry/rollover, explicit activity, unchanged correction output, bounded capture and disabled endpoint behavior.
2. Browser tests for both roles, gray/disabled tab and enable instructions, token loss, stale/offline state, expiry, log text escaping, snapshots, active-occupation monitoring and responsive layout.
3. Build both targets. Review the new Debug page and capture integration before flashing. Keep the known-good 0.10.4 binaries/commit available.
4. Implement and test peer notice/acknowledgement loss/reorder/replay/deadline behavior, then OTA transaction/rollback behavior. Do not show a working Upload control prematurely.
5. Prefer a combined final USB install once OTA is ready, so the operator does not repeatedly open the battery holder for intermediate previews. After that, validate an actual Wi-Fi update on both units, via local Wi-Fi and hotspot, plus interruption and boot rollback. Retain physical USB recovery access for a firmware that cannot boot or start networking.
6. Measure passive Debug overhead with live RTCM and a normal occupation, verify receiver freshness is maintained, then conduct real paired update/recovery checks without collecting a field point. Keep packet-loss root-cause investigation deferred unless it prevents useful operation.
