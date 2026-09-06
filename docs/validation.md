# Validation Plan

> Status: Placeholder. This document defines repeatable procedures and acceptance criteria; dated results belong under `tests/`.

## General Test Requirements

Every test must identify hardware, firmware, configuration, antennas, base coordinates, reference values, environment, procedure, raw evidence, and pass/fail criteria.

## Bench Tests

Define power, UART, wired RTCM, logging, restart, radio throughput, and failure-recovery procedures.

## Field Tests

Define known-point repeatability, time to fix, horizontal/vertical error, baseline length, range, obstruction, multipath, and radio-loss procedures.

## Antenna Comparison

Define a controlled K700 versus HA-609 comparison using the same points, receiver settings, observation periods, and environmental conditions.

> Status: On hold as of 2026-09-05. The purchased K700 cable has the wrong antenna-side center-contact gender. Confirm the exact connector family, shell gender, and center-contact gender before ordering a replacement; do not force or adapt an unidentified RF connector.

## Acceptance Criteria

| Test | Metric | Limit | Repetitions | Evidence required |
|---|---|---|---:|---|

## Regression Set

List the minimum tests that must pass after firmware, wiring, GNSS, radio, antenna, power, or enclosure changes.
