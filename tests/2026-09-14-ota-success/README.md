# First successful Wi-Fi OTA — Unit B, 2026-09-14

**PASS: Unit B updated from 0.11.1 to 0.11.2 through the real browser over local Wi-Fi, rebooted and passed startup acceptance.** Unit A remains 0.11.0. No USB flash was performed during this session; USB was used for observation/recovery availability only. UM980 and SiK firmware were not changed.

## Attempts and timing fix

Attempt 2 acknowledged preparation but reported `Update expired or controller lost` immediately after confirmation, with zero bytes transferred and no reboot. Normal processing resumed and the lock cleared. See `attempt-2-cancelled.json`.

Inspection found that `ota_service(now, ...)` could acquire its mutex after HTTP updated `changed` with a newer timestamp. Unsigned subtraction then made a fresh request look expired. New stale-prepare and stale-start tests reproduced failure against the original production source. Version 0.11.2 resamples the clock inside the mutex before inspecting shared state. It also distinguishes cancellation, controller loss, Debug disable and actual stage expiry in error messages. All 23 production OTA service cases pass, including stale timestamps, the genuine 60-second deadline and prevention of premature reboot from a stale success timestamp. Both target builds passed. This establishes the code defect and its correction; no trace of the exact internal scheduling of attempt 2 was available.

Attempt 3 used the running 0.11.1 upload handler to install the 0.11.2 package. It passed without the unconfirmed-peer override. Unit A reported preparation, `Rover updating - reception paused`, then `Peer reconnected - checking corrections`, then cleared its notice. These observations came from live peer status; no camera claim is made about the touchscreen pixels. See `attempt-3-success.json`.

## Actual acceptance evidence

- Upload endpoint returned HTTP 200. USB summary: **1,240,064 bytes verified, zero receive-timeout retries, 118,602 ms**. See `serial-summary.txt`.
- Unit B boot ID changed from 2935605947 to 1450655027; firmware reported **0.11.2**, boot verdict **New firmware verified**.
- Debug returned Off; update state idle, unlocked and unpaused. Saved survey subset matched exactly before/after; no survey point was collected. The browser reported no JavaScript errors. Brief aborted status fetches during reboot were observed and recovered.
- Application/package hashes are in `artifacts.json`. Packages and full raw serial/state snapshots remain private under ignored `.pio`.

## Limits and next work

The transfer was only about 1.4 seconds inside the 120-second budget. Zero receive timeouts means the new timeout retries did not activate on this successful attempt; it does not identify the throughput bottleneck. Profile erase/write/read timing and browser progress before choosing a longer bounded deadline or changing buffering. This is one local-router success, not evidence of field reliability or hotspot performance.

Next, after improving or explaining the time margin, test deliberate interrupted uploads and real bootloader rollback with USB recovery available. Unit A still needs a planned update if both units are to run the timing fix. Re-enable Debug physically only when ready for another test. Work stopped after documentation/commit/push with the five-hour allowance close to the requested pause threshold (13% remaining at the check).
