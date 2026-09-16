# Debug, OTA and paired update status

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

Turning Debug On is **not** a general system pause. Physical disable ends capture and clears the in-memory history; it must not revoke an ongoing ordinary survey operation. During a future admitted OTA transaction, disable must not tear down a flash write halfway through: defer shutdown to a safe transaction boundary, then reboot or abort safely.

## Passive Debug capture and access

The touchscreen has a Debug page and On/Off control. The Survey interface gains a Debug tab on both roles (Base now has Base setup and Debug navigation). `/debug` remains a readable explanation when Off, but private log access is rejected by the server until hardware enables Debug and the browser holds the current controller token. There is no HTTP operation to enable Debug and no PIN. The private capture endpoints respect existing control expiry/takeover and same-origin mutation checks.

`debug_core.h` bounds the log to 32 entries of 120 characters, at most eight entries per second. `debug_service.cpp` receives calls from existing owners; it never reads a UART, writes GNSS commands, enters radio AT mode or runs a probe. Main-loop capture uses fixed memory. HTTP copies the bounded history under a short lock, then performs JSON allocation/serialization outside that lock. Full throughput under real instrument load still needs hardware measurement; do not call a finite host test proof of zero timing impact.

Capture includes recent GNSS text, GNSS RTCM summaries, selected live SiK complete-message RX/envelope TX summaries and Wi-Fi correction RX/TX summaries. It does not provide a complete raw serial recording, all RF bytes, driver-level receive-error capture, a universal `Serial` mirror or historical logs before enabling Debug. Capture throttling/overwrites/truncation have separate counters and must not be described as radio loss. Payload output uses text rendering, not HTML interpretation. Credentials/control tokens are never passed to the observer; sensitive position/receiver text is available only to the current controller while Debug is enabled.

The browser can pause its view and download the current bounded snapshot. Debug has no idle timer and is On by default at startup (0.11.5): it remains on until disabled or restart. The normal controller's two-minute lease remains separate. Both heartbeat requests and direct API mutations still require the current token. Mode and capture are RAM-only; capture is empty at boot.

Public availability: `GET /api/v1/debug`. Private history: `GET /api/v1/debug/log`. Authenticated same-origin `POST /api/v1/debug` accepts only `disable`. `web/navigation.js` drives the page's explicitly declared Debug tab: it stays disabled until a fresh `GET /api/v1/debug` reports the service enabled and advancing, with the visible `#debugAvailability` note as its `aria-describedby`. No arbitrary command route exists.

## Peer update-notice protocol

The wire record is 40 bytes: `TUP1`, version 1, kind, sender/recipient instrument IDs, little-endian session/attempt/sequence/duration, role, acknowledged kind, ten reserved zero bytes, and CRC32 over the first 36 bytes. IDs 1/2 identify hardware, independently of Base/Rover role. Prepare/Updating deadlines are bounded to 3–180 seconds from reception; no phase/retry may extend an existing deadline. A sender offers at most six writes over three seconds, spaced 500 ms after successful transmission. The adapter must call `committed` only after accepting the entire record. Acknowledgements correlate session, attempt, sequence, phase and peer; they cannot authorize a flash operation.

Attempts must increase within a newly provisioned session. Selecting the same session preserves replay history. A cancellation received before its prepare/update records a closed attempt, so delayed messages cannot reopen it. Reconnection remains a separate status until the adapter supplies fresh identity and receiver-quality evidence. CRC provides corruption detection, not authentication. Transport adapters must enforce trusted pairing/session selection and bounded acknowledgement scheduling; these are not supplied by this portable core.

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

Keep informational update reason separate from safety gates: stale correction age still inhibits collection. A claimed Updating reason cannot make old observations usable. Store enough identity/attempt correlation to handle reboot without accepting stale buffered notices.

Before an update, wait a bounded time for the peer acknowledgement. If it cannot be confirmed, show a distinct warning and require an explicit choice to continue without peer notification. Do not permanently block an otherwise recoverable single-unit update merely because its peer is offline. This choice belongs in the final review, not an automatic retry loop.

## Guarded OTA

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
