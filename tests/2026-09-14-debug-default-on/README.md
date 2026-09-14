# Debug enabled by default at startup — 0.11.5 deployed to both units (2026-09-14)

Operator request: Debug mode is now **On by default whenever an instrument boots**, on both units. This removes the physical touchscreen step as an OTA prerequisite for every future update.

## Behavior

- A constructed `debugmode::Session` starts enabled; at boot, Debug is On with an empty capture buffer.
- A manual disable (touchscreen or web) still works and lasts **only until the next restart** — there is no stored preference, no idle timer and still no HTTP enable path.
- Capture remains RAM-only, bounded (32 × 120 characters, 8 entries/second) and empty at boot.
- OTA interplay: Debug-enabled is a target-side OTA prerequisite; with default-on it is satisfied at every boot automatically. The "Debug disabled before update" guard and mid-upload pause behavior are unchanged.

## Verification

- `test/run_host_tests.py` — reworked Debug block: default-on at boot, touchscreen toggle off/on during receiver setup, persistence over 15/45-minute horizons, disable clears capture, bounded capture, unchanged COM2 output. All PASS.
- `check_debug_browser.cjs` / `check_ota_browser.cjs` — PASS with the updated post-reboot Debug state and completion text.
- `pio run -e unit_a -e unit_b` — both builds SUCCESS; packages verified (unit byte, version, SHA-256, header CRC).
- Live Wi-Fi OTA to both units (units off USB; `run_ota_live.cjs`, acknowledged paired notices both directions):
  - Unit A: 0.11.4 → 0.11.5, ~37 s, Update complete, new boot verified, **Debug On after restart (asserted)**, saved survey state unchanged (`unit-a-ota-evidence.json`).
  - Unit B: 0.11.4 → 0.11.5, ~28 s, same PASS checks (`unit-b-ota-evidence.json`).
- Note: the "Debug is Off after restart" text visible in the first run's console output came from the target's pre-flash 0.11.4 web page; the run script's post-reboot API assertion (`enabled === true`) is the authoritative check and passed for both units.

## Outstanding

- Physical confirmation (optional): after a power cycle, the touchscreen Debug page should show DEBUG ON without any operator action.
