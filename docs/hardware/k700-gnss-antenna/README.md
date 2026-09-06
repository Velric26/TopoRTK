# K700 Full-Band GNSS Antenna

> **Status:** Selected as the primary survey-antenna candidate. Electrical and field validation are on hold pending the correct coaxial cable.

## Intended Role

- Primary base and rover antenna candidate.
- Full-band L1/L2/L5 multi-constellation reception.
- Controlled performance comparison against the compact HA-609.

These capabilities are based on the supplied product description and are not yet validated by this project.

## 2026-09-05 Cable Blocker

The purchased cable does not mate with the antenna. The antenna side requires the opposite internal center-contact gender from the cable that was ordered: the required mating end has a male center contact, while the purchased cable has a female center contact.

The exact connector family has not yet been verified. Do not infer standard SMA versus reverse-polarity SMA, TNC versus reverse-polarity TNC, or another family from center-contact gender alone.

Before ordering a replacement, record:

1. Connector shell diameter and thread location.
2. Whether the antenna connector has internal or external threads.
3. Center contact: pin or socket.
4. Any model or connector markings.
5. Required UM980/carrier end connector and center contact.
6. Cable type, length, impedance, and stated frequency range.

Do not force the current cable or stack unidentified adapters. A wrong RF connector can damage the center contact and adapters add loss and mechanical failure points.

## Deferred Validation

After obtaining a verified 50-ohm cable, repeat the HA-609 standalone test with the same receiver, firmware, location, antenna orientation, power setup, and observation duration. Record time to first fix, satellites, HDOP, signal metrics where available, stability, and any antenna-current or open/short alarms.
