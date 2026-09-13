# Loss-tolerant instrument transport — development plan

Approved 2026-09-12; acceptance revised by the operator later that day. **Radio-loss root-cause investigation is deferred and perfect delivery is not a prerequisite for continuing.** The core must reject invalid data, bound freshness and recover safely under loss. See [deferred investigation and evidence](packet-loss-investigation.md). Keep UM980s disconnected until the real forwarding implementation and connection checks are ready; this is a software-readiness condition, not a zero-loss requirement.

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

The historical paired diagnostic's `pair_pass` is a **delivery-completeness measurement**, not a gate for all subsequent development. Continue stage 3 using the demonstrated rejection/recovery behavior, preserving the loss evidence for later. System acceptance is trustworthy collection and useful recovery under realistic loss: valid fresh corrections, receiver-confirmed quality and safe inhibition during outages. Old checkpoint statements below that block stage 3 solely on incomplete delivery are superseded by this decision.

Local tests must cover clean/max-size RTCM, fragmented input, dropped/corrupt bytes, missing fragments, repeated/out-of-order fragments, wrong session/source, replay, newer-message preemption, malformed lengths/reserved fields, bad RTCM with a valid outer CRC, queue/reassembly expiry, timer wrap, and recovery on the next valid message. No damaged/incomplete RTCM may reach the sink; no partial output is allowed. The parser, sender and reassembler must use fixed memory with no data-path allocations.

A local self-test pass qualifies the algorithm on that instrument only. It does not qualify RF delivery, prove corruption impossible, or establish RTK/survey accuracy. Paired radio tests, modem/UART/power diagnosis, real receiver recovery and independent field check points are still required. Runs are finite, tablet-triggered, and reports remain available after browser disconnection.

Monitor the five-hour usage window. At 10% remaining, stop implementation, update this status and evidence, commit and push. Do not use the weekly window for this threshold.

## Stage 1 completion checkpoint

Implemented in `correction_transport.h` and `correction_transport_selftest.h`. Both ESP32 builds and flash hash verification passed. **22/22 fault checks passed on each instrument** through the actual browser page, in 51.045 ms on Base and 52.111 ms on Rover. The fixed test workspace is 4744 bytes on ESP32 (4760 on the 64-bit native test). Native tests also passed all 2048 individual envelope bit flips, an independent byte-order fixture, latest-queued-message replacement and 10000 mutated envelopes with receiver-bound checks.

On `/diagnostics`, take control and choose **Run local fault checks**, then **Download self-test report**. This is a separate local algorithm result, not a replacement for the latest radio report. Both reports survived an instrument restart unchanged; previous RF results and survey records remained intact. Neither radio nor UM980 receives synthetic data from this self-test. The report includes suite version, failed-case bitmask, runtime, memory and storage confirmation. A restart marker prevents a test interrupted by reset from appearing passed.

Failed-mask bit order (zero-based): 0 CRC vectors; 1 minimum frame; 2 maximum frame; 3 reversed fragment order; 4 duplicate fragment; 5 missing fragment/expiry; 6 corrupt envelope/recovery; 7 dropped byte/recovery; 8 inserted byte/recovery; 9 wrong source; 10 wrong session; 11 replay; 12 newer message preemption; 13 sender expiry; 14 receiver age budget; 15 timer wrap; 16 malformed fields/padding/type; 17 valid outer CRC but bad RTCM CRC; 18 invalid sender input/session reset; 19 noise resynchronization; 20 duplicate cannot extend lifetime; 21 inconsistent fragment message size.

Stage 2 now has a dedicated synthetic SiK adapter and tablet action, described below. This is not a production correction bridge. Before production, the scheduler must handle actual RTCM bursts and reference-message requirements; the first core's single newest-message slot is a bounded primitive, not a complete policy for retaining compatible observation groups and essential reference metadata. End-to-end freshness, reliable command delivery and periodic status remain outstanding. Do not reconnect UM980s yet.

See [test evidence](../tests/2026-09-12-correction-transport/README.md).

## Stage 2 implementation

`correction_pair_test.h` drives a finite Base-to-Rover test over the existing SiK UART2. On both instruments, take control at `/diagnostics`, choose the same fresh six-digit **test code**, duration and **RTCM profile**, confirm preparation, then choose **Arm RTCM test**. The test code is session matching, not a control PIN. This action always uses SiK, Base → Rover, one 600-byte synthetic message every two seconds; the raw link mode/rate selectors do not apply. Raw Wi-Fi/SiK tests and local self-tests remain separate choices. The tablet may disconnect during a run; download the latest report from each instrument afterward.

