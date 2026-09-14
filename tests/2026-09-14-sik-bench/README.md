# SiK bench session with real UM980s — 0.11.5 (2026-09-14)

First bench validation of the production SiK correction link with real receivers, driven over the instrument HTTP APIs by `test/run_sik_bench.py` (added this session). Units: A=Base (192.168.100.20), B=Rover (192.168.100.19), both on 0.11.5, receiver profiles verified, indoors (no GNSS fix expected — link-level validation only).

## Radio hardware

- Probe (ATI via UART2, 57600 baud, GPIO17 TX / GPIO18 RX) on **both** units: `RFD SiK 2.0 on HM-TRP` — radios powered, wired and responding.
- Probe caveat learned: `radio_probe` shows a final-looking value from a partial capture and stale text persists between probes; the driver now waits out the full ~4.8 s probe window.

## Paired synthetic radio test — PASS both sides (run 602917)

30 s, clean profile, RTCM-shaped synthetic frames over the SiK air interface (no GNSS output):

| Role | Planned | Sent/Received | Errors | Integrity violations | Pair pass |
|---|---|---|---|---|---|
| Base | 15 | 15 | 0 | — | **TRUE** |
| Rover | 15 | 15 | 0 | 0 | **TRUE** |

Zero loss in both directions. (The 2026-09-12 bench tests lost 1–3 packets per 468; this run had none.)

## Live correction session — PASS at link level (session 367058285)

Base UM980 COM2 → CRC validation → RTM1 envelopes → SiK air → Rover reassembly → station guard → BurstQueue → rover UM980 COM2:

- Base submitted **~4.0 RTCM frames/s continuously** (496 in the first 120 s; +358 in a later 90 s window; 1 envelope per frame) — the UM980 emits the full profile (1006/1033 @10 s + 1074/1084/1094/1124 @1 s) even indoors without a fix.
- Rover reassembled **~94%** of submitted frames as complete CRC24Q-valid messages; **every delivered frame was forwarded into the rover UM980** (`output.forwarded` delta == reassembled delta, zero output rejections or faults).
- Wire errors: ~3.6% of envelopes (13 in 90 s) — consistent with the historical close-range bench corruption signature (radios metres apart); the transport absorbed it by design.
- Peer update control plane (hello/echo) flowed over the same radio channel; an armed Base pairtest engine's transmissions were visible in the Rover's radio capture.

## Findings

- **The production SiK correction path works end-to-end with real receivers** — previously only Wi-Fi was field-proven. What remains outdoor-dependent: RTK FIXED with real corrections, fix recovery after outages, differential-age behaviour under sky, and range.
- Bench-range packet loss (~5–6% of messages incl. wire errors) matches earlier findings; suspect close-range receiver saturation. Re-measure outdoors with separation before judging the transport.
- **Base-side link display semantics (pre-existing, now more visible):** during a working SiK session the Base shows `Radio Disconnected` because the Base has no delivery acknowledgement (the Rover shows `Radio Connected`). The old card showed the same `NO LINK` semantics. Candidate refinement: `Radio Transmitting` on the Base while frames are being submitted — needs an operator decision.
- Session note: the live SiK session persists until a route change or restart; switching corrections back to Wi-Fi is `POST /api/v1/diagnostic {"op":"corrections","transport":"wifi","confirm":true}` on each unit or the Diagnostics page.

## Evidence

`sik-bench-observation.json` — 90 s counter snapshots (Base/Rover corrections objects incl. queue, wire and output counters + peer status).
