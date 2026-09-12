# Standalone transport diagnostic checkpoint - 2026-09-12

Both ESP32s were flashed with hash verification. Current saved roles: Unit A / COM4 / 192.168.100.20 is Base; Unit B / COM10 / 192.168.100.19 is Rover. USB serial identities end 16:C8 and 16:70 respectively. UM980s are disconnected. The user reports both radios externally powered, LEDs on, common grounds connected, approximately 10 cm apart.

- Native C++ protocol checks passed: CRC vector, clean pair, missing/corrupt data, uint32 timer wrap, timeout, cancellation, replay and configuration bounds.
- Wi-Fi run 912201: 30 seconds, 1000 framed bytes/second in both directions. Both endpoints sent/received 117 of 117, zero errors/duplicates/reordering, matching peer reports, pair_pass true and persisted true. Largest receive gaps: Base 660 ms, Rover 669 ms. See wifi-base.json and wifi-rover.json. USB initiated the test; generation/checking ran on the ESP32s.
- Local UART identity probes on GPIO17 TX / GPIO18 RX, 57600 baud did not identify either SiK. See probe-unit-a.json and probe-unit-b.json. Missing responses do not prove reversed wires.
- SiK run 912202: neither endpoint received a matching peer handshake after six seconds. Both remained armed with zero receive/parser-error counters; cancelled deliberately. See sik-attempt-base.json and sik-attempt-rover.json. This is not a completed acceptance run. User asked to check crossed wiring and J8 physical pin numbers.
- A subsequent correction makes JSON report parsing preserve the stored report buffer rather than modifying it in place; final-flash.txt records Unit A success and Unit B upload failure (COM10 disappeared). Unit A restored the cancelled SiK report correctly after reboot; see persistence-check.json. Unit B subsequently reconnected and the final fix flashed with hash verification; see retry-flash-unit-b.txt.

Pending: tablet-operated field/range checks, intended field-power qualification, and real RTCM/survey acceptance. The production SiK correction bridge and BLE adapter remain unimplemented. The UM980s can remain disconnected for the UART/synthetic checks.

The old node0/node1, pair.json and Python-unit artifacts belong only to an earlier development host prototype. They are not evidence for this firmware or physical RF transport; no PC is required by the selected field workflow.

## Final result after swapping TX/RX

The user swapped the UART wires. Both local probes then returned `RFD SiK 2.0 on HM-TRP`; see probe-crossed-base.json and probe-crossed-rover.json. This confirms the previous TX/RX arrangement was reversed relative to the working connection.

SiK run 912203 completed on the ESP32s: 30 seconds, 1000 framed bytes/second in both directions, 117/117 sent and received at each endpoint, zero errors/duplicates/reordering, matching peer summaries, pair_pass true, persisted true. See sik-crossed-base.json and sik-crossed-rover.json. This is close-bench synthetic acceptance at approximately 10 cm, not range or survey qualification.

Implementation paused when the five-hour allowance was observed at 92% used (8% remaining). Only capture of the already-running bounded test, documentation and repository backup continued. Next: tablet-operated test and distance/power qualification; production SiK RTCM bridge and BLE remain pending.
