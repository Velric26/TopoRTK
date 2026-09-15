# R1 parser extraction deployment — 0.11.6-arch-r1 (2026-09-14)

Deployment of the [architecture review](../../docs/architecture-review.md) R1 checkpoint to both units using the scripted OTA process. R1 is a behavior-identical refactor: UM980 ASCII parsing moved into a standalone `gnss_parser` module behind the planned `GnssParseStats` contract. No receiver, radio, UI or storage behavior changed; the version string was bumped from `0.11.5` so the refactor image is distinguishable from stock.

## Software verification (deployed source)

- `test/run_host_tests.py` — all blocks PASS, re-run after the version bump.
- Standalone parser build fed the existing BESTNAV fixtures: valid parse (height 2204, epoch `2435*604800000+432000000`, station 7, differential age 1200 ms), two invalid-data rejections, corrupt CRC rejected with exactly one `GnssParseStats.checksum_errors` increment. This compile caught one hidden dependency (`std::snprintf` arriving via `Arduino.h`), fixed with an explicit `<cstdio>`.
- `pio run -e unit_a -e unit_b` — both SUCCESS.
- Packages verified before flashing: TPK1 header, unit bytes A/B, embedded version `0.11.6-arch-r1`, header CRC32, image SHA-256.

## OTA deployment (scripted runner, peer notices over Wi-Fi)

Run per the [OTA operator guide](../../docs/ota-operator-guide.md) from the repository root. Evidence stays private under `.pio/ota-live-<timestamp>/`.

| Unit | Evidence directory | Peer notices | Result |
|---|---|---|---|
| A (Base, 192.168.100.20) | `.pio/ota-live-1789429966607` | preparing → updating → reconnected (~24 s) | Update complete; new boot verified; Debug On after restart; saved survey state unchanged; no browser errors |
| B (Rover, 192.168.100.19) | `.pio/ota-live-1789430022453` | preparing → updating → reconnected (~117 s) | Same |

Post-update API state on both units: firmware `0.11.6-arch-r1`, boot text `New firmware verified`, update idle, receiver profile verified, corrections route Wi-Fi with no fault, Debug On, storage ready with records/jobs unchanged (A: 2 journal records; B: 0).

The runner is plain CommonJS; this session executed it under Bun 1.4 with the project's `test/node_modules` Playwright because Node is not on the automation shell PATH. Runner copies used for fresh module execution were temporary and removed.

## Operational findings (now documented in the OTA guide)

1. **Peer notices ride the selected correction transport.** After the SiK bench session both units were still on the SiK route with no radio exchange, so OTA preparation could not be acknowledged. Switching corrections back to Wi-Fi on both units (`POST /api/v1/diagnostic {"op":"corrections","transport":"wifi","confirm":true}`) restored the path.
2. **A held survey/diagnostic reservation rejects preparation** with `Finish collection, receiver setup and diagnostics first`. A restart clears a held reservation; targets must be idle before updating.
3. **The runner aborted safely in every missing-prerequisite case** (no peer acknowledgement; gate rejection) — no flash occurred in either case. `attempt` remained 0 in NVS until the admitted update.

## Outstanding

- Operator confirmed both touchscreens render unchanged from 0.11.5 after the update (2026-09-14).
- The reservation hold observed before the restart was not root-caused; the restart remediation and idle-target prerequisite are documented instead.
