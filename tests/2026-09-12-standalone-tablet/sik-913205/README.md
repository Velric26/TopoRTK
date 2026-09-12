# Six-metre SiK comparison — run 913205

User confirmed approximately six metres between instruments. Same configuration as the 30 cm baseline: 120 seconds, 1000 framed bytes/second per sender, both directions. No firmware or radio settings changed for this test. Both units were armed through their actual browser pages; the ESP32s generated and checked packets. Both browser contexts were offline for 20 seconds, then reconnected and downloaded saved reports.

| Direction | Sent | Received | Missing | Receiving parser errors |
|---|---:|---:|---:|---:|
| Base → Rover | 468 | 465 | 3 (0.64%) | 380 |
| Rover → Base | 468 | 465 | 3 (0.64%) | 418 |

Both reports have zero duplicates/reordering. Maximum valid-packet interarrival gaps: Base 1028 ms, Rover 753 ms. Parser errors count discarded bytes/invalid frames, not RF bit errors. Base's browser download occurred before its peer summary arrived; local losses already make the pair fail regardless of that summary. Reports are persisted. Survey records, roles and boot IDs did not change during the run. No diagnostic remains armed/running; browser control was released.

**Radio delivery: NOT PASSED. Browser workflow: PASSED.** The 30 cm baseline received 467/468 at Base and 465/468 at Rover. Six metres did not eliminate losses; this test does not isolate their cause or prove radio damage. Radio/UART/power investigation and sustained acceptance remain pending. No RTK or survey accuracy was tested; UM980s remain disconnected.

At completion, five-hour usage was observed at 97% used (3% remaining). No implementation continued; only documenting and backing up the result followed.
