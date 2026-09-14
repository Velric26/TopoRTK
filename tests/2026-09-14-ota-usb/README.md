# Partial USB installation — 2026-09-14

Unit A/Base received 0.11.0 by USB. Following the initial pause, the user explicitly requested Unit B installation: its USB upload passed in 15.062 seconds with the written data hash verified. Live HTTP confirmed Unit B / 0.11.0, normal USB boot, Debug Off, idle/unlocked OTA and unchanged saved survey state. Both units are now USB-installed. A subsequent Wi-Fi test is recorded separately in [live OTA evidence](../2026-09-14-ota-live/README.md).

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

At completion of USB installation, actual Wi-Fi OTA, physical Debug expiry and bootloader rollback remained pending. The later live OTA record supersedes this checkpoint for transfer and paired-notice results; successful USB installation alone does not establish OTA acceptance.
