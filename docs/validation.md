# Validation Plan

> Status: canonical index. The repeatable procedures and acceptance criteria themselves live in the homes below; dated results belong under `tests/`.

## General test requirements

Every test must identify hardware, firmware, configuration, antennas, base coordinates, reference values, environment, procedure, raw evidence, and pass/fail criteria.

## Acceptance homes

| Home | What it holds |
|---|---|
| [Architecture review — Verification](architecture-review.md#verification) | Review/publication evidence boundary, the build and offline command set for future firmware checkpoints, and the risk-bearing behavior checks R1–R11 |
| [Architecture review — R11 hardware and field acceptance](architecture-review.md#r11--actual-hardware-and-field-acceptance) | Operator hardware procedure: settings matrix, outdoor SiK acquisition and safe outage recovery, sunlight and independent survey qualification |
| [R11 release gate record](../tests/2026-09-15-r11/README.md) | The staged two-unit gate: scripted pair matrix, deployment runbook and the physical checks the script cannot cover |
| Dated records under `tests/` | Results, evidence and limitations for each checkpoint, linked from PROJECT.md's history entries |

## Regression set

Run the affected native and browser checks at each checkpoint, and the full build and offline set in the review's [build and offline checks](architecture-review.md#build-and-offline-checks-for-future-firmware-checkpoints) once after concurrent work integrates, followed by the applicable hardware gate above.

## Antenna comparison (K700 versus HA-609)

> Status: On hold as of 2026-09-05. The purchased K700 cable has the wrong antenna-side center-contact gender. Confirm the exact connector family, shell gender, and center-contact gender before ordering a replacement; do not force or adapt an unidentified RF connector.

Comparison parameters when it resumes: the same points, receiver settings, observation periods and environmental conditions for both antennas.
