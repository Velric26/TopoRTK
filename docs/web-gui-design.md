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


## Historical UI 0.8 control update

The Rover now offers PIN-free **Take control** with latest-request ownership. The Base still displays its PIN field. The Rover touchscreen replaces the unused WEB CONTROL PIN line with a Take control instruction. The remaining GUI layout is unchanged. See [takeover validation](../tests/2026-09-11-rover-takeover/README.md).

## Settings page (2026-09-15, R8)

`/settings` is reachable from every page on **both roles** and consumes the pair-selection API agreed in R6. It keeps three things visibly separate, because conflating them is how a queued request gets mistaken for a working link:

- **Selected** — the durable `selected_transport` that both instruments store and restore.
- **Pending** — `candidate_transport` and the live operation (kind, state, reason, coordinator and the phase countdown), so a switch in progress is never drawn as a completed one.
- **Connected** — `peer_connected` and `corrections_fresh`, reported on their own rows.

A switch is only ever sent from an explicit confirmation: the medium card reveals the interruption warning for that switch, and Confirm posts `{id, revision, op:'link.select', transport, confirm:true}` with a fresh 32-hex id and the revision this page last read. `202` means "queued", never "connected"; the outcome is published by the same endpoint and rendered as success only with `committed`, otherwise as the failure reason with the medium still in use. Stale-revision, busy, conflicting-id, cancelled and storage refusals each get their own operator sentence, and a `recovery_required` outcome points at the Link test page for a local selection on one instrument.

Reconciliation rules: the page polls and renders the server's operation, so a reload during a switch retrieves it and never repeats the request; a lost connection shows "no live settings" and disables every write control rather than guessing; a takeover invalidates the previous browser (401 drops the shared token) and its controls lock immediately; and Cancel is offered only for a request this instrument issued, which the instrument identifies by tag.

Sizing uses rem units with reflowing grids, so 200% text reflows the medium cards and metric rows instead of overflowing. Checks cover both roles, the four widths, the refused and unreachable outcomes, reload and HTTP-loss reconciliation, takeover and the absence of any PIN or password field.

## Current control and diagnostic update (2026-09-12)

Base and Rover now share PIN-free latest-request takeover. Survey and diagnostic pages remove the Base PIN field, and both touchscreen roles instruct the user to tap Take control. Survey quality gates and accepted-operation behavior are unchanged.

The Link test page shows connection freshness, controller ownership, matched settings, current progress, packet counters and a clearly labelled restored report. Controls lock when disconnected/stale; polling renews only the current lease. Session storage retains the browser's client/token across reload. Downloads select the current run before any older saved result. A queued-request message clears when the requested state arrives. The layout is checked at 320/390/768/1280 px and 200% text, including lost connection, stale snapshots, report downloads and both roles. See [standalone workflow](transport-field-test.md).
