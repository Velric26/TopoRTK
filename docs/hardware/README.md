# Hardware Documentation

> Update hardware records only from manufacturer documentation, physical inspection, or validated measurements.

## Devices

| Device | Role | Documentation | Validation |
|---|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-3.5 | Controller, display, touch, logging, and communications | [Board documentation](waveshare-esp32-s3-touch-lcd-3.5/README.md) | Both displays and TTL2 links passed; Unit A touch passed; per-unit A/B firmware builds validated by 2026-09-06 |
| Unicore UM980 on BDRTK-980 | Base and rover RTK engine | [Module, carrier, and connected receiver](unicore-um980/README.md) | Both passed USB/TTL2 and automatic A/B configuration; Wi-Fi RTCM produced rover `RTK FIXED`; BESTNAV horizontal-accuracy parsing passed |
| Holybro SiK 1 W 915 MHz pair | RTCM correction link | [Radio documentation](../radio.md) | USB settings backed up and restart-verified; binary link acceptance, ESP32 wiring, range, and compliance pending |
| K700 GNSS antenna | Primary survey antenna | [K700 hardware note](k700-gnss-antenna/README.md) | On hold: antenna-side cable center-contact gender mismatch |
| HA-609 GNSS antenna | Compact comparison antenna | Planned | Battery-powered standalone GPS fix passed 2026-09-05; controlled comparison pending |
| BNO085 | Experimental orientation/tilt input | Planned | Pending |

## Power system

Each unit is planned around its own 3S 18650 pack, a direct battery branch for the Holybro 1 W radio, and a 12 V-to-5 V, 15 W buck converter for the ESP32 and BDRTK-980 carrier. The provisional fuse, charging, BMS, USB back-feed, and BNO085 supply requirements are recorded in the [power architecture](../power.md).

## Documentation Rules

For each hardware device, keep:

- One device folder with a `README.md` as its entry point.
- Locally embedded diagrams under `assets/`.
- Critical manufacturer documents under `references/`.
- Original URLs, retrieval dates, and checksums in `SOURCES.md`.
- Exact model/revision notes and links to real test records.

Do not assign a GPIO from a product image alone. Check the complete schematic for onboard conflicts, electrical levels, boot-strapping behavior, and power domains.

## System-Level Records

As assemblies are designed, add verified wiring, power, connector, antenna-reference, enclosure, and revision records here or in focused system documents. Never overwrite older test evidence when hardware changes.
