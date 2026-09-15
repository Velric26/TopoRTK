# R2a UI extraction deployment — 0.11.7-ui-r2a (2026-09-14)

Deployment of the [architecture review](../../docs/architecture-review.md) R2a checkpoint: touchscreen rendering, primitives, repaint cache, backlight and the touch state machine moved out of `main.cpp` into `ui_theme.h`, `ui_display.h/.cpp`, `ui_screens.h/.cpp` and `touch_input.h/.cpp` — with **no visual or interaction change by design**. The composition root now builds one `UiFrame` per render tick (pre-formatted, ready-to-draw content) and executes typed touch gestures; status policy (warning selection, fix labels, page readiness) stays in the root until R4's `instrument_status`.

## Software verification

- Host tests (`test/run_host_tests.py`): all blocks PASS, including production touch navigation/cancellation, UI bounds and the region repaint cache.
- **Rendered-surface comparison**: the seven host-rendered PNG previews (`ui-0`–`ui-5`, `ui-base-selection`) are byte-identical (SHA-1) to the pre-extraction baseline captured before the refactor.
- `pio run -e unit_a -e unit_b` — both SUCCESS.
- The runner compiles `ui_display.cpp`/`ui_screens.cpp` against the `test/host_hardware.h` adapter (hardware includes stripped, adapter prepended) instead of concatenating them; `touch_input.cpp` compiles as-is (pure logic). Host stub objects became per-translation-unit (`static`), with the PWM stub state shared through inline accessors so hardware-oracle asserts still observe the backlight writes.

## OTA deployment (scripted runner)

| Unit | Result |
|---|---|
| A (Base) | Update complete; new boot verified; Debug On; saved survey state unchanged; no browser errors |
| B (Rover) | Same |

Post-update API state on both units: firmware `0.11.7-ui-r2a`, boot text `New firmware verified`, idle, receiver profile verified, corrections on Wi-Fi, storage intact (A: 2 records; B: 0).

## Implementation notes

- `local_time_utc_minus_6` became a pure function of `(now, GnssTimeData)` in `ui_display`; `service_brightness`/`setup_backlight`/`automatic_brightness_target` take explicit time and mode parameters.
- `read_touch` (FT6336 I2C reads) intentionally stayed with the board wiring in `main.cpp`; R10a's `board_hardware` owns it next.
- Status drift cleanup was NOT attempted here (Wi-Fi-only CSV/labels remain) — that is R4's transport-aware snapshot.
- One latent defect fixed en route: the new touch wrapper initially passed `read_touch` results and coordinates in one call with unspecified argument evaluation order; sampling is now explicitly sequenced.

- Operator confirmed physical touch navigation on both panels after the update (2026-09-14): swipes, tab taps, Setup apply and Phone key actions behave as before.

