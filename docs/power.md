# Power Architecture

> Status: provisional bench architecture. Validate current draw, voltage sag, thermal behavior, and charging protection before field use.

## Per-unit power tree

Unit A and Unit B each have an independent 3-cell lithium-ion battery pack:

```text
3S 18650 pack (12.6 V full; approximately 9-10 V near cutoff)
        |
        +-- Holybro SiK 1 W radio via XT30
        |
        +-- 12 V-to-5 V, 15 W buck converter
              +-- Waveshare ESP32-S3 through its regulated 5 V input
              +-- BDRTK-980 carrier through 5V_IN
              +-- BNO085 through the appropriate 3.3 V/regulator input
```

The Holybro 1 W radio is specified for 7-28 V DC through XT30. The BDRTK-980 carrier is specified for 4.0-5.5 V, typical 5 V, at approximately 160 mA. The bare UM980 module is a different part and requires a 3.0-3.6 V rail; do not apply 5 V directly to a bare module.

## Regulator requirements

- Confirm the buck converter input range includes the full battery range and its intended low-voltage cutoff.
- Set the output to approximately 5.0 V and measure it under load before connecting the boards.
- A 15 W converter provides up to 3 A at 5 V. Verify its thermal rating and startup behavior.
- Keep radio and regulator power branches short; use a common/star ground for the radio, ESP32, and UM980 serial interface.
- Add local bulk capacitance if radio transmit bursts or display/Wi-Fi activity causes resets.
- The BNO085 chip is a 3.3 V device. Use 3.3 V unless the specific breakout board documents its own regulator and level shifting.

## Fuse

Install one fuse in the positive lead of each battery pack, physically close to the cell holder:

- Initial recommendation: **3 A time-delay fuse**.
- Use an automotive mini-blade fuse or 3.15 A slow-blow fuse rated for at least 32 V DC.
- Size the fuse below the safe current rating of the holder and wiring.
- Measure the maximum operating and startup current; select the next standard rating above the measured peak rather than increasing the fuse arbitrarily.

The fuse protects the battery wiring from shorts. It does not provide cell balancing or safe charging and is not a substitute for a BMS.

## Temporary operation without a BMS

For controlled bench testing only:

- Use three matched cells of the same chemistry, capacity, and similar age.
- Measure individual cell voltages before inserting them in series.
- Remove the cells and charge them separately with a proper single-cell charger.
- Never charge one cell while the cells remain connected as a series pack.
- Do not operate until one cell is deeply discharged; total pack voltage can hide cell imbalance.
- Secure the holder and contacts against vibration before field testing.

Before field deployment, add a 3S BMS with balancing, over-charge, over-discharge, over-current, and short-circuit protection, together with a proper 12.6 V CC/CV pack charger.

## State-of-charge monitoring (open — find the cheap, simple way)

Motivation: on 2026-09-15 a unit's 3S pack ran flat during a bench session with no warning from the instrument or the web UI; it simply stopped, and the dark panel was initially mistaken for a dead board. Nothing on either unit measures the pack today.

Constraints any solution has to respect:

- The pack is **3S (12.6 V full, roughly 9-10 V near cutoff)**, so the common single-cell fuel gauges (MAX17048, MAX17055, LC709203F) do **not** apply as-is: a pack-level measurement is required.
- An **I²C bus already exists** (the ESP32 talks to the TCA9554 expander), so an I²C sensor adds no new bus wiring.
- The value must reach the touchscreen card and `/api/v1/status`, and the project rule holds: an unmeasured value is `null`, never a fabricated percentage.
- USB can back-feed 5 V, so the sense point must be the pack itself (after the fuse) and a charging state must not be reported as load state.

Candidate options, cheapest first:

| Option | Cost | Effort | What it gives | Caveats |
|---|---|---|---|---|
| 3S low-voltage alarm/buzzer on the balance lead | ~US$1 | none, no firmware | audible warning below a set cell voltage | no remote reading, no logging, no percentage |
| Resistor divider into a free ESP32-S3 **ADC1** pin | cents | small firmware change | pack voltage; rough SoC from a discharge curve | voltage-only SoC sags under radio TX/display load; ADC2 conflicts with Wi-Fi; needs a divider ≤ ~3.1 V and a calibration point |
| **INA226** (or INA219) on the existing I²C bus, high-side shunt | ~US$2 | small firmware change | pack voltage **and** current; SoC by coulomb counting; also answers the current-draw items below | needs a shunt and a characterization run; the percentage is a compensated estimate, not a magic number |
| Multi-cell gauge or 3S BMS with telemetry (BQ34Z100-class or a BMS with a data header) | US$5-15 | medium firmware and configuration | true SoC **and** per-cell voltages (cell imbalance is currently invisible) | more configuration and a larger part; likely overkill before the final pack and BMS are chosen |

Preference, subject to the final pack and BMS choice: an **INA226 on the existing I²C bus**. It is the cheapest part that yields both a defensible SoC (coulomb counting with a voltage fallback) and the current measurements this document already needs, adds no new bus, and leaves open the per-cell BMS-with-telemetry upgrade if a pack-level estimate proves too coarse. A divider on an ADC1 pin remains the zero-cost first reading, and a low-voltage alarm is worth having regardless as a bench safety net.

Report it honestly until the pack is characterized: publish voltage and current, leave the percentage `null`, and never present a voltage reading alone as a full-charge claim.

## USB and back-feed rule

USB can also supply 5 V to the ESP32 and BDRTK carrier. During external-power tests, disconnect USB power or use a data-only USB cable unless the board's power-path isolation has been verified. Do not parallel an external 5 V regulator with USB 5 V unintentionally.

### Switched radio rail (requested 2026-09-15, open)

The SiK module is currently fed straight from the 3S pack, so it draws idle current even when Wi-Fi carries production. The operator request is that a Wi-Fi-selected instrument leaves the radios **off** until radio communication is requested. That needs a switched rail (see [radio.md](radio.md#radio-power-control-requested-2026-09-15-needs-hardware) for the electrical detail and the UART back-power caveat), the chosen GPIO recorded here and in `board_hardware`, and a measured idle-current comparison as its acceptance. The related State-of-charge item below is the other half of the same problem: the pack can currently run flat without the instrument noticing.

## Open validation items

- Measure per-unit current at radio receive, radio transmit, display startup, Wi-Fi activity, and SD writes.
- Measure 5 V voltage at the ESP32 and UM980 while the radio transmits.
- Choose and bench-validate the state-of-charge measurement above: confirm the sense point at the pack, identify a free ADC1 pin if the divider route is taken, check the I²C address budget alongside the TCA9554, and characterize the pack's discharge curve (voltage and current against a controlled load) before publishing any percentage.
- Confirm the BNO085 breakout's exact supply and logic-level requirements.
- Select the final BMS, charger, fuse holder, switch, connectors, and enclosure strain relief.
