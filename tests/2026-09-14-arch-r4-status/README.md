# R4 transport-aware status checkpoint — 0.11.15-arch-r4 deployed to both units (2026-09-14)

Implementation of the [architecture review](../../docs/architecture-review.md) R4 checkpoint: one central `InstrumentStatus` interpretation consumed by the LCD frame, the web JSON and the CSV logger, so the surfaces can never disagree on transport, freshness or readiness. Both instruments now run **`0.11.15-arch-r4`** via guarded OTA with verified boots and unchanged saved survey state. (Executed after the [repository restructure](../../PROJECT.md#deployment-history); all paths below use the new layout.)

## Contract implemented

- **`firmware/src/instrument_status.h`** — fixed-size snapshot with four separate facets (**peer** incl. transport-specific signal availability, **corrections** age/freshness, **GNSS** incl. required-fix, **readiness**), plus a transport classification (`None`/`WiFi`/`Radio`). `evaluate(Inputs)` moves the pre-R4 ready/age policy **verbatim** (thresholds unchanged): 3 s Wi-Fi peer window, Base-direct client requirement, Bridge-linked radio semantics (a Base's transmit-only bridge never reports receiver freshness), Rover correction age ≤ 3000 ms / Base RTCM-output flag, role-matched required fix (7/4) within 3 s, and the `system_ready` composition. All inputs are passed explicitly by the composition root — no globals, naming stays with the network/link owners, radio RSSI is *unavailable* (never a fabricated Wi-Fi dBm).
- **Consumers routed through one snapshot** (`status_snapshot()` in main.cpp):
  - LCD: dashboard and Link page are transport-aware — during SiK the Link page reports `LINKED` with `N/A (RADIO)` signal instead of Wi-Fi peer state, and the dashboard shows `Radio Connected`.
  - Web JSON: `transport`, `rssi_dbm`, `peer_age_ms`, `quality`, `correction_link_connected` derive from the snapshot; **keys unchanged**.
  - CSV: `solution.csv` gains a `link_transport` column (per-session header, so historical exports are untouched), and `link_rssi_dbm` is written **empty** when unknown (radio) instead of a stale/zero value; `LINK` CONNECTED/DISCONNECTED events now follow the selected transport.
- Correction safety, `Engine::quality` (job/collection authority), and all wire formats are unchanged.

## Host fixtures extended

- `test/host_hardware.h`: `SD_MMC`/`File` are now memory-backed so CSV content is assertable on the host.
- `run_host_tests.py` stubs gained `host_radio_linked` to drive the radio facet.
- New agreement block in `firmware_cases.h`: in **radio** mode the web JSON reports `"transport":"SiK RADIO"`, `rssi_dbm:null`, `peer_age_ms:null`, the LCD shows `Radio Connected` / `N/A (RADIO)`, and the CSV row carries an empty `link_rssi_dbm` with `link_transport=SIK`; in **Wi-Fi** mode everything flips consistently (`DIRECT LINK`, real station RSSI −48, `link_transport=WIFI`). LCD, browser and CSV agree in both modes.

## Software verification

- Full host suite: all 7 blocks + framing cases PASS (8 PASS lines), including the new agreement block.
- `pio run -e unit_a -e unit_b`: both SUCCESS. Packages: 1,242,960 bytes, identity-checked, `0.11.15-arch-r4`.

## OTA deployment (guarded runner, sequential)

| Unit / role | Final firmware | Result |
|---|---|---|
| A / Base | `0.11.15-arch-r4` | PASS first attempt: acknowledged peer notices, verified boot, Debug On, unchanged saved survey state (records 2) |
| B / Rover | `0.11.15-arch-r4` | PASS on retry: the first attempt raced Unit A's own restart (peer acknowledgement window expired — no override authorized, nothing flashed); the retry completed with verified boot, Debug On and unchanged state (records 0) |

Post-deploy live API check: Rover status reports `transport: LOCAL ROUTER`, live RSSI −43 dBm, `connected: true`; both units' profiles verified, storage intact.

## Outstanding

- Direct-sunlight readability acceptance on both physical panels (R2b human gate, unchanged).
- Outdoor SiK RTK acceptance (independent gate; now also the natural place to observe the transport-aware CSV/log output under real radio).
- Next checkpoint per the review: **R5** (automatic dedicated-pair bootstrap over either medium) — depends on R3+R4, both now landed.
