# Loss-tolerant instrument transport — development plan

Approved 2026-09-12. Both UM980s stay disconnected until the synthetic transport milestones pass. The latest six-metre test lost three of 468 packets in each direction; the design must handle loss/corruption while investigation of the physical path continues.

## Delivery policy

| Traffic | Policy |
|---|---|
| RTCM corrections | Forward only complete, checksum-valid messages from the selected session. Bound queues and reassembly time, discard incomplete/expired messages and prefer newer data. First version does not retransmit corrections. |
| Commands | Separate acknowledged channel with request IDs, bounded retries and duplicate suppression; acknowledge completion separately from receipt. Persistent/external effects need existing durable command semantics before retry support is connected. |
| Status | Periodic latest-value updates with bounded priority and bandwidth. Status traffic cannot starve corrections. |

A CRC detects accidental corruption; it is neither error correction nor authentication. Sequence/session checks are protocol isolation, not authorization. Radio ECC remains an independently qualified option, not a substitute for receiver-side validation.

## Implementation stages

1. **Transport core and local instrument self-test (implemented and bench-verified).** Portable fixed-memory packet encoding/parser; selected session/source; fragment reassembly; RTCM CRC24Q verification; sequence duplicate suppression; bounded age/queue handling. An explicit browser action runs deterministic loss/corruption/reorder/replay/outage tests entirely on one ESP32, saving a separate downloadable report. No generated data goes to radio or UM980 in this stage.
2. **Paired synthetic transport over SiK.** Use that same core on the existing GPIO17 TX/GPIO18 RX UART2 at 57600 baud. Generate synthetic RTCM-shaped messages, use a validation sink instead of GNSS output, and inject repeatable faults at the transport boundary. Compare raw link counters with accepted complete messages. Preserve bounded main-loop budgets and avoid simultaneous ownership of the UART by the existing diagnostic and new transport.
3. **Real RTCM integration and readiness.** Parse Base UM980 COM2 input, send the selected correction source, validate/reassemble at Rover and forward complete RTCM to COM2. Integrate correction age, source/session changes and bounded output queues with current survey gates. Do not feed simultaneous Wi-Fi and SiK copies to the receiver. Only then reconnect the healthy Base and COM2-only temporary Rover for real RTCM/RTK-recovery tests.
4. **Reliable commands and periodic status.** Implement explicit acknowledgement/retry budgets and duplicate-safe execution independently from fresh correction delivery. Integrate remote commands only after durable operation/restart behavior passes. BLE remains a future adapter; no PC is required in the field.

## First-stage protocol

Version 1 uses a fixed 256-byte envelope: 32-byte little-endian header, up to 220 payload bytes, and CRC32 over the first 252 bytes. Header identifies type, source, explicitly selected nonzero session, nonzero message sequence, total message size, aligned fragment offset, payload length and sender queue age. Reserved bytes/padding must be zero. Types other than RTCM are rejected by this first receiver; command/status support is deferred.

RTCM is at most 1029 bytes (1023 payload plus six framing/checksum bytes), requiring at most five fragments. One active reassembly slot intentionally favors the newest message. A new higher sequence abandons an incomplete older message, duplicate fragments do not extend its timer, and completed/rejected message sequences cannot be replayed. Out-of-order fragments for the current message are accepted once. Session selection is explicit; wire input cannot change it. Sequence wrap requires a new session.

The first core uses 1000 ms local reassembly and 1500 ms sender-queue-plus-local-residence age. These are engineering starting points for synthetic tests, not a validated correction freshness limit. Queue age is recomputed at each transmission. This does **not** measure unobserved time already spent in modem/RF buffers before the first fragment arrives, and clocks on the two ESP32s are not assumed synchronized. Production forwarding requires an explicit transport latency bound or receiver/epoch freshness validation; current GNSS quality gates must remain. Report these limits honestly rather than treating arrival time as observation age.

## Acceptance

Local tests must cover clean/max-size RTCM, fragmented input, dropped/corrupt bytes, missing fragments, repeated/out-of-order fragments, wrong session/source, replay, newer-message preemption, malformed lengths/reserved fields, bad RTCM with a valid outer CRC, queue/reassembly expiry, timer wrap, and recovery on the next valid message. No damaged/incomplete RTCM may reach the sink; no partial output is allowed. The parser, sender and reassembler must use fixed memory with no data-path allocations.

A local self-test pass qualifies the algorithm on that instrument only. It does not qualify RF delivery, prove corruption impossible, or establish RTK/survey accuracy. Paired radio tests, modem/UART/power diagnosis, real receiver recovery and independent field check points are still required. Runs are finite, tablet-triggered, and reports remain available after browser disconnection.

Monitor the five-hour usage window. At 10% remaining, stop implementation, update this status and evidence, commit and push. Do not use the weekly window for this threshold.

## Stage 1 completion checkpoint

Implemented in `correction_transport.h` and `correction_transport_selftest.h`. Both ESP32 builds and flash hash verification passed. **22/22 fault checks passed on each instrument** through the actual browser page, in 51.045 ms on Base and 52.111 ms on Rover. The fixed test workspace is 4744 bytes on ESP32 (4760 on the 64-bit native test). Native tests also passed all 2048 individual envelope bit flips, an independent byte-order fixture, latest-queued-message replacement and 10000 mutated envelopes with receiver-bound checks.

On `/diagnostics`, take control and choose **Run local fault checks**, then **Download self-test report**. This is a separate local algorithm result, not a replacement for the latest radio report. Both reports survived an instrument restart unchanged; previous RF results and survey records remained intact. Neither radio nor UM980 receives synthetic data from this self-test. The report includes suite version, failed-case bitmask, runtime, memory and storage confirmation. A restart marker prevents a test interrupted by reset from appearing passed.

Failed-mask bit order (zero-based): 0 CRC vectors; 1 minimum frame; 2 maximum frame; 3 reversed fragment order; 4 duplicate fragment; 5 missing fragment/expiry; 6 corrupt envelope/recovery; 7 dropped byte/recovery; 8 inserted byte/recovery; 9 wrong source; 10 wrong session; 11 replay; 12 newer message preemption; 13 sender expiry; 14 receiver age budget; 15 timer wrap; 16 malformed fields/padding/type; 17 valid outer CRC but bad RTCM CRC; 18 invalid sender input/session reset; 19 noise resynchronization; 20 duplicate cannot extend lifetime; 21 inconsistent fragment message size.

**Next is stage 2: attach this core to paired synthetic SiK traffic and validate injected faults over the real transport.** It is not yet a radio adapter or production correction bridge. Before production, the scheduler must handle actual RTCM bursts and reference-message requirements; the first core's single newest-message slot is a bounded primitive, not a complete policy for retaining compatible observation groups and essential reference metadata. End-to-end freshness, reliable command delivery and periodic status remain outstanding. Do not reconnect UM980s yet.

See [test evidence](../tests/2026-09-12-correction-transport/README.md).
