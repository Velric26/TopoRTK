# OTA upload recovery — 0.11.1, 2026-09-14

**Software checks passed; Unit B USB installation verified. The corrected Wi-Fi upload has not yet been tested on hardware.** Unit A remains 0.11.0. Unit B is 0.11.1, Debug Off after its USB restart, saved survey state unchanged and receiver profile verified. No field collection was performed. The preceding [failed live transfer](../2026-09-14-ota-live/README.md) remains the only actual OTA attempt.

## Findings and changes

The original handler aborted on every non-positive receive result, including a single two-second timeout. It also returned the successful error-response send result, allowing ESP-IDF to drain unread body data before handling another request. Espressif's [v4.4.7 file-upload example](https://github.com/espressif/esp-idf/blob/v4.4.7/examples/protocols/http_server/file_serving/main/file_server.c) retries socket timeouts and returns `ESP_FAIL` when closing a rejected upload; [request cleanup](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_http_server/src/httpd_parse.c) explains the leftover-body drain. These code defects are confirmed. The original network stall and the precise contribution of body draining to the observed HTTP outage remain unproven.

- Retry temporary receive timeouts, bounded by 12 seconds without progress and the existing 120-second overall deadline. Hard disconnect/receive errors fail immediately. Ownership is checked between reads, and the service continues to validate ownership on each write.
- Return a close response plus `ESP_FAIL` on every rejected upload path, including pre-admission rejection. Never select a new boot image after incomplete/invalid input.
- Cancel in-flight Debug fetches, block new Debug API actions and suspend background polling during upload. This reduces competing browser connections; no increase in the ESP32's three-socket limit or buffering was introduced.
- Emit bounded USB diagnostic summaries containing received byte counts, timeout retries and elapsed time, without credentials or payload data.
- Add a reusable explicit-install live runner that captures network failures and detects API failure states rather than waiting only for generic UI failure wording. It never retries automatically and never enables Debug remotely.

## Validation

- Both PlatformIO targets built successfully. Unit B: RAM 131,764 / 327,680 bytes; app 1,239,469 / 6,553,600 bytes. Binary hashes are in `artifacts.json`; binaries/packages remain ignored and private.
- `run_ota_http_tests.py`: 12 production-handler cases with explicit socket/clock/OTA doubles: transient recovery, sustained stall, disconnect, hard receive error, total deadline despite progress, ownership loss, origin/type/admission rejection, write/finish failure and timer wrap. These verify return semantics and bounds, not actual TCP behavior. See `http-tests.txt`.
- Existing production OTA service: 19 fault cases; peer codec: 1,000 fault runs; paired adapters: handshake/update/recovery checks. See `service-tests.txt`.
- Production Debug/OTA browser fixtures passed access, idle, responsive layouts, explicit confirmation, new-boot success and specific upload failure without retry. See `debug-browser.json` and `ota-browser.json`.
- Unit B USB upload passed in 14.894 seconds with written data hash verified. Live HTTP confirmed 0.11.1, normal USB boot, Debug Off, no update lock/pause, preserved survey state and verified receiver profile. See `unit-b-usb.json`.

## Next live test

After physically enabling Debug on Unit B, keep USB/power connected. The receiving firmware must already be 0.11.1; uploading a new package to the old receiver cannot fix its running upload handler. No additional full-flash backup is needed for this test.

With Playwright available through Node, from the repository root:

```powershell
node firmware/um980-display-demo/test/run_ota_live.cjs --install B http://192.168.100.19 http://192.168.100.20 firmware/um980-display-demo/.pio/build/unit_b/firmware.tpk
```

This command performs a real, explicitly confirmed firmware install using the browser. It requires a peer preparation acknowledgement and keeps the unconfirmed-peer override off. Private evidence is saved under `.pio/ota-live-<timestamp>`. Capture the USB console alongside the next attempt to distinguish receive stalls from hard errors. Successful OTA restart/acceptance, actual rollback and longer-distance reliability remain pending.
