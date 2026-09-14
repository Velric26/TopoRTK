# OTA rollback and interruption acceptance — hardware evidence (2026-09-14)

Hardware acceptance of the OTA safety paths that host doubles cannot prove. All tests ran against **Unit A (Base, 192.168.100.20)** over local Wi-Fi with **Unit B (Rover, 192.168.100.19)** as the acknowledged peer. Unit A ended the session on stock 0.11.5 with a fresh receipt; Unit B was untouched (0.11.5).

## Test images

Two flag-guarded builds of the production source (inert without the flags; version strings overridden so the test image is distinguishable):

- `0.11.5-rollback-test` (`-DTOPORTK_ROLLBACK_TEST_FAIL_HEALTH`): normal firmware whose boot-acceptance health gate is forced false — a pending boot can never be accepted.
- `0.11.5-hang-test` (`-DTOPORTK_ROLLBACK_TEST_HANG`): normal firmware that hangs in `loop()` while its boot is pending-verify — forcing the task watchdog (armed in `ota_boot_begin` for pending boots) to fire.

Both packages passed the standard OTA admission path (unit identity, size, SHA-256, embedded identity) before reaching the runtime under test.

## Results

### 1. Interrupted upload recovery — PASS

Raw-socket upload of the test package aborted with a TCP RST after 4,308 of 1,239,120 bytes. The instrument reported `failed` with **"Upload connection closed or receive error"**, kept firmware 0.11.5, kept its boot (boot_id unchanged, no reboot), kept its boot text, and survey records were unchanged. The next update cycle prepared and ran normally from the failed state. (`interrupt-evidence.json`)

A variant run also confirmed the 12-second no-progress stall detector: with a client that stopped sending but left the socket open, the instrument sat in `uploading`, then aborted with **"Upload stalled for 12 seconds"** at 1,047,969 bytes received, retaining firmware and records.

### 2. Boot-acceptance rollback (health-gate failure) — PASS

The `0.11.5-rollback-test` image was flashed via the standard OTA flow with acknowledged peer notices. Observed phase sequence (`rollback-health-evidence.json`):

1. `0.11.5` / previous boot — before flash
2. `0.11.5-rollback-test` / **`Checking new firmware`** — pending-verify boot ran, HTTP and display up, health gate never satisfied
3. `0.11.5` / **`Previous firmware restored (rollback)`** — after ~30 s the app marked its image invalid and rebooted; the bootloader fell back to the previous slot

Final state: firmware 0.11.5, state idle, unlocked, Debug On (default), survey records unchanged. Peer notices flowed (preparation acknowledged; updating; reconnected).

### 3. Watchdog rollback (hung pending boot) — PASS

The `0.11.5-hang-test` image was flashed the same way. During its pending-verify boot the application hung before the HTTP server started, so the unit disappeared from the network — no operator intervention. The task watchdog fired, the instrument reset, the bootloader marked the pending image invalid and restored stock 0.11.5. Final state identical to test 2: rollback boot text, idle, Debug On, records unchanged. (`rollback-hang-evidence.json`)

## Findings and notes

- **Bootloader rollback and task-watchdog recovery are proven on hardware.** Three independent full rollback cycles (two accidental, one observed per failure mode) all ended with the previous firmware restored and the unit fully serviceable.
- **Recovery confirmation on the peer requires GNSS quality.** Unit B showed **"Update overdue - link unavailable"** after Unit A's updating notice because indoors the Rover cannot pass the fresh-corrections + GNSS-fix recovery gate. This is by design (a returned peer is only "recovered" when it is genuinely survey-ready); documented here so indoor testers do not misread it.
- **Stale-receipt boot text is cosmetic.** After a rollback, the boot line reads `Previous firmware restored (rollback)` on every subsequent boot until the next successful update writes a fresh receipt (matching partition + version). A stock re-flash cleared it.
- **Process finding:** packaging a "stock" image from a stale `.pio/build` binary silently produced a test image; the flash-then-rollback chain caught it and the unit rolled back correctly. Package verification (version + unit byte) is now performed before every flash — a script should assert it (packaging tool could accept `--expect-version`).
- Hotspot OTA (instrument AP instead of router) remains untested.

## Outstanding

- Hotspot-mode OTA acceptance.
- Next P0: SiK bench session with real UM980s (radios powered, antennas connected, indoors — no fix required for link-level checks).
