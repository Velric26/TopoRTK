# SiK bridge checkpoint — 2026-09-13

Implemented 0.10.4: explicit Base-generated session selection, exclusive Wi-Fi/SiK routing, fixed burst/reference queues, whole-frame COM2 admission, carried queue age and receiver-confirmed readiness. See [operation and limitations](../../docs/live-correction-bridge.md).

## Verification

- Both builds passed. Initial 0.10.4 flashes passed and the actual two-instrument browser workflow passed. A later counter-label update built and passed host tests, but its first upload attempt failed because COM4/COM10 disappeared. `flash-final.txt` records the retry after reconnection.
- `bridge-native.txt`: burst/reference retention, fairness, queue bounds, CRC/expiry/timer wrap, station/session isolation, replay, loss/corruption recovery and carried output age. Native queue 8340 bytes; bridge 10800 bytes, 10792 on ESP32.
- `transport-native.txt`: all 22 existing fault checks, 2048 single-bit errors and 10000 malformed envelopes. `pair-native.txt`: existing paired injection/recovery regression passed.
- `host.txt`: actual production queue/writer under backpressure, expiry, exclusive route, profile inhibition and surfaced short-admission fault; freshness/receiver-age regressions and status serialization passed. The status RTCM count now reflects whole-frame UART admissions for either route; packet-gap/error labels explicitly identify Wi-Fi.
- `survey.txt`: survey persistence/quality regressions and 96 independent PROJ UTM cases passed (worst difference 0.760 mm).
- `browser-results.json`: responsive embedded page, takeover without PIN, live-session actions, disabled diagnostics while SiK is selected, offline controls and reports passed.
- `hardware-results.json` and `hardware-log.txt`: actual Base/Rover session selection, invalid session rejection, diagnostic exclusion, explicit Wi-Fi return, new-session generation and restart restoration passed. Jobs, existing radio reports and local self-test reports remained unchanged. No synthetic correction data was sent to GNSS. These checks ran with UM980s disconnected; the script deliberately refuses connected/verified receivers.

The first hardware attempts encountered one busy admission and then stale previous-error snapshots in the test runner. The runner now waits for the POST response and a subsequent instrument snapshot, and retries only explicit service-busy rejection. Final control-plane checks passed; these were not RF delivery experiments.

## Latest receiver state

The operator connected both UM980s after the control-plane tests. Both receiver profiles are verified. Rover is online with GGA quality 0, zero satellites, no valid position, no observation freshness and readiness false. Both instruments have no base reference at this initial inspection. No real SiK RTCM/RTK recovery result is claimed. Antenna/sky conditions were requested. Saved jobs remain unchanged and no point was collected.

Next: confirm receiver identity/antenna sky view, observe live reference/MSM generation, select matching SiK sessions and measure actual correction age and outage/recovery. Packet-loss root cause remains deferred. Persistent automatic radio pairing, absolute modem/epoch latency bounds, reliable commands and field accuracy acceptance remain outstanding.


Final pause: five-hour allowance reached **91% used / 9% remaining**. Operator confirmed GNSS antennas are connected but have no sky view indoors; PC USB must be disconnected to move outdoors. No live correction/recovery test was started. Follow the outdoor handoff in `docs/live-correction-bridge.md`; keep all instrument components powered, select a fresh session after any restart and validate receiver quality before testing outage recovery. Implementation stopped; documentation and backup are the remaining checkpoint actions.


Final deployment: `flash-final.txt` confirms both **unit_a and unit_b succeeded with flash hash verification** after USB reconnection. Both now have the final 0.10.4 counter/label update. No further hardware tests were started after the usage threshold.
