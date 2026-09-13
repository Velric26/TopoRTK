# Packet loss investigation — deferred, not a development gate

Decision: 2026-09-12, at the operator's request. Occasional radio loss is an expected operating condition. Investigation remains useful, but **100% delivery is not required before developing freshness, safe recovery and real RTCM integration**. Do not erase or relabel the historical results: their `pair_pass` means complete eligible delivery under that particular diagnostic, not overall system suitability.

## Evidence preserved

| Run | Separation | Profile | Eligible complete messages at Rover | Wire CRC errors | Invalid sink output observed |
|---|---|---|---:|---:|---:|
| 913225 | Not reconfirmed | Injected | 19/20 | 8 | 0 |
| 913230 | 20 cm | Clean | 25/30 | 5 | 0 |
| 913231 | 20 cm | Injected | 17/20 | 8 | 0 |
| 913232 | 8–10 m | Clean | 27/30 | 3 | 0 |
| 913233 | 8–10 m | Injected | 15/20 | 10 | 0 |

Each injected 60-second run deliberately corrupts five fragments and omits five, so ten generated messages are intentionally ineligible. These are message-delivery figures, not measured RF packet-loss percentages. Original raw 256-byte SiK tests also recorded loss at six metres: 465/468 received in each direction in run 913205.

The 0.10.2 tests reported no ESP32 UART FIFO overflow, receive-buffer-full, framing, parity or break events; no short writes or transmit-backpressure polls. Zero reported events does not rule out electrical/driver/modem issues. Service intervals reached about 400 ms; the responsible work has not been isolated. At 20 cm the reported supplies were ESP32 USB and radio 3S 18650, with common ground. After relocation the exact USB supply was not reconfirmed. Software roles remained Base at .20 and Rover at .19; physical labels should not determine USB identity.

Evidence: [paired RTCM](../tests/2026-09-12-correction-pair/README.md), [clean/injected and UART](../tests/2026-09-12-correction-uart/README.md), [six-metre raw test](../tests/2026-09-12-standalone-tablet/README.md). Preserve those records and radio settings for later comparisons.

## Revisit when useful

Compare bounded envelope pacing, identify long main-loop work, and examine radio/UART/supply conditions under controlled spacing and load. Reopen urgently if loss prevents useful correction freshness or receiver recovery, queues overflow persistently, invalid data escapes validation, or collection gates can be bypassed. Do not keep changing distance or RF settings merely to obtain a perfect synthetic delivery score.

## Current development acceptance

- Validate framing/checksums, selected source/session and sequence handling; reject incomplete, corrupt and replayed data.
- Bound queues and age; prioritize useful fresh correction traffic without building a backlog of old corrections.
- On loss/outage, show stale/waiting status and inhibit collection under the configured survey quality rules.
- Resume automatically on valid fresh data and receiver-confirmed quality. A heartbeat or reference-only RTCM message must not imply fresh observations.
- Verify actual correction age, RTK solution recovery and field checks with UM980s after the forwarding implementation is ready. Zero invalid outputs in a finite synthetic run is evidence, not proof against every failure.

Reliable acknowledged commands remain separate from correction freshness. Radio root-cause work is deferred; these safety and recovery requirements are not.
