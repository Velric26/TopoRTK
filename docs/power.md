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

## USB and back-feed rule

USB can also supply 5 V to the ESP32 and BDRTK carrier. During external-power tests, disconnect USB power or use a data-only USB cable unless the board's power-path isolation has been verified. Do not parallel an external 5 V regulator with USB 5 V unintentionally.

## Open validation items

- Measure per-unit current at radio receive, radio transmit, display startup, Wi-Fi activity, and SD writes.
- Measure 5 V voltage at the ESP32 and UM980 while the radio transmits.
- Confirm the BNO085 breakout's exact supply and logic-level requirements.
- Select the final BMS, charger, fuse holder, switch, connectors, and enclosure strain relief.
