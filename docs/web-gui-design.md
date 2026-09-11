# Web GUI, UI 0.7

The September 11 redesign uses a light field workspace with navy actions, clear form labels, and green for successful/verified states. It remains self-contained on the instruments, without external fonts, images, libraries or network dependencies. The existing browser scripts still submit the same typed commands; receiver logic, coordinate calculations, quality gates and journal formats are unchanged.

## Screen organization

- **Navigation:** Jobs, Setup, Collect and Points remain in the same order. Navigation stays at the bottom on phones and at the top on larger screens. Base mode shows its single setup screen without a redundant navigation bar.
- **Jobs:** clear create/open controls; storage and recovery are in an expandable section.
- **Setup:** four groups for coordinates/height, antennas, base reference and quality limits. Coordinates open initially. Native validation opens any group containing an invalid field before focusing it. Required confirmations and configuration revision behavior are retained.
- **Collect:** survey method, point ID and code are prominent. Optional description and linework expand when needed. Cancel appears while collection is active. The last confirmed observation remains separate from the current operation.
- **Points:** the plan and point list sit beside each other on larger screens and stack on phones. Downloads, reference targets and manual lines are expandable. The inspector has a Close review action. Original observations, metadata audit reasons and deletion/restore controls are retained.
- **Live status:** matching palette, compact metric cards, a prominent Survey link and existing expandable diagnostics. Loss of connection still clears live readiness/metrics.

Controls have visible keyboard focus and generous touch sizes. No animation is needed for meaning; the small button transitions respect reduced motion. Layout checks cover 320, 390, 768 and 1280 px, plus 200% text at 390 px. The active navigation label stays readable on hover.

Both units were flashed with hash verification and passed read-only browser checks. See [validation and screenshots](../tests/2026-09-11-gui-redesign/README.md). Actual phone/tablet keyboard behavior, sunlight readability and field occupations remain user/field acceptance work. This redesign does not change the [deferred Android responsibilities](esp32-android-feature-split.md).


## UI 0.8 control update

The Rover now offers PIN-free **Take control** with latest-request ownership. The Base still displays its PIN field. The Rover touchscreen replaces the unused WEB CONTROL PIN line with a Take control instruction. The remaining GUI layout is unchanged. See [takeover validation](../tests/2026-09-11-rover-takeover/README.md).
