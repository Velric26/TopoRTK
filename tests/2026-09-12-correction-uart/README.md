# Clean/injected RTCM comparison and UART observations — 2026-09-12

Firmware **0.10.2** adds a clean traffic profile and per-run ESP32 UART observations to the standalone paired RTCM diagnostic. Both UM980s remain disconnected; synthetic messages only reach the validation sink.

## Confirmed conditions

The operator reported **20 cm antenna spacing**, ESP32s powered directly from PC USB, radios powered by a **3S 18650 pack**, and shared ground. Antennas remained attached. Unit A was Base (`192.168.100.20`, COM4), Unit B Rover (`192.168.100.19`, COM10). No baud, radio power, air rate, GPIO assignment, FIFO threshold or buffer-size settings changed for this comparison.

The operator subsequently confirmed a complete powered unit had been moved, with **8–10 metres** antenna separation. The exact ESP32 USB supply after relocation and line of sight were not separately reconfirmed. The operator called the moved instrument Base, but the post-move USB inventory retained COM4 and no longer showed COM10; physical labels therefore should not be used to infer the live software role. Both browser roles/IPs remained as above, and the test direction remained software Base → Rover.

The PC flashed firmware and exercised the actual browser controls at tablet size; it did not generate, relay or validate radio data. Each 60-second run included a 20-second browser disconnection. This is not evidence of a physical Android tablet test or field-range acceptance.

## Results

| Run | Separation | Profile | Rover complete messages | Wire CRC errors | Duplicate fragments | Assembly expiry | Invalid sink output | Pair delivery |
|---|---|---|---:|---:|---:|---:|---:|---|
| 913230 | 20 cm | Clean | **25 / 30** | 5 | 0 | 5 | **0** | Not passed |
| 913231 | 20 cm | Injected | **17 / 20 eligible** | 8 | 5 | 13 | **0** | Not passed |
| 913232 | 8–10 m | Clean | **27 / 30** | 3 | 0 | 3 | **0** | Not passed |
| 913233 | 8–10 m | Injected | **15 / 20 eligible** | 10 | 5 | 15 | **0** | Not passed |

Every Base run generated 30 messages and wrote 90 data envelopes without transmitter expiry or short writes. Each injected run deliberately omitted five fragments, corrupted five and duplicated five; its other ten messages are intentionally ineligible for sink delivery. First missing eligible sequences were 1/12 at 20 cm and 19/8 at 8–10 m (clean/injected). Later valid messages recovered, and neither corrupt nor incomplete output was observed at the sink.

**All reported UART FIFO overflow, receive-buffer-full, framing, parity and break counters were zero on both units in all four runs.** There were no output-backpressure polls or short writes. At 20 cm, Rover peak sampled receive backlog was 768 bytes clean / 1024 injected, below the configured 4096-byte buffer. Maximum service intervals were Base 403/404 ms and Rover 404/406 ms (clean/injected). At 8–10 m, Rover peak backlog was 768 bytes in both runs; maximum service intervals were Base 403/404 ms and Rover 405/405 ms.

This evidence does not isolate the remaining loss to RF, the modem, wiring, supply or software. UART event delivery can be delayed or lost; zero events does not prove the electrical path was clean. Parity is disabled in 8N1. The several-hundred-millisecond service intervals are also a production scheduling concern; they do not by themselves explain the observed CRC failures. Total UART bytes include control traffic in different local observation windows, so subtracting the two instruments' totals is not a valid packet-loss calculation.

Both peer summaries arrived in every run. Reports were downloaded, prior local self-test and survey records/role/boot remained unchanged during each run, and control was released. After the two 20 cm runs, both units restarted; identical latest reports, including profile and frozen UART counters, were restored. The later 8–10 m reports were saved/read-back verified without another USB reset of the relocated unit. Both final tests finished with `busy=false`; result repeats are bounded to 60 seconds.

## Evidence and repeatable checks

- `native-results.txt`, `core_test.cpp`: clean/injected traffic for every supported duration, additional loss, timer wrap, profile mismatch/timeout, first missing sequence, and stalled output expiry/recovery; C++11 with warnings as errors. Native workspace 4184 bytes / ESP32 4164 bytes.
- `host-results.txt`: existing receiver/profile/storage/touch/web snapshot regression checks passed.
- `browser-results.json`, `diagnostic-*.png`: mocked production browser checks, including profile selection/locking, clean versus injected wording, UART observations, takeover, offline/stale controls, downloads and phone/tablet layouts. Hardware screenshots are separately inside each run directory.
- `build.txt`, `flash.txt`: both builds and flash hashes verified. Static RAM **106884 bytes (32.6%)**, firmware about 1.175 MB. Most added static RAM is bounded report/USB snapshot capacity; JSON/HTTP report allocation is outside the fixed transport data path.
- `913230/` through `913233/`: instrument snapshots, real browser downloads, screenshots and workflow results. Workflow checks passed; **`pair_pass=false`** remains explicit.
- `restart-*.txt`, `restart-before.json`, `restart-results.json`: hard-reset/read-MAC checks, changed boot IDs, identical restored reports and idle state.

Compile `core_test.cpp` with include path `firmware/um980-display-demo/src`. Browser regression: `check_diagnostic_browser.cjs` with `TOPORTK_TEST_RECORD=tests/2026-09-12-correction-uart`. Real hardware: `check_correction_pair.cjs`, setting a fresh `TOPORTK_RUN`, `TOPORTK_PROFILE=clean` or `injected`, optional `TOPORTK_SECONDS`, and `TOPORTK_CONDITIONS` to record operator-reported conditions. The script rejects invalid profile requests, verifies auth, records UART fields and saves/downloads results. Process success means the workflow passed, not that every eligible message arrived.

The UART callback is supported by the pinned Arduino framework package `3.20017.241212+sha.dcc1105b` (`HardwareSerial.h/.cpp`, `onReceiveError`). It only increments counters under a short critical section; main-loop JSON generation takes a protected copy. Observations span arming through the local completion/cancel iteration, including handshake/drain and possibly the first result envelope; later result repeats are excluded.

## Next checkpoint

Greater separation did not eliminate the loss. These small samples and the incompletely recorded post-move supply do not establish that distance caused any delivery-rate change. Next, compare bounded envelope pacing against the current scheduler under fixed recorded conditions, and measure which main-loop work causes the roughly 400 ms service intervals. Keep changes separate so the comparison is interpretable; do not relax CRC, freshness or sink checks to obtain a pass. UART/modem/electrical/RF loss is still not isolated. Real receiver routing also needs freshness, burst/reference retention and output-backpressure policies. Keep UM980s disconnected for these synthetic checks. Only COM4 was visible after relocation; any further flash must verify the actual connected unit rather than relying on the operator's physical Base/Rover label.