The injected profile cycles every six messages through: clean, duplicate first fragment, corrupt middle fragment CRC, omit middle fragment, reverse all fragments, clean recovery. The clean profile injects none of these faults. Each message normally uses three RTM1 envelopes. Sender queue age is updated at the actual adapter write attempt. Messages with intentionally corrupt/omitted fragments must never reach the sink; every eligible message must recover byte-for-byte exactly once for a delivery pass. Payloads identify the run and sequence and carry valid RTCM framing/CRC24Q, but contain **no real satellite observations**. Nothing is forwarded to GNSS. Additional RF loss can make the delivery check fail while the sink still rejects invalid output correctly. Zero observed integrity violations is not proof against all faults.

CRC-protected 256-byte RTC1 diagnostic envelopes carry hello, result and abort, with explicit matching version/profile/run/duration/opposite role. They are not production remote commands. Both instruments must arm within 120 seconds; a two-second start delay, finite selected duration, five-second drain and bounded 60-second result repetition require no PC relay or tablet heartbeat. The existing survey admission gate and single UART owner exclude concurrent raw diagnostics/probes/occupations. RX work is limited to 2048 bytes and TX to one envelope per service iteration. Both fixed scanners see the UART stream; RTC1 control bytes count as discarded noise in the RTCM scanner and are not a meaningful RF-loss metric.

Reports replace the latest **radio** report and keep the separate local self-test report. Download previous results first. They include eligible complete-message counts, deliberate fault counts, CRC/reassembly counters, invalid sink outputs, transmission expiry, peer summary and storage confirmation. A restart during a test is reported interrupted; it never auto-resumes. A Base local pass verifies generated traffic; a paired pass additionally requires the Rover delivery result. The first paired profile covers only Base → Rover correction traffic; reverse/command reliability remains stage 4.

Native coverage includes deterministic injection/recovery, extra simulated RF loss, timer wrap, wrong session, missing peer, corrupt control and cancellation. Hardware/build status and reusable commands are recorded in [paired-test evidence](../tests/2026-09-12-correction-pair/README.md). Stage 3 remains blocked on reviewing this evidence and defining production burst/reference retention, freshness and receiver output backpressure.

**Hardware checkpoint:** both units flashed/hash-verified at 0.10.1. Run 913225 completed with the browser offline for 20 seconds. Rover recovered **19/20 eligible messages**, with zero invalid outputs observed, so **paired delivery did not pass**. Both downloaded reports survived restart unchanged. ESP32 paired workspace is 4164 bytes. Next is a selectable clean profile and UART fault telemetry under documented spacing/supply conditions to investigate additional loss; do not proceed to real receiver forwarding or reconnect UM980s yet. The first injected profile is implemented, but stage 2 acceptance remains open.

Implementation paused at the user's five-hour threshold: **90% used / 10% remaining** on 2026-09-12. Native/browser checks, both builds/flashes and report persistence checks completed; both instruments are idle. Resume with the diagnostic work above, not stage 3. This pause is unrelated to the weekly allowance.

## Stage 2 continuation: clean comparison and UART observations

Resumed after the five-hour window reset. Firmware 0.10.2 adds a **RTCM profile** selector: `clean` sends all messages without deliberate faults; `injected` retains the six-message fault cycle. Match the code, duration **and profile** on both instruments. RTC1 byte 7 carries the profile (0 clean / 1 injected), so unlike profiles cannot start each other. The diagnostic report suite version is now 2; API requests without a profile retain the earlier injected behavior, while the new browser defaults to clean. Production RTM1 framing is unchanged.

Each paired run reports UART FIFO overflow, receive-buffer-full, framing, parity and break event counts through the installed Arduino HardwareSerial error callback, plus bytes read/written, peak sampled receive backlog, maximum service interval, output-backpressure polls and short writes. The callback runs in the UART event task and only increments protected counters. Counters reset at arming and freeze at local completion/cancellation; they include handshake/drain traffic but exclude subsequent result repetitions. Callback timing is approximate, and a dropped/late event may not be represented. Zero reported events cannot establish that the whole electrical/RF path was error-free. Parity checking is not enabled in 8N1. Delivery pass and UART health remain separate facts.

