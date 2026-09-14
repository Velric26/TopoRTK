# Merged Base/Rover Link dashboard card — 0.11.4 deployed to both units (2026-09-14)

Operator-requested display change: on both Base and Rover screens, the former **Correction Link** and **Link Signal** cards are merged into a single **BASE/ROVER LINK** indicator. The value names the transport first (`Wi-Fi` or `Radio`) followed by either `Disconnected` or the signal strength in dBm.

## Behavior

| State | Value |
|---|---|
| Wi-Fi transport, linked | `Wi-Fi  -67 dBm` (live receiver RSSI) |
| Wi-Fi transport, not linked | `Wi-Fi Disconnected` |
| SiK radio, linked | `Radio Connected` — the SiK radio reports no RSSI to the ESP32, so no dBm can be shown honestly |
| SiK radio, not linked | `Radio Disconnected` |

Color keeps the previous semantics: red when disconnected, green for a linked radio, and the RSSI-graded green/yellow/red scale for Wi-Fi. The dashboard reflowed from four to three equal-height cards (90 px each, 8 px spacing); the warning panel position is unchanged. The web status JSON and web UI are unchanged.

## Verification

- `test/run_host_tests.py` — all cases pass; the UI bounds/repaint-cache case renders the new layout through the real drawing code and asserts no repaint when nothing changed. Host-rendered dashboard preview: `dashboard-preview-rover.png` (shows `BASE/ROVER LINK` / `Wi-Fi Disconnected` in the offline fixture state).
- `pio run -e unit_a -e unit_b` — both builds SUCCESS.
- Live Wi-Fi OTA to both units (units unplugged from USB; `run_ota_live.cjs` browser workflow, acknowledged paired notices both directions):
  - Unit A: 0.11.3 → 0.11.4, ~68 s transfer, Update complete, new boot verified, Debug Off after restart, saved survey state unchanged (`unit-a-ota-evidence.json`).
  - Unit B: 0.11.3 → 0.11.4, ~46 s transfer, same PASS checks (`unit-b-ota-evidence.json`).
- These transfers were governed by the 0.11.3 receivers' new 300-second deadline — first real exercise of the raised limit (previous measured worst case: 118.6 s against the old 120 s limit).

## Outstanding

- Physical on-screen confirmation of the merged card by the operator (the host preview verifies the drawing path; the actual display is not remotely observable).
- Radio-state appearance (`Radio Connected` / `Radio Disconnected`) will first appear in the field once the production SiK session is selected; it has not been rendered on live hardware yet.
