# Partial USB installation — 2026-09-14

Unit A/Base received 0.11.0 by USB. Unit B/Rover remains on 0.10.4; the user explicitly stopped further flashing after backups. No Wi-Fi OTA upload has occurred on either unit.

## Unit A observed results

- PlatformIO USB upload succeeded and esptool reported the written data hash verified.
- Live Debug API reported 0.11.0, Debug Off and advancing uptime. Update API reported available, idle, unlocked, unpaused, attempt zero and USB / normal boot.
- Saved survey state matched the preflash snapshot (identity/role, active job, jobs, record usage, configuration and storage readiness). Receiver profile remained verified; no field point was collected.
- With temporary takeover then release, private logs were rejected with 403 while Debug was Off; valid package preparation and unprepared start were rejected with 409; a wrong origin was rejected with 403. No update was admitted.
- The read-only installed-browser test passed against the actual Survey and Debug pages: disabled Debug tab with touchscreen instructions, 0.11.0, idle OTA, unavailable controls and no JavaScript errors. See `unit-a-browser.json` and `unit-a-debug-off.png`.

Reproduce the browser check with Debug Off:

```powershell
node firmware/um980-display-demo/test/check_debug_installed.cjs http://192.168.100.20 A
```

## Backups and remaining checks

Private full 16 MB snapshots for both units completed before the stop. Unit B's full read completed just before cancellation; a subsequent 28,672-byte settings/OTA-metadata snapshot completed in 0.7 seconds. Files and raw device snapshots remain in ignored `.pio/ota-usb-2026-09-14/`, outside Git. Full backups are optional going forward; see the operator guide for settings versus firmware recovery limits.

Pending: Unit B installation, physical Debug enable/expiry, actual Wi-Fi OTA, peer update notices during a real upload and actual bootloader rollback/failure recovery. Host tests and successful USB installation do not establish those results. Resume only when the user requests it.