No baud, RF power, air rate, antenna, pin, UART FIFO threshold or buffer-size changes are part of this comparison. Diagnostic report capacity is increased to 6144 bytes; the HTTP copy uses checked allocation rather than expanding its task stack, while transport data-path buffers remain fixed. Reports identify the first missing eligible sequence (zero if none; only meaningful at Rover completion).

Operator-confirmed bench setup: **20 cm antenna spacing**, each ESP32 powered directly by USB, radios powered from a **3S 18650 pack**, shared ground, antennas fitted and UM980s disconnected. These comparisons do not qualify field range. See [current evidence](../tests/2026-09-12-correction-uart/README.md) for results and the next checkpoint.

Both 0.10.2 builds/flashes, native and browser tests passed. At 20 cm, run 913230 recovered **25/30 clean messages**; run 913231 recovered **17/20 eligible injected-profile messages**. Both observed zero invalid sink output and zero reported UART error events, but paired delivery did not pass. Peak Rover backlogs were 768/1024 bytes and service intervals reached 404/406 ms. Both downloaded reports survived restart unchanged. Next is the same comparison at operator-confirmed greater separation; if loss persists, evaluate envelope pacing and long main-loop work. No receiver connection or production-forwarding approval is implied by the software test passes.

The operator then confirmed **8–10 m** separation. Runs 913232/913233 recovered **27/30 clean** and **15/20 eligible injected** messages, again with zero invalid outputs and zero reported UART error events. Wire CRC failures remained (3 clean, 10 injected including 5 deliberate corruptions), so distance alone did not remove the problem. Both runs completed independently of the browser and saved/downloaded their reports. Next is controlled envelope pacing and measurement of long main-loop work under fixed supply/spacing conditions, while retaining all integrity and freshness gates. Stage 2 delivery acceptance and stage 3 production routing remain open.

## Stage 3 foundation checkpoint (supersedes loss-driven blocking above)

The operator explicitly deferred radio-loss investigation and authorized continued development. Firmware 0.10.3 adds observation freshness and receiver confirmation to the existing Wi-Fi path: reference-only or replayed-epoch traffic cannot refresh correction readiness, and BESTNAV age is advanced by elapsed report time and checked against the observation station. Survey collection retains its other configured gates. A nonblocking GNSS handshake replaces four 100 ms delays, removing a known long service interruption without attributing RF loss to it.

Production-code host tests passed. Details and limitations: [freshness evidence](../tests/2026-09-12-correction-freshness/README.md). This is the first stage-3 increment, **not a completed live SiK bridge**. Next implement selected-source/session integration, bounded RTCM burst/reference/output queues and receiver forwarding, then reconnect UM980s for real correction-age and RTK-recovery validation. Do not require perfect synthetic delivery before doing that work.

**Pause/deployment status:** both 0.10.3 builds passed, but no flash or real-device validation of this increment was performed. Both units remain on 0.10.2. The user reconnected both ESP32s and COM4/COM10 were visible; UM980s remain disconnected. The latest five-hour check rose from 89% used to 95% used during checks/documentation, crossing the user's 90% stop threshold; implementation stopped and this checkpoint was committed/pushed. Resume with review of the freshness observer and hardware validation, then the remaining forwarding work. The weekly allowance was not used for this decision.


## Stage 3 continuation — 0.10.4 (2026-09-13)

The live SiK adapter is implemented with explicit local session selection, reserved reference/observation burst queues and bounded COM2 output. Both native regression and actual two-instrument control-plane checks passed; saved jobs/reports survived restart. The preview returns to Wi-Fi at boot and requires a new Base session; automatic persistent pairing remains future work. See [bridge operation and limits](live-correction-bridge.md) and [test/deployment evidence](../tests/2026-09-13-correction-bridge/README.md).

The operator has now connected both UM980s. Both profiles verify, but initial Rover status has zero satellites/no fix and neither unit has a base reference. Antenna sky view and real RTCM/RTK outage recovery remain to be checked. Do not equate control-plane or native passes with receiver/field acceptance. The last five-hour check was 85% used; preserve this checkpoint and obey the 90% pause threshold.


Final pause: five-hour allowance reached **91% used / 9% remaining**. Operator confirmed GNSS antennas are connected but have no sky view indoors; PC USB must be disconnected to move outdoors. No live correction/recovery test was started. Follow the outdoor handoff in `docs/live-correction-bridge.md`; keep all instrument components powered, select a fresh session after any restart and validate receiver quality before testing outage recovery. Implementation stopped; documentation and backup are the remaining checkpoint actions.
